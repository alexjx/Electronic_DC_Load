#ifndef __FAN_H__
#define __FAN_H__

class FanController
{
private:
    uint8_t _pin;
    uint8_t _on;


public:
    FanController(int pin) :
        _pin(pin),
        _on(0)
    {
    }

    void init()
    {
        pinMode(_pin, OUTPUT);
        turn_off();
    }

    void turn_on()
    {
        digitalWrite(_pin, HIGH);
        _on = 1;
    }

    void turn_off()
    {
        digitalWrite(_pin, LOW);
        _on = 0;
    }

    bool isOn()
    {
        return !!_on;
    }

};

#endif