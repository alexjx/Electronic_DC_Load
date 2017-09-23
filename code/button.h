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

    }

};


#endif
