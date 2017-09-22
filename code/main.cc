#include <Wire.h>
#include <SPI.h>
#include <TimerOne.h>
#include <LiquidCrystal_I2C.h>
#include <ClickEncoder.h>

#include "ad5541.h"
#include "ad7190.h"


// Hardware Configuration
#define LCD_IIC_ADDRESS      0x20
#define LCD_IIC_COLS         16
#define LCD_IIC_ROWS         2

#define ENCODER_PIN_1        A0
#define ENCODER_PIN_2        A1
#define ENCODER_SW_PIN       2
#define ENCODER_UPDATE_RATE  4


#define ADC_CS_PIN           8
#define DAC_CS_PIN           9

#define BUTTON_1_PIN         3
#define BUTTON_2_PIN         4
#define BUTTON_3_PIN         6
#define BUTTON_4_PIN         5

#define FAN_SW_PIN           7

#define LM35_PIN             A3

#define DEBOUND_DELAY        330
#define MAX_GAINS            2
#define MAX_BUTTON           4

#define ADC_CURRENT_CHN      AD7190_CH_AIN2P_AINCOM
#define ADC_VOLTAGE_CHN      AD7190_CH_AIN1P_AINCOM

// Constants
const double VREF_VOLTAGE = 5000.0;  // mV
const struct {
    uint8_t value;
    double lower;
} gains[MAX_GAINS] = {
    {AD7190_CONF_GAIN_1, 600.0},  // AD7190_CONF_GAIN_1
    {AD7190_CONF_GAIN_8, 0.0},  // AD7190_CONF_GAIN_8
    // {AD7190_CONF_GAIN_32, 0.0},   // AD7190_CONF_GAIN_32
};

// DAC
AD5541 ad5541(DAC_CS_PIN);

// ADC
AD7190 ad7190(ADC_CS_PIN);


// LCD
LiquidCrystal_I2C lcd(LCD_IIC_ADDRESS,
                      LCD_IIC_COLS,
                      LCD_IIC_ROWS);

// encoder
ClickEncoder encoder(ENCODER_PIN_1,
                     ENCODER_PIN_2,
                     ENCODER_SW_PIN,
                     ENCODER_UPDATE_RATE);


enum OPTERATION_STATE
{
    STATE_IDLE = 1,
    STATE_RUNNING = 2,
    STATE_CALIBRATION = 10,

    STATE_UNDEF = 99,
};


// Global Data
struct {

    // button status
    struct {
        uint8_t active;
        uint32_t time;

    } buttons[MAX_BUTTON];

    // encoder value
    int16_t encoder_val;
    ClickEncoder::Button encoder_btn;

    // ADC value
    double current;
    double voltage;

    struct {
        double offset;
        double scale;
    } calib[MAX_GAINS];

    // DAC
    int32_t dac_set_point;

    // FAN
    uint8_t fan_is_on;

    // temperature
    double temperature;

    // status
    OPTERATION_STATE state;
    uint32_t state_time;

} g_cb = {0};


// timer service
void timer_one_isr()
{
    encoder.service();
}


// ADC helper
double ReadADC(int chn)
{
    double voltage = 0.0;
    ad7190.BeginTransaction();

    ad7190.ConfigChannel(chn);
    for (int i = 0; i < MAX_GAINS; i++) {
        ad7190.SetGain(gains[i].value);
        ad7190.SetMode(AD7190_MODE_SINGLE);
        uint32_t value = ad7190.ReadDataRegister();
        uint8_t gain_factor = ad7190.GetGainFactor();
        uint8_t status = (uint8_t)(value & 0xFF);
        value >>= 8;

        // calculate the voltage
        voltage = (double)value * VREF_VOLTAGE / AD7190_CODES / gain_factor;
        voltage *= g_cb.calib[i].scale;
        voltage += g_cb.calib[i].offset;

        // if we could not use a better resolution call it
        if (voltage > gains[i].lower) {
            break;
        }
    }

    ad7190.EndTransaction();
    return voltage;
}


