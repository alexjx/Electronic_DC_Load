#include <assert.h>

#include "../control.h"

using namespace control;

static MeasurementSnapshot validMeasurement()
{
    MeasurementSnapshot measurement = {
        5.0,   // current
        24.0,  // voltage
        25.0,  // temperature
        true,  // current_valid
        true,  // voltage_valid
        true,  // temperature_valid
        1000   // timestamp_ms
    };
    return measurement;
}

static void safetyTests()
{
    const SafetyLimits limits = {10.0, 12.0, 80.0};
    MeasurementSnapshot measurement = validMeasurement();

    measurement.current = 10.1;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::Overcurrent);

    measurement = validMeasurement();
    measurement.voltage = 11.9;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::Undervoltage);

    measurement = validMeasurement();
    measurement.temperature = 80.1;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::Overtemperature);

    measurement = validMeasurement();
    measurement.current_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::AdcFailure);

    // ADC failure has priority over all numeric limits and sensor failures.
    measurement.current = 100.0;
    measurement.voltage = 1.0;
    measurement.temperature = 100.0;
    measurement.temperature_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::AdcFailure);

    // A bad temperature sensor has priority over numeric electrical limits.
    measurement = validMeasurement();
    measurement.current = 100.0;
    measurement.voltage = 1.0;
    measurement.temperature = 100.0;
    measurement.temperature_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::TemperatureSensorFailure);

    measurement = validMeasurement();
    measurement.voltage_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::AdcFailure);

    measurement = validMeasurement();
    measurement.temperature_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::TemperatureSensorFailure);

    // ADC failure wins over a simultaneous invalid temperature reading.
    measurement.current_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::AdcFailure);

    // Overcurrent and thermal limits remain active while idle so an output or
    // sensor failure cannot be hidden merely by leaving the running state.
    measurement = validMeasurement();
    measurement.current = 100.0;
    assert(evaluateSafety(measurement, limits, OperationState::Idle) ==
           FaultReason::Overcurrent);
    measurement = validMeasurement();
    measurement.temperature = 100.0;
    assert(evaluateSafety(measurement, limits, OperationState::Idle) ==
           FaultReason::Overtemperature);

    // Cutoff voltage applies only to an intentional discharge session.
    measurement = validMeasurement();
    measurement.voltage = 1.0;
    assert(evaluateSafety(measurement, limits, OperationState::Idle) ==
           FaultReason::None);

    // Invalid measurements are unsafe even before a session starts.
    measurement.current_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Idle) ==
           FaultReason::AdcFailure);
    measurement = validMeasurement();
    measurement.temperature_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Idle) ==
           FaultReason::TemperatureSensorFailure);
}

static void stateAndTimingTests()
{
    ControllerState controller;
    assert(controller.state == OperationState::Idle);
    assert(controller.fault == FaultReason::None);

    assert(!startHoldSatisfied(2999, 0, true));
    assert(startHoldSatisfied(3000, 0, true));
    assert(!startHoldSatisfied(3000, 0, false));

    // The hold predicate and start guard work across millis() rollover.
    const uint32_t hold_started = 0xfffffF00UL;
    assert(!startHoldSatisfied(0x00000aB7UL, hold_started, true));
    assert(startHoldSatisfied(0x00000aB8UL, hold_started, true));
    assert(elapsedMilliseconds(0x00000aB8UL, hold_started) == 3000UL);

    assert(tryStart(controller, 3000, 0, true));
    assert(controller.state == OperationState::Running);
    assert(!tryStart(controller, 6000, 0, true));

    assert(latchFault(controller, FaultReason::Overcurrent, 7000));
    assert(controller.state == OperationState::Fault);
    assert(controller.fault == FaultReason::Overcurrent);
    // Latching retains the original reason.
    assert(!latchFault(controller, FaultReason::Overtemperature, 8000));
    assert(controller.fault == FaultReason::Overcurrent);
    assert(!tryStart(controller, 10000, 0, true));

    assert(acknowledgeFault(controller, 9000));
    assert(controller.state == OperationState::Idle);
    assert(controller.fault == FaultReason::None);
    assert(!acknowledgeFault(controller, 10000));
    assert(tryStart(controller, 12000, 9000, true));
}

static void integralTests()
{
    assert(updateIntegral(0.0, 100.0, 1000) == kIntegralBound);
    assert(updateIntegral(0.0, -100.0, 1000) == -kIntegralBound);
    assert(updateIntegral(kIntegralBound, 1.0, 1000) == kIntegralBound);
    assert(updateIntegral(-kIntegralBound, -1.0, 1000) == -kIntegralBound);
    assert(updateIntegral(14.0, 1.0, 1000) > 14.0);
    assert(updateIntegral(-14.0, -1.0, 1000) < -14.0);
    assert(updateIntegral(7.0, 100.0, 0) == 7.0);
}

int main()
{
    safetyTests();
    stateAndTimingTests();
    integralTests();
    return 0;
}
