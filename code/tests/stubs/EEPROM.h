#ifndef ELECTRONIC_DC_LOAD_HOST_EEPROM_H
#define ELECTRONIC_DC_LOAD_HOST_EEPROM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

class EEPROMClass
{
public:
    uint8_t bytes[128];

    EEPROMClass() : bytes{}
    {
    }

    template <typename T>
    void put(int address, const T& value)
    {
        memcpy(bytes + address, &value, sizeof(value));
    }

    template <typename T>
    void get(int address, T& value) const
    {
        memcpy(&value, bytes + address, sizeof(value));
    }
};

extern EEPROMClass EEPROM;

#endif
