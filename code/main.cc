#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <TimerOne.h>
#include <LiquidCrystal_I2C.h>
#include <ClickEncoder.h>
#include <EEPROM.h>

#include "ad5541.h"
#include "adc.h"
#include "button.h"
#include "fan.h"
#include "setter.h"
#include "lm35.h"
#include "control.h"


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

#define EEPROM_VERSION_ADDR  0x00
#define EEPROM_VERSION       0x0a

#define EEPROM_CURRENT_ADDR  0x10
#define EEPROM_VOLTAGE_ADDR  0x20


// Constants
const double VREF_VOLTAGE = 5000.0;  // mV

const double MAX_WATTAGE = 200.0;

constexpr int32_t MAX_CURRENT_MILLIAMPS = 15000;
constexpr double MAX_CURRENT = MAX_CURRENT_MILLIAMPS / 1000.0;

const double MAX_TEMPERATURE = 95.0;

const int MAX_PAGE = 4;

const double PID_K_COEFF = 0.0;
const double PID_I_COEFF = 0.0;
const double PID_D_COEFF = 0.0;


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
Setter<MAX_CURRENT_MILLIAMPS> current_set_point;
// Cut off voltage set
Setter<99999l> voltage_set_point;

// 0 - 4 current 5 - 9 cut off voltage
int8_t setter_position = 4;
const int MAX_SET_POSITION = 10;


//////////////////////////////
// Operations
//////////////////////////////

// Global Data
struct {
    control::ControllerState controller;
    control::MeasurementSnapshot measurement;

    double mah;
    double watt_h;
    uint32_t update_last;
    bool adc_initialized;

    // page
    int page;

} g_cb {
    control::ControllerState(),
    { 0.0, 0.0, 0.0, false, false, false, 0 },
    0.0, 0.0, 0, false, 0
};

double p_term = 0.0, i_term = 0.0, d_term = 0.0;



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
    if (!g_cb.adc_initialized) {
        adc.resetCurrent();
        g_cb.measurement.current = 0.0;
        g_cb.measurement.voltage = 0.0;
        g_cb.measurement.current_valid = false;
        g_cb.measurement.voltage_valid = false;
        return;
    }

    const bool current_valid = adc.updateCurrent();
    if (!current_valid) {
        g_cb.measurement.current = adc.readCurrent();
        g_cb.measurement.voltage = adc.readVoltage();
        g_cb.measurement.current_valid = false;
        g_cb.measurement.voltage_valid = false;
        return;
    }

    const bool voltage_valid = adc.updateVoltage();

    g_cb.measurement.current = adc.readCurrent();
    g_cb.measurement.voltage = adc.readVoltage();
    g_cb.measurement.current_valid = current_valid;
    g_cb.measurement.voltage_valid = voltage_valid;
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

    g_cb.measurement.temperature = lm35.getTemperature();
    g_cb.measurement.temperature_valid = lm35.isValid();
    g_cb.measurement.timestamp_ms = millis();
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

    if (g_cb.controller.state == control::OperationState::Fault) {
        lcd.setCursor(0, 0);
        switch (g_cb.controller.fault) {
            case control::FaultReason::AdcFailure:
                lcd.print(F("FAULT ADC       "));
                break;
            case control::FaultReason::TemperatureSensorFailure:
                lcd.print(F("FAULT TEMP SNS  "));
                break;
            case control::FaultReason::Overcurrent:
                lcd.print(F("FAULT OVERCUR   "));
                break;
            case control::FaultReason::Undervoltage:
                lcd.print(F("FAULT UNDERVOLT "));
                break;
            case control::FaultReason::Overtemperature:
                lcd.print(F("FAULT OVERTEMP  "));
                break;
            default:
                lcd.print(F("FAULT UNKNOWN   "));
                break;
        }
        lcd.setCursor(0, 1);
        lcd.print(F("Click to ack    "));
        return;
    }

    // Display
    // Line 1 - Current Set Point, temperature:
    //   aa.aaaA ttt.ttC X
    lcd.setCursor(0, 0);
    DisplayFixedDouble(current_set_point.as_double(), 6, 3);
    lcd.print("A ");
    DisplayFixedDouble(voltage_set_point.as_double(), 6, 3);
    lcd.print("V");

    // FIXME: print out status
    switch (g_cb.controller.state) {
        case control::OperationState::Idle:
            lcd.print(" ");
            break;
        case control::OperationState::Running:
            lcd.print("*");
            break;
        default:
            lcd.print("?");
    }

    // Line 2 - Current Sensing, Voltage Sensing:
    //   ss.ssssA vvv.vvvV
    lcd.setCursor(0, 1);

    if (g_cb.page == 0) {
        DisplayFixedDouble(g_cb.measurement.current, 6, 3);
        lcd.print("A ");
        DisplayFixedDouble(g_cb.measurement.voltage, 6, 3);
        lcd.print("V ");
    } else if (g_cb.page == 1) {
        double wattage = g_cb.measurement.voltage * g_cb.measurement.current;
        DisplayFixedDouble(wattage, 8, 4);
        lcd.print("W ");
        DisplayFixedDouble(g_cb.measurement.temperature, 5, 2);
        lcd.print("C");
    } else if (g_cb.page == 2) {
        DisplayFixedDouble(g_cb.mah, 8, 2);
        lcd.print("mAh     ");
    } else if (g_cb.page == 3) {
        DisplayFixedDouble(g_cb.watt_h, 8, 2);
        lcd.print("Wh      ");
    }

    // positiont the cursor for showing
    // current 01.345A 89.123V
    uint8_t bit = setter_position;
    if (setter_position >= 2) {
        bit++;
    }
    if (setter_position > 4) {
        bit += 2;
    }
    if (setter_position >= 7) {
        bit++;
    }
    lcd.setCursor(bit, 0);
    lcd.cursor();

}

