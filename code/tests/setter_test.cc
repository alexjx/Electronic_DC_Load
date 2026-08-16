#include <assert.h>
#include <stdint.h>

#include "EEPROM.h"

template <typename T>
static T constrain(T value, T minimum, T maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

#include "../setter.h"

EEPROMClass EEPROM;

static void eepromRoundTripTests()
{
    Setter<15000> setter;
    assert(setter.get_value() == 0);
    assert(setter.as_double() == 0.0);

    setter.change(12);
    assert(setter.get_value() == 12);
    assert(setter.as_double() == 0.012);
    setter.save_to_eeprom(16);

    Setter<15000> restored;
    assert(restored.load_from_eeprom(16));
    assert(restored.get_value() == 12);
    assert(restored.as_double() == 0.012);

    Setter<15000> minimum;
    minimum.save_to_eeprom(32);
    assert(restored.load_from_eeprom(32));
    assert(restored.get_value() == 0);

    Setter<15000> maximum;
    maximum.change(15);
    maximum.set_position(0);
    maximum.change(1);
    maximum.set_position(1);
    maximum.change(5);
    maximum.set_position(2);
    maximum.change(0);
    maximum.set_position(3);
    maximum.change(0);
    maximum.set_position(4);
    maximum.change(0);
    // The public setter operations must retain the exact raw format.
    maximum.save_to_eeprom(48);
    assert(restored.load_from_eeprom(48));
    assert(restored.get_value() == maximum.get_value());
}

static void invalidValueTests()
{
    const int32_t negative = -1;
    const int32_t over_max = 15001;
    EEPROM.put(64, negative);
    EEPROM.put(80, over_max);

    Setter<15000> setter;
    assert(!setter.load_from_eeprom(64));
    assert(setter.get_value() == 0);
    assert(!setter.load_from_eeprom(80));
    assert(setter.get_value() == 0);

    // Changes are clamped at both ends, including after invalid data fallback.
    setter.change(-1);
    assert(setter.get_value() == 0);
    setter.set_position(4);
    setter.change(2);
    assert(setter.get_value() == 2);
    setter.change(30000);
    assert(setter.get_value() == 15000);
    setter.change(-30000);
    assert(setter.get_value() == 0);
}

static void formatTests()
{
    Setter<99999> setter;
    setter.set_position(0);
    assert(setter.current_bit() == 4);
    setter.move_left();
    assert(setter.current_bit() == 0);
    setter.move_right();
    assert(setter.current_bit() == 4);
    setter.set_position(4);
    setter.change(123);
    assert(setter.get_value() == 123);
    assert(setter.as_double() == 0.123);
}

int main()
{
    eepromRoundTripTests();
    invalidValueTests();
    formatTests();
    return 0;
}
