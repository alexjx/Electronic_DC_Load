#ifndef __AD7190_H__
#define __AD7190_H__

/******************************************************************************/
/******************************** AD7190 **************************************/
/******************************************************************************/

/* AD7190 Register Map */
#define AD7190_REG_COMM         0 // Communications Register (WO, 8-bit)
#define AD7190_REG_STAT         0 // Status Register         (RO, 8-bit)
#define AD7190_REG_MODE         1 // Mode Register           (RW, 24-bit
#define AD7190_REG_CONF         2 // Configuration Register  (RW, 24-bit)
#define AD7190_REG_DATA         3 // Data Register           (RO, 24/32-bit)
#define AD7190_REG_ID           4 // ID Register             (RO, 8-bit)
#define AD7190_REG_GPOCON       5 // GPOCON Register         (RW, 8-bit)
#define AD7190_REG_OFFSET       6 // Offset Register         (RW, 24-bit
#define AD7190_REG_FULLSCALE    7 // Full-Scale Register     (RW, 24-bit)

/* Communications Register Bit Designations (AD7190_REG_COMM) */
#define AD7190_COMM_WEN         (1ul << 7)              // Write Enable.
#define AD7190_COMM_WRITE       (0 << 6)                // Write Operation.
#define AD7190_COMM_READ        (1ul << 6)              // Read Operation.
#define AD7190_COMM_ADDR(x)     (((x) & 0x7) << 3)      // Register Address.
#define AD7190_COMM_CREAD       (1ul << 2)              // Continuous Read of Data Register.

/* Status Register Bit Designations (AD7190_REG_STAT) */
#define AD7190_STAT_RDY         (1ul << 7)      // Ready.
#define AD7190_STAT_ERR         (1ul << 6)      // ADC error bit.
#define AD7190_STAT_NOREF       (1ul << 5)      // Error no external reference.
#define AD7190_STAT_PARITY      (1ul << 4)      // Parity check of the data register.
#define AD7190_STAT_CH(x)       ((x) & 0b111)   // Channel Mask


/* Mode Register Bit Designations (AD7190_REG_MODE) */
#define AD7190_MODE_SEL(x)      (((x) & 0x7ul) << 21)     // Operation Mode Select.
#define AD7190_MODE_DAT_STA     (1ul << 20)               // Status Register transmission.
#define AD7190_MODE_CLKSRC(x)   (((x) & 0x3ul) << 18)     // Clock Source Select.
#define AD7190_MODE_CLKMSK(x)   ((x) & (0x3ul << 18))
#define AD7190_MODE_SINC3       (1ul << 15)               // SINC3 Filter Select.
#define AD7190_MODE_ENPAR       (1ul << 13)               // Parity Enable.
#define AD7190_MODE_SCYCLE      (1ul << 11)               // Single cycle conversion.
#define AD7190_MODE_REJ60       (1ul << 10)               // 50/60Hz notch filter.
#define AD7190_MODE_RATE(x)     ((x) & 0x3FF)             // Filter Update Rate Select.

/* Mode Register: AD7190_MODE_SEL(x) options */
#define AD7190_MODE_CONT                0ul // Continuous Conversion Mode.
#define AD7190_MODE_SINGLE              1ul // Single Conversion Mode.
#define AD7190_MODE_IDLE                2ul // Idle Mode.
#define AD7190_MODE_PWRDN               3ul // Power-Down Mode.
#define AD7190_MODE_CAL_INT_ZERO        4ul // Internal Zero-Scale Calibration.
#define AD7190_MODE_CAL_INT_FULL        5ul // Internal Full-Scale Calibration.
#define AD7190_MODE_CAL_SYS_ZERO        6ul // System Zero-Scale Calibration.
#define AD7190_MODE_CAL_SYS_FULL        7ul // System Full-Scale Calibration.

/* Mode Register: AD7190_MODE_CLKSRC(x) options */
#define AD7190_CLK_EXT_MCLK1_2          0ul     // External crystal. The external crystal
                                                // is connected from MCLK1 to MCLK2.