static uint32_t pid_last;
static double last_input;
static double pid_sum;
static double e;
static double e_sum;


void SaveSetPointToEEPROM()
{
    current_set_point.save_to_eeprom(EEPROM_CURRENT_ADDR);
    voltage_set_point.save_to_eeprom(EEPROM_VOLTAGE_ADDR);
}


void UpdateCursorPosition()
{
    if (setter_position < 5) {
        current_set_point.set_position(setter_position);
    } else {
        voltage_set_point.set_position(setter_position - 5);
    }
}


void StopDischarge()
{
    ad5541.setValue(0);
    control::stop(g_cb.controller, millis());
    SaveSetPointToEEPROM();
}


bool StartDischarge()
{
    uint32_t now = millis();
    if (!control::tryStart(g_cb.controller,
                           now,
                           g_cb.controller.state_since_ms,
                           true)) {
        return false;
    }

    ad5541.setValue(0);
    e_sum = 0.0;
    last_input = 0.0;
    pid_sum = 0.0;
    p_term = 0.0;
    i_term = 0.0;
    d_term = 0.0;
    g_cb.mah = 0;
    g_cb.watt_h = 0;
    g_cb.update_last = now;
    pid_last = now;
    SaveSetPointToEEPROM();
    return true;
}


void ProcessControl()
{
    // All control decisions in this pass use the same sensor sample.
    const uint32_t now = g_cb.measurement.timestamp_ms;
    const control::MeasurementSnapshot& measurement = g_cb.measurement;

    // A failed/unsafe reading is handled before any user input, PID, or DAC
    // processing.  This also keeps a newly latched fault from restarting in
    // the same pass.
    if (!measurement.temperature_valid ||
        measurement.temperature > MAX_TEMPERATURE) {
        fan.turn_on();
    } else if (measurement.temperature > 40.0 && !fan.isOn()) {
        fan.turn_on();
    } else if (measurement.temperature < 35.0 && fan.isOn()) {
        fan.turn_off();
    }

    const control::SafetyLimits limits = {
        MAX_CURRENT * 1.1,
        voltage_set_point.as_double(),
        MAX_TEMPERATURE
    };
    const control::FaultReason unsafe_reason = control::evaluateSafety(
        measurement, limits, g_cb.controller.state);
    if (unsafe_reason != control::FaultReason::None) {
        control::latchFault(g_cb.controller, unsafe_reason, now);
        ad5541.setValue(0);
        return;
    }

    ClickEncoder::Button encoder_btn = encoder.getButton();
    if (g_cb.controller.state == control::OperationState::Fault) {
        ad5541.setValue(0);
        // Do not acknowledge until all three measurements are valid.  Return
        // after acknowledgement so a held button cannot start immediately.
        if (encoder_btn == ClickEncoder::Clicked && measurement.adcValid() &&
            measurement.temperature_valid) {
            control::acknowledgeFault(g_cb.controller, now);
        }
        return;
    }

    // A click is the user stop command and must make the output safe before
    // any further work in this pass.
    if (g_cb.controller.state == control::OperationState::Running &&
        encoder_btn == ClickEncoder::Clicked) {
        StopDischarge();
        return;
    }

    // configuration setter control
    bool pos_changed = false;
    if (buttons[1].isRaisingEdge()) {
        setter_position = (setter_position - 1) % MAX_SET_POSITION;
        if (setter_position < 0) {
            setter_position += MAX_SET_POSITION;
        }
        pos_changed = true;
    } else if (buttons[2].isRaisingEdge()) {
        setter_position = (setter_position + 1) % MAX_SET_POSITION;
        pos_changed = true;
    }

    if (pos_changed) {
        UpdateCursorPosition();
    }
    // find which to set
    noInterrupts();
    auto encoder_value = encoder.getValue();
    interrupts();
    if (encoder_value != 0) {
        if (setter_position < 5) {
            current_set_point.change(encoder_value);
            lcd.print(current_set_point.get_value());
        } else {
            lcd.print(voltage_set_point.get_value());
            voltage_set_point.change(encoder_value);
        }
    }

    // display control
    if (buttons[3].isRaisingEdge()) {
        g_cb.page = (g_cb.page + 1) % MAX_PAGE;
    }
    if (buttons[0].isRaisingEdge()) {
        g_cb.page = (g_cb.page - 1) % MAX_PAGE;
        if (g_cb.page < 0) {
            g_cb.page += MAX_PAGE;
        }
    }

    // A held encoder button retains the existing idle-to-running workflow.
    // The guarded model operation makes starting a fault impossible.
    if (g_cb.controller.state == control::OperationState::Idle &&
        encoder_btn == ClickEncoder::Held &&
        control::hasElapsed(now, g_cb.controller.state_since_ms, 3000UL)) {
        StartDischarge();
        return;
    }

    if (g_cb.controller.state != control::OperationState::Running) {
        pid_sum = 0.0;
        e = 0.0;
        p_term = 0.0;
        i_term = 0.0;
        d_term = 0.0;
        return;
    }

    const uint32_t sample_elapsed = control::elapsedMilliseconds(
        now, g_cb.update_last);
    g_cb.mah += measurement.current * sample_elapsed / 3600.0;
    g_cb.watt_h += measurement.current * measurement.voltage *
        sample_elapsed / 3600000.0;
    g_cb.update_last = now;

    // A correction is applied at most once per PID sample.  Clearing this
    // prevents a stale correction from being added repeatedly by a faster UI
    // loop when fewer than 10 ms have elapsed.
    pid_sum = 0.0;
    if (control::hasElapsed(now, pid_last, 10UL)) {
        const uint32_t pid_elapsed = control::elapsedMilliseconds(now, pid_last);
        double current_set_p = current_set_point.as_double();
        // We must limit the max wattage to IRFP250 MAX
        if (current_set_p * measurement.voltage > MAX_WATTAGE) {
            current_set_p = MAX_WATTAGE / measurement.voltage;
        }
        // calc PIDd
        e = current_set_p - measurement.current;
        if (e > -0.001 && e < 0.001) {
            e = 0.0; // dead band
        }
        p_term = e * 2289.0; // _kP
        e_sum = control::updateIntegral(e_sum, e, pid_elapsed);
        i_term = e_sum;
        d_term = (measurement.current - last_input) / pid_elapsed * 366.0;
        pid_sum = p_term + i_term - d_term;
        // double pid_sum = p_term;
        // pid_sum *= 3.5;
        pid_last = now;
        last_input = measurement.current;
    }

    int32_t set_point = (int32_t)ad5541.getValue();
    set_point += (int32_t)pid_sum;
    set_point = constrain(set_point, AD5541_CODE_LOW, AD5541_CODE_HIGH);
    ad5541.setValue((uint16_t)set_point);
}


