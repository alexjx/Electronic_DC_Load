#include <assert.h>
#include <math.h>

#include "../control.h"

using namespace control;

static bool near(double actual, double expected, double tolerance)
{
    return fabs(actual - expected) <= tolerance;
}

static MeasurementSnapshot validMeasurement()
{
    MeasurementSnapshot measurement = {
        5.0,   // calibrated operating current
        24.0,  // calibrated operating voltage
        25.0,  // temperature
        true,  // current_valid
        true,  // voltage_valid
        true,  // temperature_valid
        1000,  // timestamp_ms
        5.0,   // theoretical safety current
        24.0,  // theoretical safety voltage
        true,  // safety_current_valid
        true   // safety_voltage_valid
    };
    return measurement;
}

static void conversionTests()
{
    assert(near(theoreticalCurrentFromSenseVoltage(0.25), 1.0, 1e-12));
    assert(near(theoreticalVoltageFromDivider(1.0), 10.09, 1e-12));

    assert(theoreticalDacCodeForCurrent(0.0) == 0U);
    assert(theoreticalDacCodeForCurrent(-1.0) == 0U);
    assert(theoreticalDacCodeForCurrent(1.0) == 321U);
    assert(theoreticalDacCodeForCurrent(15.0) ==
           kTheoreticalDacHardCapCode);
    assert(theoreticalDacCodeForCurrent(100.0) ==
           kTheoreticalDacHardCapCode);
    assert(theoreticalCurrentFromDacCode(kTheoreticalDacHardCapCode) <
           15.01);
    assert(theoreticalCurrentFromDacCode(
               theoreticalDacCodeForCurrent(1.0)) < 1.01);
}

static void slewTests()
{
    const uint16_t target = theoreticalDacCodeForCurrent(15.0);
    const uint16_t one_second = slewDacCode(0U, target, 1000U, 0U);
    const uint16_t half_second = slewDacCode(0U, target, 500U, 0U);

    // Upward slew uses at most 100 ms of elapsed time after a scheduler stall;
    // reductions are immediate so a lower safety target is never delayed.
    assert(one_second == 160U);
    assert(half_second == 160U);
    assert(slewDacCode(target, 0U, 1000U, 0U) == 0U);
    assert(slewDacCode(target, 0U, 1000U, 0U, 0.0) == 0U);
    assert(slewDacCode(0U, 65535U, 1000U, 0U) == one_second);
    assert(slewDacCode(100U, 100U, 1000U, 0U) == 100U);

    // uint32_t subtraction makes the rate rollover-safe.
    const uint32_t then_ms = 0xfffffff0UL;
    const uint32_t now_ms = 0x000003d8UL; // 1000 ms later
    assert(slewDacCode(0U, target, now_ms, then_ms) == one_second);
}

static void safetyTests()
{
    const SafetyLimits limits = {10.0, 12.0, 80.0, 60.0, 200.0};
    MeasurementSnapshot measurement = validMeasurement();

    measurement.safety_current = 10.1;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::Overcurrent);

    measurement = validMeasurement();
    measurement.safety_voltage = 60.1;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::Overvoltage);

    measurement = validMeasurement();
    measurement.safety_current = 9.0;
    measurement.safety_voltage = 24.0;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::Overpower);

    measurement = validMeasurement();
    measurement.temperature = 80.1;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::Overtemperature);

    // Operating/display calibration cannot weaken safety limits.
    measurement = validMeasurement();
    measurement.current = 100.0;
    measurement.voltage = 1000.0;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::None);
    measurement.safety_current = 10.1;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::Overcurrent);

    measurement = validMeasurement();
    measurement.safety_current_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::AdcFailure);
    measurement = validMeasurement();
    measurement.safety_voltage_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::AdcFailure);

    measurement = validMeasurement();
    measurement.temperature_valid = false;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::TemperatureSensorFailure);

    // Undervoltage is qualified separately and is not an electrical fault.
    measurement = validMeasurement();
    measurement.safety_voltage = 11.9;
    assert(evaluateSafety(measurement, limits, OperationState::Running) ==
           FaultReason::None);
}

