#include <assert.h>
#include <cstddef>
#include <stdint.h>

#include "Arduino.h"
#include "SPI.h"
#include "../ad7190.h"

SPIClass SPI;
static uint32_t clock_ms;

int analogRead(int)
{
    return 0;
}

void analogReference(int)
{
}

uint32_t millis()
{
    return clock_ms++;
}

int digitalRead(int)
{
    return HIGH;
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

static void readyTimeoutTest()
{
    AD7190 adc(8, 12);
    uint32_t value = 1234;
    clock_ms = 0;
    assert(!adc.readDataRegister(value));
    assert(value == 1234);
    assert(adc.status() == AD7190_STATUS_TIMEOUT);
}

static void resetTransferTest()
{
    AD7190 adc(8, 12);
    SPI.transfers.clear();
    adc.reset();
    assert(SPI.transfers.size() == 1);
    assert(SPI.transfers[0].size() == 6);
    assert(SPI.transfers[0][0] == 0x01);
    for (size_t index = 1; index < SPI.transfers[0].size(); ++index) {
        assert(SPI.transfers[0][index] == 0xff);
    }
}

int main()
{
    readyTimeoutTest();
    resetTransferTest();
    return 0;
}