#define AD7190_CLK_EXT_MCLK2            1ul     // External Clock applied to MCLK2
#define AD7190_CLK_INT                  2ul     // Internal 4.92 MHz clock.
                                                // Pin MCLK2 is tristated.
#define AD7190_CLK_INT_CO               3ul     // Internal 4.92 MHz clock. The internal
                                                // clock is available on MCLK2.

/* Configuration Register Bit Designations (AD7190_REG_CONF) */
#define AD7190_CONF_CHOP        (1ul << 23)                     // CHOP enable.
#define AD7190_CONF_REFSEL      (1ul << 20)                     // REFIN1/REFIN2 Reference Select.
#define AD7190_CONF_CHAN(x)     ((1ul << ((x) & 0xFF)) << 8)    // Channel select.
#define AD7190_CONF_BURN        (1ul << 7)                      // Burnout current enable.
#define AD7190_CONF_REFDET      (1ul << 6)                      // Reference detect enable.
#define AD7190_CONF_BUF         (1ul << 4)                      // Buffered Mode Enable.
#define AD7190_CONF_UNIPOLAR    (1ul << 3)                      // Unipolar/Bipolar Enable.
#define AD7190_CONF_GAIN(x)     ((x) & 0x7)                     // Gain Select.

/* Configuration Register: AD7190_CONF_CHAN(x) options */
#define AD7190_CH_AIN1P_AIN2M      0ul // AIN1(+) - AIN2(-)
#define AD7190_CH_AIN3P_AIN4M      1ul // AIN3(+) - AIN4(-)
#define AD7190_CH_TEMP_SENSOR      2ul // Temperature sensor
#define AD7190_CH_AIN2P_AIN2M      3ul // AIN2(+) - AIN2(-)
#define AD7190_CH_AIN1P_AINCOM     4ul // AIN1(+) - AINCOM
#define AD7190_CH_AIN2P_AINCOM     5ul // AIN2(+) - AINCOM
#define AD7190_CH_AIN3P_AINCOM     6ul // AIN3(+) - AINCOM
#define AD7190_CH_AIN4P_AINCOM     7ul // AIN4(+) - AINCOM

/* Configuration Register: AD7190_CONF_GAIN(x) options */
//                                             ADC Input Range (5 V Reference)
#define AD7190_CONF_GAIN_1      0ul // Gain 1    +-5 V
#define AD7190_CONF_GAIN_8      3ul // Gain 8    +-625 mV
#define AD7190_CONF_GAIN_16     4ul // Gain 16   +-312.5 mV
#define AD7190_CONF_GAIN_32     5ul // Gain 32   +-156.2 mV
#define AD7190_CONF_GAIN_64     6ul // Gain 64   +-78.125 mV
#define AD7190_CONF_GAIN_128    7ul // Gain 128  +-39.06 mV

/* ID Register Bit Designations (AD7190_REG_ID) */
#define ID_AD7190               0x4
#define AD7190_ID_MASK          0x0F

/* GPOCON Register Bit Designations (AD7190_REG_GPOCON) */
#define AD7190_GPOCON_BPDSW     (1ul << 6) // Bridge power-down switch enable
#define AD7190_GPOCON_GP32EN    (1ul << 5) // Digital Output P3 and P2 enable
#define AD7190_GPOCON_GP10EN    (1ul << 4) // Digital Output P1 and P0 enable
#define AD7190_GPOCON_P3DAT     (1ul << 3) // P3 state
#define AD7190_GPOCON_P2DAT     (1ul << 2) // P2 state
#define AD7190_GPOCON_P1DAT     (1ul << 1) // P1 state
#define AD7190_GPOCON_P0DAT     (1ul << 0) // P0 state


#define AD7190_SPI_CLK_SPEED    (5000000)
#define AD7190_CODES            (16777216ul)


