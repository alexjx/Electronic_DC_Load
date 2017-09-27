#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <TimerOne.h>
#include <LiquidCrystal_I2C.h>
#include <ClickEncoder.h>

#include "ad5541.h"
#include "adc.h"
#include "button.h"
#include "fan.h"
#include "setter.h"
#include "lm35.h"


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

#define MAX_BUTTON           4

#define ADC_CURRENT_CHN      AD7190_CH_AIN2P_AINCOM
#define ADC_VOLTAGE_CHN      AD7190_CH_AIN1P_AINCOM

// Constants
const double VREF_VOLTAGE = 5000.0;  // mV


///////////////////////
// Devices
///////////////////////

// DAC
AD5541 ad5541(DAC_CS_PIN);

// ADC
ADConverter adc(ADC_CS_PIN,
                ADC_VOLTAGE_CHN,
                ADC_CURRENT_CHN,
                VREF_VOLTAGE);


// LCD
LiquidCrystal_I2C lcd(LCD_IIC_ADDRESS,
                      LCD_IIC_COLS,
                      LCD_IIC_ROWS);

// encoder
ClickEncoder encoder(ENCODER_PIN_1,
                     ENCODER_PIN_2,
                     ENCODER_SW_PIN,
                     ENCODER_UPDATE_RATE);


// Buttons
Button buttons[MAX_BUTTON] {
    BUTTON_1_PIN,
    BUTTON_2_PIN,
    BUTTON_3_PIN,
    BUTTON_4_PIN,
};


// FAN
FanController fan(FAN_SW_PIN);


// temperature sensor
LM35 lm35(LM35_PIN, VREF_VOLTAGE);


// Setter (max 15000mA)
Setter<15000> current_set_point;


//////////////////////////////
// Operations
//////////////////////////////

enum OPTERATION_STATE
{
    STATE_IDLE = 1,
    STATE_RUNNING = 2,
    STATE_CALIBRATION = 10,

    STATE_UNDEF = 99,
};


// Global Data
struct {

    // status
    OPTERATION_STATE state;
    uint32_t state_time;

    double max;

    // page
    int page;

} g_cb { STATE_IDLE, 0, 0.0, 0 };


// timer service
void timer_one_isr()
{
    encoder.service();
}


void UpdateButtons()
{
    for (int i = MAX_BUTTON; i > 0; i--) {
        buttons[i - 1].update();
    }
}


void UpdateCurrentVoltage()
{
    // update current data, no need for idle
    if (g_cb.state != STATE_IDLE) {
        adc.updateCurrent();
    } else {
        adc.resetCurrent();
    }
    // update voltage
    adc.updateVoltage();
}


void UpdateTemperature()
{
    lm35.update();
}


void UpdateSensors()
{
    // update inputs
    UpdateButtons();

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
    lcd.noCursor();

    // Display
    // Line 1 - Current Set Point, temperature:
    //   aa.aaaA ttt.ttC X
    lcd.setCursor(0, 0);
    DisplayFixedDouble(current_set_point.as_double(), 6, 3);
    lcd.print("A ");
    DisplayFixedDouble(lm35.getTemperature(), 5, 2);
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

    if (g_cb.page == 0) {
        DisplayFixedDouble(adc.readCurrent(), 6, 3);
        lcd.print("A ");
        DisplayFixedDouble(adc.readVoltage(), 6, 3);
        lcd.print("V ");
    } else if (g_cb.page == 1) {
        double wattage = adc.readVoltage() * adc.readCurrent();
        DisplayFixedDouble(wattage, 8, 4);
        lcd.print("W ");
        lcd.print(ad5541.getValue(), HEX);
        lcd.print("     ");
    }

    uint8_t bit = 5 - current_set_point.current_bit();
    if (bit < 3) {
        bit -= 1;
    }
    lcd.setCursor(bit, 0);
    lcd.cursor();

}


