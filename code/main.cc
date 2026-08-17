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
const double CONTINUOUS_WATTAGE = 180.0;
const double MAX_INPUT_VOLTAGE = 50.0;
const double MIN_SOURCE_VOLTAGE = 0.1;

constexpr int32_t MAX_CURRENT_MILLIAMPS = 15000;
constexpr double MAX_CURRENT = MAX_CURRENT_MILLIAMPS / 1000.0;

const double MAX_TEMPERATURE = 95.0;
const double THERMAL_DERATE_START = 80.0;

const int MAX_PAGE = 4;
const uint32_t DISPLAY_UPDATE_INTERVAL_MS = 200UL;
const uint32_t WIRE_TIMEOUT_US = 1000UL;


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
    bool display_available;
    bool stop_armed;
    bool start_press_active;
    uint32_t start_pressed_ms;
    uint32_t output_last;
    uint32_t display_last;
    control::UndervoltageQualification undervoltage;

    // page
    int page;

} g_cb {
    control::ControllerState(),
    control::MeasurementSnapshot(),
    0.0, 0.0, 0, false, true, false, false, 0, 0, 0,
    control::UndervoltageQualification(), 0
};


void SetLoadOutput(uint16_t code)
{
    if (code > control::kTheoreticalDacHardCapCode) {
        code = control::kTheoreticalDacHardCapCode;
    }
    ad5541.setValue(code);
}



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


bool HandleImmediateStop();


bool UpdateCurrentVoltage()
{
    if (!g_cb.adc_initialized) {
        adc.resetCurrent();
        g_cb.measurement.current = 0.0;
        g_cb.measurement.voltage = 0.0;
        g_cb.measurement.safety_current = 0.0;
        g_cb.measurement.safety_voltage = 0.0;
        g_cb.measurement.current_valid = false;
        g_cb.measurement.voltage_valid = false;
        g_cb.measurement.safety_current_valid = false;
        g_cb.measurement.safety_voltage_valid = false;
        return true;
    }

    const bool current_valid = adc.updateCurrent();
    if (!current_valid) {
        g_cb.measurement.current = adc.readCurrent();
        g_cb.measurement.voltage = adc.readVoltage();
        g_cb.measurement.current_valid = false;
        g_cb.measurement.voltage_valid = false;
        g_cb.measurement.safety_current_valid = false;
        g_cb.measurement.safety_voltage_valid = false;
        return true;
    }

    // Do not make an intentional stop wait for the second ADC conversion.
    if (HandleImmediateStop()) {
        return false;
    }

    const bool voltage_valid = adc.updateVoltage();

    g_cb.measurement.current = adc.readCurrent();
    g_cb.measurement.voltage = adc.readVoltage();
    g_cb.measurement.safety_current = adc.readSafetyCurrent();
    g_cb.measurement.safety_voltage = adc.readSafetyVoltage();
    g_cb.measurement.current_valid = current_valid;
    g_cb.measurement.voltage_valid = voltage_valid;
    g_cb.measurement.safety_current_valid = current_valid;
    g_cb.measurement.safety_voltage_valid = voltage_valid;
    return true;
}


void UpdateTemperature()
{
    lm35.update();
}


bool UpdateSensors()
{
    // update inputs
    UpdateButtons();

    // sensors
    const bool control_processing_allowed = UpdateCurrentVoltage();
    UpdateTemperature();

    g_cb.measurement.temperature = lm35.getTemperature();
    g_cb.measurement.temperature_valid = lm35.isValid();
    g_cb.measurement.timestamp_ms = millis();
    return control_processing_allowed;
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


bool UpdateDisplay()
{
    lcd.noCursor();
    if (Wire.getWireTimeoutFlag()) {
        return false;
    }

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
            case control::FaultReason::Overvoltage:
                lcd.print(F("FAULT OVERVOLT  "));
                break;
            case control::FaultReason::Overpower:
                lcd.print(F("FAULT OVERPOWER "));
                break;
            case control::FaultReason::NoSource:
                lcd.print(F("FAULT NO SOURCE "));
                break;
            case control::FaultReason::DisplayFailure:
                lcd.print(F("FAULT DISPLAY   "));
                break;
            default:
                lcd.print(F("FAULT UNKNOWN   "));
                break;
        }
        lcd.setCursor(0, 1);
        if (g_cb.controller.fault == control::FaultReason::AdcFailure) {
            lcd.print(F("Click to retry  "));
        } else {
            lcd.print(F("Click to ack    "));
        }
        return !Wire.getWireTimeoutFlag();
    }

    if (g_cb.controller.state == control::OperationState::Completed) {
        lcd.setCursor(0, 0);
        lcd.print(F("DONE: CUTOFF    "));
        lcd.setCursor(0, 1);
        lcd.print(F("Click to finish "));
        return !Wire.getWireTimeoutFlag();
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
    if (Wire.getWireTimeoutFlag()) {
        return false;
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
    return !Wire.getWireTimeoutFlag();
}

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
    SetLoadOutput(0);
    control::stop(g_cb.controller, millis());
    control::resetUndervoltageQualification(g_cb.undervoltage);
    SaveSetPointToEEPROM();
}