class AD7190
{
private:
    uint8_t _cs_pin;
    uint8_t _gain;
    uint8_t _gain_factor;
    uint8_t _data_sta;

protected:
    void _SPI_Transfer(uint8_t* data, uint8_t nr)
    {
        SPISettings setting(AD7190_SPI_CLK_SPEED, MSBFIRST, SPI_MODE3);
        SPI.beginTransaction(setting);
        SPI.transfer(data, nr);
        SPI.endTransaction();
    }

    uint32_t _readRegister(uint8_t reg, uint8_t nr)
    {
        uint8_t word[5] = {0, 0, 0, 0, 0};
        uint32_t buffer = 0;

        word[0] = AD7190_COMM_READ | AD7190_COMM_ADDR(reg);
        _SPI_Transfer(word, nr + 1);

        for(int i = 0; i < nr; i++) {
            buffer = (buffer << 8) + word[i + 1];
        }

        return buffer;
    }

    void _writeRegister(uint8_t reg, uint32_t val, uint8_t nr)
    {
        uint8_t cmd[5] = {0, 0, 0, 0, 0};
        uint8_t c_nr = nr;

        cmd[0] = AD7190_COMM_WRITE | AD7190_COMM_ADDR(reg);
        while(c_nr > 0) {
            cmd[c_nr] = (val & 0xff);
            val >>= 8;
            c_nr--;
        }
        _SPI_Transfer(cmd, nr + 1);
    }

    void _waitDataReady()
    {
        loop_until_bit_is_clear(PINB, 4);
    }



public:

    AD7190(uint8_t cs_pin) :
        _cs_pin(cs_pin),
        _gain(1),
        _gain_factor(1),
        _data_sta(0)
    {

    }

    void reset()
    {
        uint8_t registerWord[6];
        registerWord[0] = 0x01;
        registerWord[1] = 0xFF;
        registerWord[2] = 0xFF;
        registerWord[3] = 0xFF;
        registerWord[4] = 0xFF;
        registerWord[5] = 0xFF;
        registerWord[6] = 0xFF;
        _SPI_Transfer(registerWord, 6);
        delay(1);
    }

    void begin()
    {
        pinMode(_cs_pin, OUTPUT);
        digitalWrite(_cs_pin, HIGH);
        pinMode(12, INPUT);
    }

    bool init()
    {
        reset();
        // wait for ID
        uint32_t regVal = _readRegister(AD7190_REG_ID, 1);
        if ((regVal & AD7190_ID_MASK) != ID_AD7190) {
            return false;
        }
        return true;
    }

    void beginTransaction()
    {
        digitalWrite(_cs_pin, LOW);
    }

    void endTransaction()
    {
        digitalWrite(_cs_pin, HIGH);
    }

    void configDataStatus(int enable)
    {
        uint32_t val = _readRegister(AD7190_REG_MODE, 3);
        val &= ~AD7190_MODE_DAT_STA;  // clear current mode
        if (enable) {
            val |= AD7190_MODE_DAT_STA;
        }
        _data_sta = enable;
        _writeRegister(AD7190_REG_MODE, val, 3);
    }

    uint32_t readDataRegister()
    {
        _waitDataReady();
        if (_data_sta) {
            return _readRegister(AD7190_REG_DATA, 4);
        } else {
            return _readRegister(AD7190_REG_DATA, 3);
        }
    }

    uint32_t readModeRegister()
    {
        return _readRegister(AD7190_REG_MODE, 3);
    }

    uint32_t readConfigRegister()
    {
        return _readRegister(AD7190_REG_CONF, 3);
    }

    uint8_t getMode()
    {
        uint32_t val = _readRegister(AD7190_REG_MODE, 3);
        val &= AD7190_MODE_SEL(7ul);
        return (uint8_t)(val >> 21);
    }

    void setMode(uint8_t mode)
    {
        uint32_t val = _readRegister(AD7190_REG_MODE, 3);
        val &= ~AD7190_MODE_SEL(7ul);  // clear current mode
        val |= AD7190_MODE_SEL(mode);
        _writeRegister(AD7190_REG_MODE, val, 3);
    }