void ProcessControl()
{
    uint32_t now = millis();

    // if current exceeded max, abort
    if (adc.readCurrent() > 16.000) {
        g_cb.state = STATE_IDLE;
        ad5541.setValue(0);
        return;
    }

    if (adc.readCurrent() > g_cb.max)
        g_cb.max = adc.readCurrent();

    // temperature control
    if (lm35.getTemperature() > 40.0 && !fan.isOn()) {
        fan.turn_on();
    } else if (lm35.getTemperature() < 35.0 && fan.isOn()) {
        fan.turn_off();
    }

    if (buttons[1].isRaisingEdge()) {
        current_set_point.move_left();
    } else if (buttons[2].isRaisingEdge()) {
        current_set_point.move_right();
    }
    current_set_point.increase(encoder.getValue());

    if (buttons[3].isRaisingEdge()) {
        g_cb.page = (g_cb.page + 1) % 2;
    }

    // FIXME: change this to PID
    static uint32_t last;
    static double last_e;
    static double e_sum;
    double pid_sum = 0.0;
    if (now - last > 10) {
        double e = current_set_point.as_double() - adc.readCurrent();
        if (e > -0.0005 && e < 0.0005) {
            e = 0.0; // dead band
        }
        double p_term = e * 2600.0; // _kP
        e_sum += e * (now - last);
        e_sum = constrain(e_sum, -0.2/0.03, 0.2/0.03);
        double i_term = e_sum * 0.03;
        double d_term = (e - last_e) / (now - last) * 600.0;
        double pid_sum = p_term + i_term - d_term;
        // double pid_sum = p_term;
        // pid_sum *= 3.5;
        last = now;
        last_e = e;
    }


    int32_t set_point = (int32_t)ad5541.getValue();
    set_point += (int32_t)pid_sum;
    // int32_t set_point = (int32_t)pid_sum;
    set_point = constrain(set_point, AD5541_CODE_LOW, AD5541_CODE_HIGH);

    if (buttons[0].isActive()) {
        lcd.clear();
        lcd.home();
        lcd.print(e);
        lcd.print(" ");
        lcd.print(pid_sum);
        lcd.print(" ");
        lcd.print(set_point, HEX);
        lcd.print(ad5541.getValue(), HEX);
        lcd.setCursor(0, 1);
        lcd.print(p_term);
        lcd.print(" ");
        lcd.print(i_term);
        lcd.print(" ");
        lcd.print(d_term);
        delay(1000);
    }

    // State change event
    ClickEncoder::Button encoder_btn = encoder.getButton();
    if (g_cb.state == STATE_IDLE &&
        encoder_btn == ClickEncoder::Held &&
        g_cb.state_time + 3000.0 < now)
    {
        // IDLE -> RUNNING
        g_cb.state = STATE_RUNNING;
        g_cb.state_time = now;
        ad5541.setValue(0);
        last_e = 0.0;
    }
    else if (g_cb.state == STATE_RUNNING)
    {
        if (encoder_btn == ClickEncoder::Clicked)
        {
            g_cb.state = STATE_IDLE;
            ad5541.setValue(0);
        }
        else
        {
            ad5541.setValue((uint16_t)set_point);
        }
    }
}


void setup()
{
    // initialize state to idle
    g_cb.state = STATE_IDLE;

    // LCD
    lcd.init();
    lcd.home();
    lcd.print("@DC Active Load@");
    lcd.setCursor(0, 1);
    lcd.print("       20170927");
    lcd.home();

    // SPI
    SPI.begin();

    // DAC
    ad5541.begin();
    ad5541.setValue(0);

    // Timer
    Timer1.initialize(1000);
    Timer1.attachInterrupt(timer_one_isr);

    // temperature
    lm35.init();

    // ADC
    adc.begin();
    while (!adc.detectDevice()) {
        lcd.clear();
        lcd.print("ADC ERROR");
        delay(300);
    }
    delay(10);
    adc.init();
    // FIXME: Calibration data should be gotten from EEPROM
    adc.setCalibData(AD7190_CONF_GAIN_1, 1.00080, 4.0);
    adc.setCalibData(AD7190_CONF_GAIN_8, 1.00243, -0.60);

    // Buttons
    buttons[0].init();
    buttons[1].init();
    buttons[2].init();
    buttons[3].init();

    // FAN
    fan.init();

    // get lcd ready for using information
    delay(1000);
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

