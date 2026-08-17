#ifndef __ADC_H__
#define __ADC_H__

#include "ad7190.h"
#include "control.h"

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
        double nominal_value;
        uint8_t gain;
        uint8_t channel;
    } _chan[MAX_CHANNELS];

    GainCalibData _gain_cal[MAX_GAINS];

    AD7190 _ad7190;
    AD7190Status _status;

public:

    template<int chn>
    bool _read(double& result, double& nominal_result)
    {
        ADTransaction trans(_ad7190);
        // setup channel
        _ad7190.configChannel(_chan[chn].channel);
        // sample until we have the best value
        do {
            _ad7190.setGain(_chan[chn].gain);
            _ad7190.setMode(AD7190_MODE_SINGLE);
            uint32_t value = 0;
            if (!_ad7190.readDataRegister(value, _chan[chn].channel)) {
                _status = _ad7190.status();
                return false;
            }
            uint8_t gain_factor = _ad7190.getGainFactor();
            const double nominal_voltage =
                (double)value * _vref / AD7190_CODES / gain_factor;
            double voltage = nominal_voltage;

            // FIXME: we need to find a better solution for gain to calib mapping
            if (_chan[chn].gain == AD7190_CONF_GAIN_1) {
                voltage *= _gain_cal[0].scale;
                voltage += _gain_cal[0].offset;
            } else {
                voltage *= _gain_cal[1].scale;
                voltage += _gain_cal[1].offset;
            }

            if (nominal_voltage > GAIN_8_HIGH &&
                _chan[chn].gain != AD7190_CONF_GAIN_1) {
                _chan[chn].gain = AD7190_CONF_GAIN_1;
            } else if (nominal_voltage < GAIN_1_LOW &&
                       _chan[chn].gain != AD7190_CONF_GAIN_8) {
                _chan[chn].gain = AD7190_CONF_GAIN_8;
            } else {
                result = voltage;
                nominal_result = nominal_voltage;
                _status = AD7190_STATUS_OK;
                return true;
            }
        } while (1);
    }

    /* Keep the original helper available to existing callers that only need
     * the calibrated operating reading. */
    template<int chn>
    bool _read(double& result)
    {
        double nominal_result = 0.0;
        return _read<chn>(result, nominal_result);
    }

public:

    ADConverter(uint8_t cs_pin,
                uint8_t voltage_channel,
                uint8_t current_channel,
                double vref,
                uint8_t ready_pin = MISO) :
        _vref(vref),
        _ad7190(cs_pin, ready_pin),
        _status(AD7190_STATUS_OK)
    {
        _chan[CHANNEL_VOLTAGE].value = 0.0;
        _chan[CHANNEL_VOLTAGE].nominal_value = 0.0;
        _chan[CHANNEL_VOLTAGE].gain = 0;
        _chan[CHANNEL_VOLTAGE].channel = voltage_channel;

        _chan[CHANNEL_CURRENT].value = 0.0;
        _chan[CHANNEL_CURRENT].nominal_value = 0.0;
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
        bool detected = _ad7190.init();
        _status = _ad7190.status();
        return detected;
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

    bool init()
    {
        ADTransaction trans(_ad7190);
        // we are running AD7190 in single convert mode.
        // this is to workaround the different gains
        // of current and voltage.
        _ad7190.configUnipolar(1);
        _ad7190.configReferenceDetection(1);
        _ad7190.configDataStatus(1);
        _ad7190.configFilter(DIGITAL_FILTER_WORDS);
        _ad7190.setGain(AD7190_CONF_GAIN_1);
        if (!_ad7190.calibrate(_chan[CHANNEL_VOLTAGE].channel)) {
            _status = _ad7190.status();
            return false;
        }
        if (!_ad7190.calibrate(_chan[CHANNEL_CURRENT].channel)) {
            _status = _ad7190.status();
            return false;
        }
        _ad7190.setMode(AD7190_MODE_PWRDN);
        _status = AD7190_STATUS_OK;
        return true;
    }

    bool updateVoltage() __attribute__((always_inline))
    {
        double voltage = 0.0;
        double nominal_voltage = 0.0;
        if (!_read<CHANNEL_VOLTAGE>(voltage, nominal_voltage)) {
            return false;
        }
        _chan[CHANNEL_VOLTAGE].nominal_value =
            control::theoreticalVoltageFromDivider(nominal_voltage / 1000.0);
        _chan[CHANNEL_VOLTAGE].value =
            (control::theoreticalVoltageFromDivider(voltage / 1000.0) +
             0.006) * 0.9994;
        return true;
    }

    bool updateCurrent() __attribute__((always_inline))
    {
        double current = 0.0;
        double nominal_current = 0.0;
        if (!_read<CHANNEL_CURRENT>(current, nominal_current)) {
            return false;
        }
        _chan[CHANNEL_CURRENT].nominal_value =
            control::theoreticalCurrentFromSenseVoltage(
                nominal_current / 1000.0);
        _chan[CHANNEL_CURRENT].value =
            control::theoreticalCurrentFromSenseVoltage(current / 1000.0);
        return true;
    }

    AD7190Status status() const
    {
        return _status;
    }

    double readVoltage() __attribute__((always_inline))
    {
        return _chan[CHANNEL_VOLTAGE].value;
    }

    double readCurrent() __attribute__((always_inline))
    {
        return _chan[CHANNEL_CURRENT].value;
    }

    /* Nominal readings are derived directly from the schematic transfer
     * values. They intentionally do not include calibration scale/offset. */
    double readNominalVoltage() __attribute__((always_inline))
    {
        return _chan[CHANNEL_VOLTAGE].nominal_value;
    }

    double readNominalCurrent() __attribute__((always_inline))
    {
        return _chan[CHANNEL_CURRENT].nominal_value;
    }

    /* Safety-oriented aliases make the calibration boundary explicit at the
     * call site while retaining the nominal names for diagnostics. */
    double readSafetyVoltage() __attribute__((always_inline))
    {
        return readNominalVoltage();
    }

    double readSafetyCurrent() __attribute__((always_inline))
    {
        return readNominalCurrent();
    }

    void resetCurrent()
    {
        _chan[CHANNEL_CURRENT].value = 0.0;
        _chan[CHANNEL_CURRENT].nominal_value = 0.0;
    }

};

#endif