static void cutoffTests()
{
    UndervoltageQualification cutoff;
    assert(!qualifyUndervoltage(cutoff, 12.0, true, 12.0, 99U));
    assert(qualifyUndervoltage(cutoff, 12.0, true, 12.0, 599U));

    resetUndervoltageQualification(cutoff);
    assert(!qualifyUndervoltage(cutoff, 11.9, true, 12.0, 100U));
    assert(!qualifyUndervoltage(cutoff, 11.9, true, 12.0, 599U));
    assert(qualifyUndervoltage(cutoff, 11.9, true, 12.0, 600U));
    assert(cutoff.completed);

    resetUndervoltageQualification(cutoff);
    assert(!qualifyUndervoltage(cutoff, 12.05, true, 12.0, 100U));
    assert(!qualifyUndervoltage(cutoff, 11.9, true, 12.0, 200U));
    // A reading inside the hysteresis band does not reset an active timer.
    assert(!qualifyUndervoltage(cutoff, 12.05, true, 12.0, 500U));
    assert(qualifyUndervoltage(cutoff, 12.05, true, 12.0, 700U));

    resetUndervoltageQualification(cutoff);
    assert(!qualifyUndervoltage(cutoff, 11.9, true, 12.0, 100U));
    assert(!qualifyUndervoltage(cutoff, 12.11, true, 12.0, 200U));
    assert(!qualifyUndervoltage(cutoff, 11.9, true, 12.0, 500U));

    // Invalid samples cannot complete a cutoff and reset the debounce timer.
    resetUndervoltageQualification(cutoff);
    assert(!qualifyUndervoltage(cutoff, 11.9, true, 12.0, 100U));
    assert(!qualifyUndervoltage(cutoff, 0.0, false, 12.0, 600U));
    assert(!qualifyUndervoltage(cutoff, 11.9, true, 12.0, 1000U));

    // The same qualification works across millis() rollover.
    resetUndervoltageQualification(cutoff);
    const uint32_t then_ms = 0xfffffff0UL;
    assert(!qualifyUndervoltage(cutoff, 11.9, true, 12.0, then_ms));
    assert(qualifyUndervoltage(cutoff, 11.9, true, 12.0, 0x000001e4UL));
}

static void stateAndTimingTests()
{
    ControllerState controller;
    assert(controller.state == OperationState::Idle);
    assert(controller.fault == FaultReason::None);

    assert(!startHoldSatisfied(2999, 0, true));
    assert(startHoldSatisfied(3000, 0, true));
    assert(!startHoldSatisfied(3000, 0, false));

    const uint32_t hold_started = 0xfffffF00UL;
    assert(!startHoldSatisfied(0x00000aB7UL, hold_started, true));
    assert(startHoldSatisfied(0x00000aB8UL, hold_started, true));
    assert(elapsedMilliseconds(0x00000aB8UL, hold_started) == 3000UL);

    assert(tryStart(controller, 3000, 0, true));
    assert(controller.state == OperationState::Running);
    assert(!tryStart(controller, 6000, 0, true));
    assert(complete(controller, 7000));
    assert(controller.state == OperationState::Completed);
    assert(!complete(controller, 8000));
    assert(acknowledgeCompleted(controller, 9000));
    assert(controller.state == OperationState::Idle);

    assert(tryStart(controller, 12000, 9000, true));
    assert(latchFault(controller, FaultReason::Overcurrent, 13000));
    assert(controller.state == OperationState::Fault);
    assert(controller.fault == FaultReason::Overcurrent);
    assert(!latchFault(controller, FaultReason::Overtemperature, 14000));
    assert(controller.fault == FaultReason::Overcurrent);
    assert(!tryStart(controller, 15000, 12000, true));
    assert(acknowledgeFault(controller, 16000));
    assert(controller.state == OperationState::Idle);
}

static void targetLimitTests()
{
    const uint16_t high = theoreticalDacCodeForCurrent(10.0);
    const uint16_t low = theoreticalDacCodeForCurrent(2.0);
    assert(slewDacCode(high, low, 1000, 0) == low);

    // A long scheduler stall is capped to a 100 ms upward slew interval.
    assert(slewDacCode(0, high, 10000, 0) ==
           slewDacCode(0, high, 100, 0));

    assert(boundedCurrentTarget(15.0, 20.0, 25.0, 180.0, 80.0, 95.0) == 9.0);
    assert(boundedCurrentTarget(15.0, 10.0, 25.0, 180.0, 80.0, 95.0) == 15.0);
    assert(boundedCurrentTarget(15.0, 10.0, 87.5, 180.0, 80.0, 95.0) == 7.5);
    assert(boundedCurrentTarget(15.0, 10.0, 95.0, 180.0, 80.0, 95.0) == 0.0);
    assert(boundedCurrentTarget(15.0, 0.0, 25.0, 180.0, 80.0, 95.0) == 0.0);
}

int main()
{
    conversionTests();
    slewTests();
    safetyTests();
    cutoffTests();
    stateAndTimingTests();
    targetLimitTests();
    return 0;
}
