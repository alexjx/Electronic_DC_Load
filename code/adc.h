#ifndef __ADC_H__
#define __ADC_H__

#include "ad7190.h"

//
const double GAIN_8_HIGH = 600.0;
const double GAIN_1_LOW = 550.0;
const int DIGITAL_FILTER_WORDS = 48;


// Calibrate Data for different gains
struct GainCalibData
{
    double offset;
    double scale;
};


// Helper function for hold the CS pin
class ADTransaction
{
private:
    AD7190& _device;

public:
    ADTransaction(AD7190& dev) :
        _device(dev)
    {
        _device.beginTransaction();
    }

    ~ADTransaction()
    {
        _device.endTransaction();
    }
};


// Channels
enum
{
    CHANNEL_VOLTAGE = 0,
    CHANNEL_CURRENT = 1,

    MAX_CHANNELS = 2,
};

enum
{
    GAIN_1 = 0,
    GAIN_8 = 1,

    MAX_GAINS = 2,
};


// The AD converter sensor
class ADConverter
{
private:

    double _vref;

    struct {
        double value;
        uint8_t gain;
        uint8_t channel;
    } _chan[MAX_CHANNELS];

    GainCalibData _gain_cal[MAX_GAINS];

    AD7190 _ad7190;

public:

    template<int chn>
    double _read()
    {
        ADTransaction trans(_ad7190);
        // setup channel
        _ad7190.configChannel(_chan[chn].channel);
        // sample until we have the best value
        do {
            _ad7190.setGain(_chan[chn].gain);
            _ad7190.setMode(AD7190_MODE_SINGLE);
            uint32_t value = _ad7190.readDataRegister();
            uint8_t gain_factor = _ad7190.getGainFactor();
            double voltage = (double)value * _vref / AD7190_CODES / gain_factor;

            // FIXME: we need to find a better solution for gain to calib mapping
            if (_chan[chn].gain == AD7190_CONF_GAIN_1) {
                voltage *= _gain_cal[0].scale;
                voltage += _gain_cal[0].offset;
            } else {
                voltage *= _gain_cal[1].scale;
                voltage += _gain_cal[1].offset;
            }

            if (voltage > GAIN_8_HIGH && _chan[chn].gain != AD7190_CONF_GAIN_1) {
                _chan[chn].gain = AD7190_CONF_GAIN_1;
            } else if (voltage < GAIN_1_LOW && _chan[chn].gain != AD7190_CONF_GAIN_8) {
                _chan[chn].gain = AD7190_CONF_GAIN_8;
            } else {
                return voltage;
            }
        } while (1);
    }

public:

    ADConverter(uint8_t cs_pin,
                uint8_t voltage_channel,
                uint8_t current_channel,
                double vref) :
        _vref(vref),
        _ad7190(cs_pin)
    {
        _chan[CHANNEL_VOLTAGE].value = 0.0;
        _chan[CHANNEL_VOLTAGE].gain = 0;
        _chan[CHANNEL_VOLTAGE].channel = voltage_channel;

        _chan[CHANNEL_CURRENT].value = 0.0;
        _chan[CHANNEL_CURRENT].gain = 0;
        _chan[CHANNEL_CURRENT].channel = current_channel;

        _gain_cal[0].scale = 1.0;
        _gain_cal[0].offset = 0.0;
        _gain_cal[1].scale = 1.0;
        _gain_cal[1].offset = 0.0;
    }

    void begin()
    {
        _ad7190.begin();
    }


    bool detectDevice()
    {
        ADTransaction trans(_ad7190);
        return _ad7190.init();
    }

    void setCalibData(uint8_t gain, double scale, double offset)
    {
        if (gain == AD7190_CONF_GAIN_1) {
            _gain_cal[0].scale = scale;
            _gain_cal[0].offset = offset;
        } else {
            _gain_cal[1].scale = scale;
            _gain_cal[1].offset = offset;
        }
    }

    void init()
    {
        ADTransaction trans(_ad7190);
        // we are running AD7190 in single convert mode.
        // this is to workaround the different gains
        // of current and voltage.
        _ad7190.configUnipolar(1);
        _ad7190.configFilter(DIGITAL_FILTER_WORDS);
        _ad7190.setGain(AD7190_CONF_GAIN_1);
        _ad7190.calibrate(_chan[CHANNEL_VOLTAGE].channel);
        _ad7190.calibrate(_chan[CHANNEL_CURRENT].channel);
        _ad7190.setMode(AD7190_MODE_PWRDN);
    }

    double updateVoltage() __attribute__((always_inline))
    {
        _chan[CHANNEL_VOLTAGE].value = _read<CHANNEL_VOLTAGE>() * 10.09 / 1000.0;
        return readVoltage();
    }

    double updateCurrent() __attribute__((always_inline))
    {
        _chan[CHANNEL_CURRENT].value = _read<CHANNEL_CURRENT>() / 50.0 / 0.005 / 1000.0;
        return readCurrent();
    }

    double readVoltage() __attribute__((always_inline))
    {
        return _chan[CHANNEL_VOLTAGE].value;
    }

    double readCurrent() __attribute__((always_inline))
    {
        return _chan[CHANNEL_CURRENT].value;
    }

    void resetCurrent()
    {
        _chan[CHANNEL_CURRENT].value = 0.0;
    }

};

#endif