void UpdateButtons()
{
    uint32_t now = millis();
#define UPDATE_BUTTON(index)                                                          \
    {                                                                                 \
        if (!g_cb.buttons[index - 1].active && !digitalRead(BUTTON_##index##_PIN)) {      \
            if (g_cb.buttons[index - 1].time + DEBOUND_DELAY < now) {                     \
                g_cb.buttons[index - 1].active = 1;                                       \
                g_cb.buttons[index - 1].time = now;                                       \
            }                                                                         \
        } else if (g_cb.buttons[index - 1].active && digitalRead(BUTTON_##index##_PIN)) { \
            if (g_cb.buttons[index - 1].time + DEBOUND_DELAY < now) {                     \
                g_cb.buttons[index - 1].active = 0;                                       \
                g_cb.buttons[index - 1].time = now;                                       \
            }                                                                         \
        }                                                                             \
    }
    UPDATE_BUTTON(1);
    UPDATE_BUTTON(2);
    UPDATE_BUTTON(3);
    UPDATE_BUTTON(4);
#undef UPDATE_BUTTON
}


void UpdateEncoder()
{
    g_cb.encoder_btn = encoder.getButton();
    g_cb.encoder_val = encoder.getValue();
}


void UpdateCurrentVoltage()
{
    // update current data, no need for idle
    if (g_cb.state == STATE_IDLE) {
        g_cb.current = 0.0;
    } else {
        g_cb.current = ReadADC(ADC_CURRENT_CHN) / 50.0 / 0.005 / 1000.0;
    }
    // update voltage
    g_cb.voltage = ReadADC(ADC_VOLTAGE_CHN) * 10.09 / 1000.0;
}


void UpdateTemperature()
{
    int reg = analogRead(LM35_PIN);
    g_cb.temperature = (double)reg * VREF_VOLTAGE / 1024.0 / 10.0;
}


void UpdateSensors()
{
    // update inputs
    UpdateButtons();
    UpdateEncoder();

    // sensors
    UpdateCurrentVoltage();
    UpdateTemperature();
}


/// Display a double with Fixed length
void DisplayFixedDouble(double value, int width, int prec)
{
    char line[20];
    dtostrf(value, width, prec, line);
    int len = strlen(line);
    if (len > width) {
        // if we have longger value, truncate it
        line[width] = '\0';
    } else {
        // if the first one is space then we have leading spaces
        if (line[0] == ' ') {
            for (char* p = strchr(line, ' '); p; p = strchr(line, ' ')) {
                *p = '0';
            }
        }
    }
    lcd.print(line);
}


void UpdateDisplay()
{
    // Display
    // Line 1 - Current Set Point, temperature:
    //   aa.aaaA ttt.ttC X
    lcd.setCursor(0, 0);
    DisplayFixedDouble((double)g_cb.dac_set_point * VREF_VOLTAGE / (double)AD5541_CODES / 1000.0, 6, 4);
    lcd.print("V ");
    DisplayFixedDouble(g_cb.temperature, 5, 2);
    lcd.print("C ");
    // FIXME: print out status
    switch (g_cb.state) {
        case STATE_IDLE:
            lcd.print(" ");
            break;
        case STATE_RUNNING:
            lcd.print("R");
        default:
            lcd.print("?");
    }

    // Line 2 - Current Sensing, Voltage Sensing:
    //   ss.ssssA vvv.vvvV
    lcd.setCursor(0, 1);
    DisplayFixedDouble(g_cb.current, 6, 3);
    lcd.print("A ");
    DisplayFixedDouble(g_cb.voltage, 6, 3);
    lcd.print("V ");
}


void ProcessControl()
{
    uint32_t now = millis();

    // temperature control
    if (g_cb.temperature > 40.0 && !g_cb.fan_is_on) {
        g_cb.fan_is_on = true;
        digitalWrite(FAN_SW_PIN, HIGH);
    } else if (g_cb.temperature < 35.0 && g_cb.fan_is_on) {
        g_cb.fan_is_on = false;
        digitalWrite(FAN_SW_PIN, LOW);
    }

    if (g_cb.buttons[2].active && g_cb.buttons[3].active && (g_cb.buttons[2].time + 3000.0 < now)) {
        g_cb.state = STATE_CALIBRATION;
    }
    else
    {
        int32_t set_point = g_cb.dac_set_point;
        // set point change
        if (g_cb.buttons[0].active) {
            set_point += 0x100;
        } else if (g_cb.buttons[1].active) {
            set_point -= 0x100;
        } else if (g_cb.buttons[2].active) {
            set_point += 0x10;
        } else if (g_cb.buttons[3].active) {
            set_point -= 0x10;
        }
        set_point += g_cb.encoder_val;
        set_point = constrain(set_point, AD5541_CODE_LOW, AD5541_CODE_HIGH);

        // State change event
        if (g_cb.state == STATE_IDLE &&
            g_cb.encoder_btn == ClickEncoder::Held &&
            g_cb.state_time + 3000.0 < now)
        {
            // IDLE -> RUNNING
            g_cb.state = STATE_RUNNING;
            g_cb.state_time = now;
            ad5541.setValue(set_point);
        }
        else if (g_cb.state == STATE_RUNNING)
        {
            if (g_cb.encoder_btn == ClickEncoder::Clicked)
            {
                g_cb.state = STATE_IDLE;
                ad5541.setValue(0);
            }
            else if (g_cb.dac_set_point != set_point)
            {
                ad5541.setValue(set_point);
            }
        }

        g_cb.dac_set_point = set_point;
    }
}


void setup()
{
    // initialize state to idle
    g_cb.state = STATE_IDLE;

    // LCD
    lcd.init();
    lcd.home();
    lcd.print("DC Active Load");
    lcd.setCursor(0, 1);
    lcd.print("R20170922");

    // SPI
    SPI.begin();

    // VREF
    analogReference(EXTERNAL);

    // ADC
    ad7190.begin();
    ad7190.BeginTransaction();
    while (!ad7190.init()) {
        lcd.clear();
        lcd.print("ADC Error");
        delay(200);
    }
    ad7190.ConfigUnipolar(1);
    ad7190.ConfigFilter(48);
    ad7190.SetGain(AD7190_CONF_GAIN_1);
    ad7190.Calibrate(AD7190_CH_AIN1P_AINCOM);
    ad7190.Calibrate(AD7190_CH_AIN2P_AINCOM);
    ad7190.SetMode(AD7190_MODE_PWRDN);
    ad7190.EndTransaction();
    // FIXME: Calibration data should be gotten from EEPROM
    g_cb.calib[0].scale = 1.0;
    g_cb.calib[0].offset = 2.5;
    g_cb.calib[1].scale = 1.0;

    // Timer
    Timer1.initialize(1000);
    Timer1.attachInterrupt(timer_one_isr);

    // Buttons
    pinMode(BUTTON_1_PIN, INPUT);
    pinMode(BUTTON_2_PIN, INPUT);
    pinMode(BUTTON_3_PIN, INPUT);
    pinMode(BUTTON_4_PIN, INPUT);

    // FAN
    pinMode(FAN_SW_PIN, OUTPUT);
    digitalWrite(FAN_SW_PIN, g_cb.fan_is_on);

    // DAC
    ad5541.begin();
    ad5541.setValue(g_cb.dac_set_point);

    // get lcd ready for using information
    delay(3000);
    lcd.clear();
}


void loop()
{
    // Read Information
    UpdateSensors();
    // TODO: Controller logic
    ProcessControl();
    // Output:
    UpdateDisplay();
}

