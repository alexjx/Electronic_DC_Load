#ifndef __AD5541_H__
#define __AD5541_H__

// TODO: We should wrap the DAC and ADC too
#define AD5541_SPI_SPEED   5000000ul
#define AD5541_CODES       65536u
#define AD5541_CODE_LOW    0
#define AD5541_CODE_HIGH   65535u

class AD5541 {
protected:
    uint16_t _current;
    uint16_t _fast_mode;
    int _fs_pin;
    int _cs_pin;
    int _last;

    void send_to_device() {
        // Send CS signal
        digitalWrite(_cs_pin, LOW);
        delayMicroseconds(1);  // datsheet only need 10ns
        SPISettings setting(AD5541_SPI_SPEED, MSBFIRST, SPI_MODE0);
        SPI.beginTransaction(setting);
        SPI.transfer16(_current);
        SPI.endTransaction();
        digitalWrite(_cs_pin, HIGH);
    }

public:
    AD5541(int cs_pin) {
    	_cs_pin = cs_pin;
        _last = 0;
        _current = -1;
    }

    void begin() {
        pinMode(_cs_pin, OUTPUT);
        digitalWrite(_cs_pin, HIGH);
    }

    void setValue(uint16_t value) {
        value = constrain(value, AD5541_CODE_LOW, AD5541_CODE_HIGH);
        if (value != _current) {
            _current = value;
            send_to_device();
        }
    }

    uint16_t getValue() {
        return _current;
    }

};

#endif