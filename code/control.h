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
    Calibration
};

enum class FaultReason : uint8_t {
    None = 0,
    AdcFailure,
    TemperatureSensorFailure,
    Overcurrent,
    Undervoltage,
    Overtemperature
};

struct MeasurementSnapshot {
    double current;
    double voltage;
    double temperature;
    bool current_valid;
    bool voltage_valid;
    bool temperature_valid;
    uint32_t timestamp_ms;

    bool adcValid() const
    {
        return current_valid && voltage_valid;
    }
};

struct SafetyLimits {
    double max_current;
    double min_voltage;
    double max_temperature;
};

// Checks are ordered deliberately: an ADC failure, then a temperature sensor
// failure, then electrical limits, then the thermal limit.  Limits are only
// evaluated while running; invalid measurements are unsafe in every state.
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
    if (measurement.current > limits.max_current) {
        return FaultReason::Overcurrent;
    }
    if (measurement.temperature > limits.max_temperature) {
        return FaultReason::Overtemperature;
    }
    // The cutoff voltage is meaningful only during an intentional discharge.
    if (state == OperationState::Running &&
        measurement.voltage < limits.min_voltage) {
        return FaultReason::Undervoltage;
    }
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

static const uint32_t kStartHoldMilliseconds = 3000UL;

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

static const double kIntegralBound = 15.0;
static const double kHistoricalIntegralGainPerMillisecond = 0.00036;

// The historical controller used 0.00036 * error * elapsed_ms.  Clamping the
// candidate on every update prevents integral windup while preserving that
// gain and the existing +/-15 operating bound.
inline double updateIntegral(double integral,
                             double error,
                             uint32_t elapsed_ms)
{
    const double candidate = integral +
        kHistoricalIntegralGainPerMillisecond * error * elapsed_ms;
    if (candidate > kIntegralBound) {
        return kIntegralBound;
    }
    if (candidate < -kIntegralBound) {
        return -kIntegralBound;
    }
    return candidate;
}

} // namespace control

#endif // ELECTRONIC_DC_LOAD_CONTROL_H
