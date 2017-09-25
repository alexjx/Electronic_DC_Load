#ifndef __LM35_H__
#define __LM35_H__


#define LM35_SAMPLES 16


class LM35
{
private:
    int _pin;
    int _samples[LM35_SAMPLES];
    int _index;
    int _vref;
    double _temperature;

protected:
    void _update()
    {
        _samples[_index] = analogRead(_pin);
        _index = (_index + 1) % LM35_SAMPLES;
    }

public:
    LM35(uint8_t pin, double vref) :
        _pin(pin), _index(0), _vref(vref), _temperature(0.0)
    {
        memset(_samples, 0, sizeof(_samples));
    }

    void init()
    {
        analogReference(EXTERNAL);
        for (int i = LM35_SAMPLES - 1; i > 0; i--)
        {
            _update();
        }
    }

    void update()
    {
        _update();
        calcTemperature();
    }

    double calcTemperature()
    {
        double r = 0.0;
        for (int i = LM35_SAMPLES - 1; i > 0; i--) {
            r += _samples[i];
        }
        return r * _vref / 1024 / 10.0 / LM35_SAMPLES;
    }

    double getTemperature() __attribute__((always_inline))
    {
        return _temperature;
    }
};

#endif