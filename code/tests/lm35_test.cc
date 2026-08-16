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

int main()
{
    normalReadingTests();
    initializationRailTests();
    runtimeRailTests();
    return 0;
}