bool HandleImmediateStop()
{
    if (g_cb.controller.state != control::OperationState::Running) {
        return false;
    }

    const bool encoder_pressed = digitalRead(ENCODER_SW_PIN) == LOW;
    if (!encoder_pressed) {
        g_cb.stop_armed = true;
        return false;
    }
    if (!g_cb.stop_armed) {
        return false;
    }

    StopDischarge();
    return true;
}


void LatchFault(control::FaultReason reason, uint32_t now)
{
    SetLoadOutput(0);
    control::latchFault(g_cb.controller, reason, now);
}


void CompleteDischarge(uint32_t now)
{
    SetLoadOutput(0);
    control::complete(g_cb.controller, now);
    SaveSetPointToEEPROM();
}


bool StartDischarge(uint32_t held_since_ms)
{
    uint32_t now = millis();
    if (!control::tryStart(g_cb.controller,
                           now,
                           held_since_ms,
                           true)) {
        return false;
    }

    SetLoadOutput(0);
    g_cb.mah = 0;
    g_cb.watt_h = 0;
    g_cb.update_last = now;
    g_cb.output_last = now;
    g_cb.stop_armed = false;
    g_cb.start_press_active = false;
    control::resetUndervoltageQualification(g_cb.undervoltage);
    SaveSetPointToEEPROM();
    return true;
}


bool InitializeAdc()
{
    adc.begin();
    if (!adc.detectDevice() || !adc.init()) {
        return false;
    }

    // Calibration improves operating/display accuracy only. Absolute safety
    // readings use the separate nominal schematic path in ADConverter.
    adc.setCalibData(AD7190_CONF_GAIN_1, 1.00080, 4.0);
    adc.setCalibData(AD7190_CONF_GAIN_8, 1.00243, -0.60);
    return true;
}


