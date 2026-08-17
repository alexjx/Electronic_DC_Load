#ifndef ELECTRONIC_DC_LOAD_CONTROL_H
#define ELECTRONIC_DC_LOAD_CONTROL_H

// The control model deliberately has no Arduino dependencies.  Hardware code
// can copy its readings into a MeasurementSnapshot and act on the resulting
// state without making the safety decisions themselves.

#include <stdint.h>

namespace control {

enum class OperationState : uint8_t {
    Idle = 0,
    Running,
    Fault,
    Calibration,
    Completed
};

enum class FaultReason : uint8_t {
    None = 0,
    AdcFailure,
    TemperatureSensorFailure,
    Overcurrent,
    Undervoltage,
    Overtemperature,
    Overvoltage,
    Overpower,
    NoSource,
    DisplayFailure
};

struct MeasurementSnapshot {
    double current;
    double voltage;
    double temperature;
    bool current_valid;
    bool voltage_valid;
    bool temperature_valid;
    uint32_t timestamp_ms;

    // These are deliberately separate from current/voltage.  The latter may
    // contain user calibration for display and control; safety_current and
    // safety_voltage must come from the nominal schematic transfer functions.
    double safety_current;
    double safety_voltage;
    bool safety_current_valid;
    bool safety_voltage_valid;

