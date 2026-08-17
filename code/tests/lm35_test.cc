#include <assert.h>
#include <cmath>
#include <cstring>
#include <stdint.h>

#include "Arduino.h"
#include "../lm35.h"

static int analog_value;
static int analog_reference;

int analogRead(int)
{
    return analog_value;
}

void analogReference(int mode)
{
    analog_reference = mode;
}

uint32_t millis()
{
    return 0;
}

int digitalRead(int)
{
    return LOW;
}

void pinMode(int, int)
{
}

void digitalWrite(int, int)
{
}

void delay(unsigned long)
{
}

static void assertNear(double actual, double expected)
{
    assert(std::fabs(actual - expected) < 1e-12);
}

static void normalReadingTests()
{
    analog_value = 100;
    analog_reference = -1;
    LM35 sensor(3, 5000.0);
    sensor.init();
    assert(analog_reference == EXTERNAL);
    assert(sensor.isValid());
    sensor.update();
    assert(sensor.isValid());
    assertNear(sensor.getTemperature(), 100.0 * 5000.0 / 1024.0 / 10.0);
}

static void initializationRailTests()
{
    analog_value = LM35_ADC_RAIL_LOW;
    LM35 ground_sensor(3, 5000.0);
    ground_sensor.init();
    assert(!ground_sensor.isValid());

    analog_value = LM35_ADC_RAIL_HIGH;
    LM35 supply_sensor(3, 5000.0);
    supply_sensor.init();
    assert(!supply_sensor.isValid());
}

static void runtimeRailTests()
{
    analog_value = 100;
    LM35 sensor(3, 5000.0);
    sensor.init();
    assert(sensor.isValid());

    analog_value = LM35_ADC_RAIL_HIGH;
    sensor.update();
    assert(sensor.isValid());
    sensor.update();
    assert(sensor.isValid());
    sensor.update();
    assert(sensor.isValid());
    sensor.update();
    assert(!sensor.isValid());

    analog_value = 100;
    sensor.update();
    assert(sensor.isValid());
}

static void rollingAverageTests()
{
    analog_value = 100;
    LM35 sensor(3, 5000.0);
    sensor.init();

    // Fill every ring slot with a known alternating pattern, then replace the
    // first slot after wraparound.  This checks the running sum against the
    // equivalent 64-sample average without inspecting private state.
    long expected_sum = 0;
    for (int i = 0; i < LM35_SAMPLES; ++i) {
        analog_value = (i % 2 == 0) ? 200 : 400;
        sensor.update();
        expected_sum += analog_value;
    }
    assertNear(sensor.getTemperature(),
               (double)expected_sum * 5000.0 / 1024.0 / 10.0 /
               LM35_SAMPLES);

    analog_value = 600;
    expected_sum -= 200;
    expected_sum += analog_value;
    sensor.update();
    assertNear(sensor.getTemperature(),
               (double)expected_sum * 5000.0 / 1024.0 / 10.0 /
               LM35_SAMPLES);
    assert(sensor.isValid());
}

int main()
{
    normalReadingTests();
    initializationRailTests();
    runtimeRailTests();
    rollingAverageTests();
    return 0;
}
