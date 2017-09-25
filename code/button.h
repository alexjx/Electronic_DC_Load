#ifndef __BUTTON_H__
#define __BUTTON_H__


#define ACTIVE_TICK_COUNT   1


class Button
{
private:
    uint8_t _pin;
    uint8_t _active;
    uint8_t _active_tick;
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
            _active_tick = 0;
        } else if (_active && digitalRead(_pin)) {
            _active = 0;
            _time = now;
        } else if (_active && !digitalRead(_pin)) {
            _active_tick++;
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

    bool isRaisingEdge() __attribute__ ((always_inline))
    {
        return _active && _active_tick < ACTIVE_TICK_COUNT;
    }

};


#endif