    bool adcValid() const
    {
        return safety_current_valid && safety_voltage_valid;
    }
};

struct SafetyLimits {
    double max_current;
    double min_voltage;
    double max_temperature;
    double max_voltage;
    double max_power;
};

// Checks are ordered deliberately: an ADC failure, then a temperature sensor
// failure, then electrical limits, then the thermal limit.  Invalid safety
// measurements are unsafe in every state.  Undervoltage is intentionally not
// a fault: a qualified low-voltage cutoff is a normal Completed state.
inline FaultReason evaluateSafety(const MeasurementSnapshot& measurement,
                                  const SafetyLimits& limits,
                                  OperationState state)
{
    if (!measurement.adcValid()) {
        return FaultReason::AdcFailure;
    }
    if (!measurement.temperature_valid) {
        return FaultReason::TemperatureSensorFailure;
    }
    if (measurement.safety_current > limits.max_current) {
        return FaultReason::Overcurrent;
    }
    if (measurement.safety_voltage > limits.max_voltage &&
        limits.max_voltage > 0.0) {
        return FaultReason::Overvoltage;
    }
    if (measurement.safety_current * measurement.safety_voltage >
            limits.max_power &&
        limits.max_power > 0.0) {
        return FaultReason::Overpower;
    }
    if (measurement.temperature > limits.max_temperature) {
        return FaultReason::Overtemperature;
    }
    (void)state;
    return FaultReason::None;
}

// Unsigned subtraction gives the intended result across a uint32_t millis()
// rollover, provided an interval is shorter than one full timer cycle.
inline uint32_t elapsedMilliseconds(uint32_t now_ms, uint32_t then_ms)
{
    return static_cast<uint32_t>(now_ms - then_ms);
}

inline bool hasElapsed(uint32_t now_ms,
                       uint32_t then_ms,
                       uint32_t interval_ms)
{
    return elapsedMilliseconds(now_ms, then_ms) >= interval_ms;
}

// Schematic-derived transfer functions.  These are nominal safety values and
// must not be replaced by calibration when checking absolute limits.
static const double kDacReferenceVolts = 5.0;
static const double kDacDividerTopOhms = 39000.0;
static const double kDacDividerBottomOhms = 10000.0;
static const double kCurrentShuntOhms = 0.005;
static const double kCurrentSenseGain = 50.0; // INA213
// The design files say 9.9 kOhm, but the purchase record for the assembled
// board says R5 is 9.09 kOhm. Fitted-part evidence takes precedence.
static const double kInputDividerRatio = 10.09; // 9.09 kOhm / 1 kOhm
static const double kMaximumTheoreticalCurrentAmps = 15.0;
static const uint16_t kTheoreticalDacHardCapCode = 4817U;

inline double theoreticalCurrentFromSenseVoltage(double sense_voltage)
{
    return sense_voltage / (kCurrentSenseGain * kCurrentShuntOhms);
}

inline double theoreticalVoltageFromDivider(double divider_voltage)
{
    return divider_voltage * kInputDividerRatio;
}

inline double theoreticalCurrentFromDacCode(uint16_t dac_code)
{
    const double dac_voltage =
        static_cast<double>(dac_code) * kDacReferenceVolts / 65536.0;
    const double shunt_voltage = dac_voltage *
        kDacDividerBottomOhms /
        (kDacDividerTopOhms + kDacDividerBottomOhms);
    return shunt_voltage / kCurrentShuntOhms;
}

inline uint16_t theoreticalDacCodeForCurrent(double current_amps)
{
    if (current_amps <= 0.0) {
        return 0U;
    }
    if (current_amps >= kMaximumTheoreticalCurrentAmps) {
        return kTheoreticalDacHardCapCode;
    }

    const double dac_voltage = current_amps * kCurrentShuntOhms *
        (kDacDividerTopOhms + kDacDividerBottomOhms) /
        kDacDividerBottomOhms;
    const double code = dac_voltage * 65536.0 / kDacReferenceVolts;
    // Round nominal values to the nearest DAC code, then enforce the
    // immutable 15 A ceiling independently of any calibration.
    const uint32_t rounded = static_cast<uint32_t>(code + 0.5);
    return rounded > kTheoreticalDacHardCapCode ?
        kTheoreticalDacHardCapCode : static_cast<uint16_t>(rounded);
}

// Conservative default: no more than 5 A/s in either direction.  The code
// rate is derived from the same schematic transfer function as the command.
static const double kDefaultSlewRateAmpsPerSecond = 5.0;

inline uint16_t slewDacCode(uint16_t current_code,
                            uint16_t target_code,
                            uint32_t now_ms,
                            uint32_t then_ms,
                            double rate_amps_per_second =
                                kDefaultSlewRateAmpsPerSecond)
{
    if (target_code > kTheoreticalDacHardCapCode) {
        target_code = kTheoreticalDacHardCapCode;
    }
    if (current_code == target_code) {
        return current_code;
    }

    // A delayed main loop must never turn one stale interval into a large
    // upward output step. Downward changes are always applied immediately.
    if (target_code < current_code) {
        return target_code;
    }
    if (rate_amps_per_second <= 0.0) {
        return current_code;
    }

    uint32_t elapsed_ms = elapsedMilliseconds(now_ms, then_ms);
    if (elapsed_ms > 100UL) {
        elapsed_ms = 100UL;
    }
    // Codes per amp is the inverse of the nominal current-per-code transfer.
    const double codes_per_amp = kDacReferenceVolts == 0.0 ? 0.0 :
        (kDacDividerTopOhms + kDacDividerBottomOhms) *
        kCurrentShuntOhms * 65536.0 /
        (kDacDividerBottomOhms * kDacReferenceVolts);
    const double allowed_codes = rate_amps_per_second *
        (static_cast<double>(elapsed_ms) / 1000.0) * codes_per_amp;
    uint32_t step = static_cast<uint32_t>(allowed_codes);
    if (step == 0U) {
        return current_code;
    }
    const uint32_t difference = target_code - current_code;
    return static_cast<uint16_t>(current_code +
        (difference < step ? difference : step));
}

inline double boundedCurrentTarget(double requested_current,
                                   double safety_voltage,
                                   double temperature,
                                   double continuous_power_limit,
                                   double thermal_derate_start,
                                   double maximum_temperature)
{
    if (requested_current <= 0.0 || safety_voltage <= 0.0 ||
        temperature >= maximum_temperature) {
        return 0.0;
    }

    double target = requested_current > kMaximumTheoreticalCurrentAmps ?
        kMaximumTheoreticalCurrentAmps : requested_current;
    if (continuous_power_limit > 0.0 &&
        target * safety_voltage > continuous_power_limit) {
        target = continuous_power_limit / safety_voltage;
    }
    if (temperature > thermal_derate_start &&
        maximum_temperature > thermal_derate_start) {
        target *= (maximum_temperature - temperature) /
            (maximum_temperature - thermal_derate_start);
    }
    return target < 0.0 ? 0.0 : target;
}

static const uint32_t kStartHoldMilliseconds = 3000UL;
static const uint32_t kUndervoltageDebounceMilliseconds = 500UL;
static const double kUndervoltageHysteresisVolts = 0.1;

// `held_since_ms` must be reset by the input adapter when the button is
// released.  This leaves button/interrupt handling outside this pure model.
inline bool startHoldSatisfied(uint32_t now_ms,
                               uint32_t held_since_ms,
                               bool held,
                               uint32_t required_ms = kStartHoldMilliseconds)
{
    return held && hasElapsed(now_ms, held_since_ms, required_ms);
}

struct ControllerState {
    OperationState state;
    FaultReason fault;
    uint32_t state_since_ms;

