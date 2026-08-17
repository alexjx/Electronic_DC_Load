#include <assert.h>
#include <cstddef>
#include <stdint.h>

#include "Arduino.h"
#include "SPI.h"
#include "../ad7190.h"

SPIClass SPI;
static uint32_t clock_ms;
static int ready_level = HIGH;

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
    return ready_level;
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
    ready_level = HIGH;
    assert(!adc.readDataRegister(value));
    assert(value == 1234);
    assert(adc.status() == AD7190_STATUS_TIMEOUT);
}

static void statusValidationTest()
{
    AD7190 adc(8, 12);
    uint32_t value = 0xabcdef;
    ready_level = LOW;

    /* Enable status transmission. The first response is the mode register
     * read performed by configDataStatus; the write has no response. */
    SPI.responses.clear();
    SPI.responses.push_back(std::vector<uint8_t>(4, 0));
    adc.configDataStatus(1);

    /* Conversion value 0x123456, followed by AD7190 ERR. */
    SPI.responses.push_back(std::vector<uint8_t>{0, 0x12, 0x34, 0x56,
                                                  AD7190_STAT_ERR});
    assert(!adc.readDataRegister(value, AD7190_CH_AIN1P_AINCOM));
    assert(value == 0xabcdef);
    assert(adc.status() == AD7190_STATUS_DATA_ERROR);

    /* NOREF is independently rejected when reference detection is enabled. */
    SPI.responses.push_back(std::vector<uint8_t>{0, 0x12, 0x34, 0x56,
                                                  AD7190_STAT_NOREF});
    assert(!adc.readDataRegister(value, AD7190_CH_AIN1P_AINCOM));
    assert(adc.status() == AD7190_STATUS_NO_REFERENCE);

    /* A conversion from another enabled channel must not be accepted. */
    SPI.responses.push_back(std::vector<uint8_t>{0, 0x12, 0x34, 0x56,
                                                  AD7190_STAT_CH(AD7190_CH_AIN2P_AINCOM)});
    assert(!adc.readDataRegister(value, AD7190_CH_AIN1P_AINCOM));
    assert(adc.status() == AD7190_STATUS_WRONG_CHANNEL);

    SPI.responses.push_back(std::vector<uint8_t>{0, 0x12, 0x34, 0x56,
                                                  AD7190_STAT_CH(AD7190_CH_AIN1P_AINCOM)});
    assert(adc.readDataRegister(value, AD7190_CH_AIN1P_AINCOM));
    assert(value == 0x123456);
    assert(adc.status() == AD7190_STATUS_OK);
}

static void referenceDetectionTest()
{
    AD7190 adc(8, 12);
    SPI.transfers.clear();
    SPI.responses.clear();
    /* Config read returns zero, then config write must set REFDET in the
     * low byte of the 24-bit configuration register. */
    SPI.responses.push_back(std::vector<uint8_t>(4, 0));
    adc.configReferenceDetection(1);
    assert(SPI.transfers.size() == 2);
    assert((SPI.transfers[1][3] & AD7190_CONF_REFDET) != 0);
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
    statusValidationTest();
    referenceDetectionTest();
    resetTransferTest();
    return 0;
}
