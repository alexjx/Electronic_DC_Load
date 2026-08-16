#ifndef __LM35_H__
#define __LM35_H__


#define LM35_SAMPLES 64
#define LM35_ADC_RAIL_LOW 0
#define LM35_ADC_RAIL_HIGH 1023
#define LM35_RAIL_FAULT_SAMPLES 4


class LM35
{
private:
    int _pin;
    int _samples[LM35_SAMPLES];
    int _index;
    int _vref;
    double _temperature;
    bool _valid;
    bool _initialized;
    uint8_t _rail_samples;

    bool _isPlausible()
    {
        double r = 0.0;
        for (int i = LM35_SAMPLES; i > 0; i--) {
            r += (double)_samples[i - 1];
        }

        // Do not impose a temperature range: only reject ADC rail readings.
        return r > (double)LM35_ADC_RAIL_LOW * LM35_SAMPLES &&
               r < (double)LM35_ADC_RAIL_HIGH * LM35_SAMPLES;
    }

protected:
    void _update()
    {
        const int sample = analogRead(_pin);
        _samples[_index] = sample;
        _index = (_index + 1) % LM35_SAMPLES;

        if (sample <= LM35_ADC_RAIL_LOW || sample >= LM35_ADC_RAIL_HIGH) {
            if (_rail_samples < LM35_RAIL_FAULT_SAMPLES) {
                _rail_samples++;
            }
        } else {
            _rail_samples = 0;
        }
    }

public:
    LM35(uint8_t pin, double vref) :
        _pin(pin), _index(0), _vref(vref), _temperature(0.0),
        _valid(false), _initialized(false), _rail_samples(0)
    {
        memset(_samples, 0, sizeof(_samples));
    }

    void init()
    {
        analogReference(EXTERNAL);
        _initialized = false;
        for (int i = LM35_SAMPLES; i > 0; i--)
        {
            _update();
        }
        _valid = _isPlausible() &&
            _rail_samples < LM35_RAIL_FAULT_SAMPLES;
        _initialized = true;
    }

    void update()
    {
        _update();
        _temperature = calcTemperature();
        _valid = _initialized && _isPlausible() &&
            _rail_samples < LM35_RAIL_FAULT_SAMPLES;
    }

    double calcTemperature()
    {
        double r = 0.0;
        for (int i = LM35_SAMPLES; i > 0; i--) {
            r += (double)_samples[i - 1];
        }
        return r * _vref / 1024 / 10.0 / LM35_SAMPLES;
    }

    double getTemperature() __attribute__((always_inline))
    {
        return _temperature;
    }

    bool isValid() __attribute__((always_inline))
    {
        return _valid;
    }
};

#endif