    ControllerState()
        : state(OperationState::Idle),
          fault(FaultReason::None),
          state_since_ms(0)
    {
    }
};

struct UndervoltageQualification {
    bool below_cutoff;
    bool completed;
    uint32_t below_since_ms;

    UndervoltageQualification()
        : below_cutoff(false), completed(false), below_since_ms(0)
    {
    }
};

inline void resetUndervoltageQualification(
    UndervoltageQualification& qualification)
{
    qualification.below_cutoff = false;
    qualification.completed = false;
    qualification.below_since_ms = 0;
}

// A low reading must persist for 500 ms before completing a discharge.  Once
// the timer has started, readings inside the 0.1 V hysteresis band do not
// restart it; a clear rise above the band does.
inline bool qualifyUndervoltage(UndervoltageQualification& qualification,
                                double voltage,
                                bool voltage_valid,
                                double cutoff_voltage,
                                uint32_t now_ms,
                                uint32_t debounce_ms =
                                    kUndervoltageDebounceMilliseconds,
                                double hysteresis_volts =
                                    kUndervoltageHysteresisVolts)
{
    if (qualification.completed) {
        return true;
    }
    if (!voltage_valid) {
        qualification.below_cutoff = false;
        return false;
    }
    if (voltage > cutoff_voltage + hysteresis_volts) {
        qualification.below_cutoff = false;
        return false;
    }
    if (voltage <= cutoff_voltage && !qualification.below_cutoff) {
        qualification.below_cutoff = true;
        qualification.below_since_ms = now_ms;
    }
    if (qualification.below_cutoff &&
        hasElapsed(now_ms, qualification.below_since_ms, debounce_ms)) {
        qualification.completed = true;
    }
    return qualification.completed;
}

// The first fault is retained until an explicit acknowledgement.  Repeated
// sensor failures cannot replace the reason that originally stopped the load.
inline bool latchFault(ControllerState& controller,
                       FaultReason reason,
                       uint32_t now_ms)
{
    if (reason == FaultReason::None) {
        return false;
    }
    if (controller.state == OperationState::Fault) {
        if (controller.fault == FaultReason::None) {
            controller.fault = reason;
        }
        return false;
    }
    controller.state = OperationState::Fault;
    controller.fault = reason;
    controller.state_since_ms = now_ms;
    return true;
}

// A fault cannot silently become idle.  This is the only transition out of
// Fault, and it clears the latched reason for the next session.
inline bool acknowledgeFault(ControllerState& controller, uint32_t now_ms)
{
    if (controller.state != OperationState::Fault) {
        return false;
    }
    controller.state = OperationState::Idle;
    controller.fault = FaultReason::None;
    controller.state_since_ms = now_ms;
    return true;
}

inline bool complete(ControllerState& controller, uint32_t now_ms)
{
    if (controller.state != OperationState::Running) {
        return false;
    }
    controller.state = OperationState::Completed;
    controller.fault = FaultReason::None;
    controller.state_since_ms = now_ms;
    return true;
}

inline bool acknowledgeCompleted(ControllerState& controller,
                                 uint32_t now_ms)
{
    if (controller.state != OperationState::Completed) {
        return false;
    }
    controller.state = OperationState::Idle;
    controller.fault = FaultReason::None;
    controller.state_since_ms = now_ms;
    return true;
}

inline bool canStart(const ControllerState& controller,
                    uint32_t now_ms,
                    uint32_t held_since_ms,
                    bool held)
{
    return controller.state == OperationState::Idle &&
           startHoldSatisfied(now_ms, held_since_ms, held);
}

// Starting is intentionally exposed as a guarded operation rather than a
// setter, so callers cannot start a latched fault accidentally.
inline bool tryStart(ControllerState& controller,
                     uint32_t now_ms,
                     uint32_t held_since_ms,
                     bool held)
{
    if (!canStart(controller, now_ms, held_since_ms, held)) {
        return false;
    }
    controller.state = OperationState::Running;
    controller.fault = FaultReason::None;
    controller.state_since_ms = now_ms;
    return true;
}

inline bool stop(ControllerState& controller, uint32_t now_ms)
{
    if (controller.state != OperationState::Running) {
        return false;
    }
    controller.state = OperationState::Idle;
    controller.fault = FaultReason::None;
    controller.state_since_ms = now_ms;
    return true;
}

} // namespace control

#endif // ELECTRONIC_DC_LOAD_CONTROL_H
