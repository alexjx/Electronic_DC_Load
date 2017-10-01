#ifndef __SETTER_H__
#define __SETTER_H__


const int32_t INCREASE_BY[] = {
    1,
    10,
    100,
    1000,
    10000,
};

const int MAX_DIGITS = sizeof(INCREASE_BY) / sizeof(INCREASE_BY[0]);
const int32_t MIN_VALUE = 0;


template <int32_t _MAX_>
class Setter
{
private:
    int32_t _value;
    int8_t  _index;

public:
    Setter() :
        _value(0), _index(0)
    {
    }

    void change(int16_t v)
    {
        _value = constrain(_value + INCREASE_BY[_index] * v,
                           MIN_VALUE,
                           _MAX_);
    }

    void move_left()
    {
        _index = (_index + 1) % MAX_DIGITS;
    }

    void move_right()
    {
        _index = (_index - 1) % MAX_DIGITS;
        if (_index < 0) {
            _index += MAX_DIGITS;
        }
    }

    void set_position(int pos)
    {
        _index = (MAX_DIGITS - pos - 1) % MAX_DIGITS;
    }

    double as_double()
    {
        return static_cast<double>(_value) / 1000.0;
    }

    uint8_t current_bit()
    {
        return _index;
    }
};

#endif