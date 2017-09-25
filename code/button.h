#ifndef __BUTTON_H__
#define __BUTTON_H__


class Button
{
private:
    uint8_t _pin;
    uint8_t _active;
    uint32_t _time;

public:

    Button(uint8_t pin) :
        _pin(pin)
    {
    }

    void init()
    {
        pinMode(_pin, INPUT);
    }

    void update()
    {
        uint32_t now = millis();
        if (!_active && !digitalRead(_pin)) {
            _active = 1;
            _time = now;
        } else if (_active && digitalRead(_pin)) {
            _active = 0;
            _time = now;
        }
    }

    uint32_t getEventTime() __attribute__((always_inline))
    {
        return _time;
    }

    uint8_t isActive() __attribute__((always_inline))
    {
        return _active;
    }

};


#endif