    void configClock(int src)
    {
        uint32_t val = _readRegister(AD7190_REG_MODE, 3);
        val ^= AD7190_MODE_CLKMSK(val);
        val |= AD7190_MODE_CLKSRC(src);
        _writeRegister(AD7190_REG_MODE, val, 3);
    }

    void configFilter(uint16_t rate)
    {
        uint32_t val = _readRegister(AD7190_REG_MODE, 3);
        val ^= AD7190_MODE_RATE(val);
        val |= AD7190_MODE_RATE(rate);
        _writeRegister(AD7190_REG_MODE, val, 3);
    }

    // Config Reg
    void configChop(int enable)
    {
        uint32_t val = _readRegister(AD7190_REG_CONF, 3);
        if (enable) {
            val |= AD7190_CONF_CHOP;
        } else {
            val &= ~AD7190_CONF_CHOP;
        }
        _writeRegister(AD7190_REG_CONF, val, 3);
    }

    void configBuffer(int enable)
    {
        uint32_t val = _readRegister(AD7190_REG_CONF, 3);
        if (enable) {
            val |= AD7190_CONF_BUF;
        } else {
            val &= ~AD7190_CONF_BUF;
        }
        _writeRegister(AD7190_REG_CONF, val, 3);
    }

    void configUnipolar(int enable)
    {
        uint32_t val = _readRegister(AD7190_REG_CONF, 3);
        if (enable) {
            val |= AD7190_CONF_UNIPOLAR;
        } else {
            val &= ~AD7190_CONF_UNIPOLAR;
        }
        _writeRegister(AD7190_REG_CONF, val, 3);
    }

    bool setGain(uint8_t gain)
    {
        if (_gain == gain) {
            return false;
        }

        uint32_t val = _readRegister(AD7190_REG_CONF, 3);
        uint8_t g = AD7190_CONF_GAIN(val);
        val ^= g;
        val |= AD7190_CONF_GAIN(gain);
        _writeRegister(AD7190_REG_CONF, val, 3);

        switch (gain) {
            case AD7190_CONF_GAIN_1:
                _gain_factor = 1;
                break;
            case AD7190_CONF_GAIN_8:
                _gain_factor = 8;
                break;
            case AD7190_CONF_GAIN_16:
                _gain_factor = 16;
                break;
            case AD7190_CONF_GAIN_32:
                _gain_factor = 32;
                break;
            case AD7190_CONF_GAIN_64:
                _gain_factor = 64;
                break;
            case AD7190_CONF_GAIN_128:
                _gain_factor = 128;
                break;
        }

        return true;
    }

    uint8_t getGain()
    {
        uint32_t val = _readRegister(AD7190_REG_CONF, 3);
        _gain = (uint8_t)(AD7190_CONF_GAIN(val));
        return _gain;
    }

    uint8_t getGainFactor()
    {
        return _gain_factor;
    }

    void calibrateInternalScale()
    {
        // We will hold CS pin low
        setMode(AD7190_MODE_CAL_INT_FULL);
        _waitDataReady();
    }

    void calibrateInternalZero()
    {
        // We will hold CS pin low
        setMode(AD7190_MODE_CAL_INT_ZERO);
        _waitDataReady();
    }

    void calibrate(uint8_t chn)
    {
        configChannel(chn);
        calibrateInternalZero();
        calibrateInternalScale();
    }

    void enableChannel(int chn)
    {
        uint32_t val = _readRegister(AD7190_REG_CONF, 3);
        val |= AD7190_CONF_CHAN(chn);
        _writeRegister(AD7190_REG_CONF, val, 3);
    }

    void disableChannel(int chn)
    {
        uint32_t val = _readRegister(AD7190_REG_CONF, 3);
        val ^= AD7190_CONF_CHAN(chn);
        _writeRegister(AD7190_REG_CONF, val, 3);
    }

    void configChannel(int chn)
    {
        uint32_t val = _readRegister(AD7190_REG_CONF, 3);
        val &= ~(0xfflu << 8);
        val |= AD7190_CONF_CHAN(chn);
        _writeRegister(AD7190_REG_CONF, val, 3);
    }
};

#endif