void setup()
{
    // initialize state to idle
    g_cb.controller = control::ControllerState();

    // LCD
    lcd.init();
    lcd.home();
    lcd.print("@DC Active Load@");
    lcd.setCursor(0, 1);
    lcd.print("       20260817");
    lcd.home();

    // load set point from eeprom
    if (EEPROM.read(EEPROM_VERSION_ADDR) != EEPROM_VERSION) {
        EEPROM.write(EEPROM_VERSION_ADDR, EEPROM_VERSION);
        SaveSetPointToEEPROM();
    } else {
        const bool current_valid =
            current_set_point.load_from_eeprom(EEPROM_CURRENT_ADDR);
        const bool voltage_valid =
            voltage_set_point.load_from_eeprom(EEPROM_VOLTAGE_ADDR);
        if (!current_valid || !voltage_valid) {
            SaveSetPointToEEPROM();
        }
    }

    //
    delay(400);

    lcd.clear();

    // SPI
    SPI.begin();

    // DAC
    ad5541.begin();
    ad5541.setValue(0);

    // Cursor position
    UpdateCursorPosition();

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
    g_cb.adc_initialized = adc.init();
    delay(10);
    // FIXME: Calibration data should be gotten from EEPROM
    adc.setCalibData(AD7190_CONF_GAIN_1, 1.00080, 4.0);
    adc.setCalibData(AD7190_CONF_GAIN_8, 1.00243, -0.60);

    // Calibration failure is a latched fault.  The DAC was set to zero before
    // ADC setup and remains there until valid measurements permit an explicit
    // acknowledgement in the main loop.
    if (!g_cb.adc_initialized) {
        control::latchFault(g_cb.controller,
                            control::FaultReason::AdcFailure,
                            millis());
        ad5541.setValue(0);
    }

    // Buttons
    buttons[0].init();
    buttons[1].init();
    buttons[2].init();
    buttons[3].init();

    // FAN
    fan.init();

    // get lcd ready for using information
    lcd.clear();

    // lcd.home();
    // lcd.print("To test encoder....");
    // while (!encoder.getValue()) {

    // }
    // lcd.clear();
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