void ProcessControl()
{
    // All control decisions in this pass use the same sensor sample.
    const uint32_t now = g_cb.measurement.timestamp_ms;
    const control::MeasurementSnapshot& measurement = g_cb.measurement;

    // A failed/unsafe reading is handled before any user input or DAC
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

    ClickEncoder::Button encoder_btn = encoder.getButton();
    if (g_cb.controller.state == control::OperationState::Fault) {
        SetLoadOutput(0);
        if (encoder_btn == ClickEncoder::Clicked) {
            if (g_cb.controller.fault == control::FaultReason::AdcFailure) {
                g_cb.adc_initialized = InitializeAdc();
                if (g_cb.adc_initialized) {
                    control::acknowledgeFault(g_cb.controller, millis());
                }
            } else if (g_cb.controller.fault !=
                       control::FaultReason::DisplayFailure &&
                       measurement.adcValid() &&
                       measurement.temperature_valid) {
                const control::SafetyLimits recovery_limits = {
                    MAX_CURRENT * 1.1, 0.0, MAX_TEMPERATURE,
                    MAX_INPUT_VOLTAGE, MAX_WATTAGE
                };
                if (control::evaluateSafety(measurement, recovery_limits,
                                            g_cb.controller.state) ==
                    control::FaultReason::None) {
                    control::acknowledgeFault(g_cb.controller, now);
                }
            }
        }
        return;
    }

    if (g_cb.controller.state == control::OperationState::Completed) {
        SetLoadOutput(0);
        if (encoder_btn == ClickEncoder::Clicked) {
            control::acknowledgeCompleted(g_cb.controller, now);
        }
        return;
    }

    const control::SafetyLimits limits = {
        MAX_CURRENT * 1.1,
        voltage_set_point.as_double(),
        MAX_TEMPERATURE,
        MAX_INPUT_VOLTAGE,
        MAX_WATTAGE
    };
    const control::FaultReason unsafe_reason = control::evaluateSafety(
        measurement, limits, g_cb.controller.state);
    if (unsafe_reason != control::FaultReason::None) {
        LatchFault(unsafe_reason, now);
        return;
    }

    // After starting, require release before arming an immediate press-to-stop.
    // A false stop from switch bounce is safe; a delayed stop is not.
    const bool encoder_pressed = digitalRead(ENCODER_SW_PIN) == LOW;
    if (HandleImmediateStop()) {
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
        } else {
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

    // Track the physical press time directly. ClickEncoder's Held event has a
    // different duration and must not be confused with the 3 second start hold.
    const double cutoff_voltage = voltage_set_point.as_double() >
        MIN_SOURCE_VOLTAGE ? voltage_set_point.as_double() : MIN_SOURCE_VOLTAGE;
    if (g_cb.controller.state == control::OperationState::Idle) {
        if (encoder_pressed && !g_cb.start_press_active) {
            g_cb.start_press_active = true;
            g_cb.start_pressed_ms = now;
        } else if (!encoder_pressed) {
            g_cb.start_press_active = false;
        }

        if (g_cb.start_press_active &&
            control::hasElapsed(now, g_cb.start_pressed_ms,
                                control::kStartHoldMilliseconds)) {
            if (measurement.safety_voltage < MIN_SOURCE_VOLTAGE) {
                LatchFault(control::FaultReason::NoSource, now);
                return;
            }
            if (measurement.safety_voltage <= cutoff_voltage) {
                if (StartDischarge(g_cb.start_pressed_ms)) {
                    CompleteDischarge(millis());
                }
                return;
            }
            StartDischarge(g_cb.start_pressed_ms);
            return;
        }
    }

    if (g_cb.controller.state != control::OperationState::Running) {
        SetLoadOutput(0);
        return;
    }

    if (control::qualifyUndervoltage(g_cb.undervoltage,
                                     measurement.safety_voltage,
                                     measurement.safety_voltage_valid,
                                     cutoff_voltage,
                                     now)) {
        CompleteDischarge(now);
        return;
    }

    const uint32_t sample_elapsed = control::elapsedMilliseconds(
        now, g_cb.update_last);
    g_cb.mah += measurement.current * sample_elapsed / 3600.0;
    g_cb.watt_h += measurement.current * measurement.voltage *
        sample_elapsed / 3600000.0;
    g_cb.update_last = now;

    // The analog AD8629/shunt loop is the fast current servo. Firmware supplies
    // an absolute schematic-derived command, never an accumulated correction.
    double target_current = control::boundedCurrentTarget(
        current_set_point.as_double(),
        measurement.safety_voltage,
        measurement.temperature,
        CONTINUOUS_WATTAGE,
        THERMAL_DERATE_START,
        MAX_TEMPERATURE);
    if (measurement.safety_voltage < MIN_SOURCE_VOLTAGE) {
        target_current = 0.0;
    }
    const uint16_t target_code =
        control::theoreticalDacCodeForCurrent(target_current);
    const uint16_t output_code = control::slewDacCode(
        ad5541.getValue(), target_code, now, g_cb.output_last);
    g_cb.output_last = now;
    SetLoadOutput(output_code);
}


void setup()
{
    g_cb.controller = control::ControllerState();

    // Establish safe physical outputs before any UI delay or device probing.
    SPI.begin();
    ad5541.begin();
    SetLoadOutput(0);
    fan.init();
    fan.turn_on();

    // Bound every I2C transaction so a failed display cannot freeze control.
    Wire.begin();
    Wire.setWireTimeout(WIRE_TIMEOUT_US, true);
    Wire.clearWireTimeoutFlag();
    lcd.init();
    g_cb.display_available = !Wire.getWireTimeoutFlag();
    Wire.clearWireTimeoutFlag();
    if (g_cb.display_available) {
        lcd.home();
        lcd.print("@DC Active Load@");
        lcd.setCursor(0, 1);
        lcd.print("       20260817");
        lcd.home();
        if (Wire.getWireTimeoutFlag()) {
            g_cb.display_available = false;
            Wire.clearWireTimeoutFlag();
            LatchFault(control::FaultReason::DisplayFailure, millis());
        }
    }

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

    delay(400);

    // Cursor position
    UpdateCursorPosition();

    // Timer
    Timer1.initialize(1000);
    Timer1.attachInterrupt(timer_one_isr);

    // Temperature averaging runs before the fan is allowed to turn off.
    lm35.init();

    // One bounded initialization attempt enters the normal fault model instead
    // of blocking setup forever. A click on FAULT ADC retries safely.
    g_cb.adc_initialized = InitializeAdc();
    if (!g_cb.adc_initialized) {
        LatchFault(control::FaultReason::AdcFailure, millis());
    }

    // Buttons
    buttons[0].init();
    buttons[1].init();
    buttons[2].init();
    buttons[3].init();

    if (g_cb.display_available) {
        lcd.clear();
        if (Wire.getWireTimeoutFlag()) {
            g_cb.display_available = false;
            Wire.clearWireTimeoutFlag();
            LatchFault(control::FaultReason::DisplayFailure, millis());
        }
    }
    g_cb.display_last = millis() - DISPLAY_UPDATE_INTERVAL_MS;
}


void loop()
{
    if (HandleImmediateStop()) {
        return;
    }
    if (UpdateSensors()) {
        ProcessControl();
    }

    const uint32_t now = millis();
    if (g_cb.display_available &&
        control::hasElapsed(now, g_cb.display_last,
                            DISPLAY_UPDATE_INTERVAL_MS)) {
        const bool display_ok = UpdateDisplay();
        g_cb.display_last = now;
        if (!display_ok || Wire.getWireTimeoutFlag()) {
            Wire.clearWireTimeoutFlag();
            g_cb.display_available = false;
            LatchFault(control::FaultReason::DisplayFailure, millis());
        }
    }
}
