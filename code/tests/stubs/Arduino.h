#ifndef ELECTRONIC_DC_LOAD_HOST_ARDUINO_H
#define ELECTRONIC_DC_LOAD_HOST_ARDUINO_H

#include <stdint.h>

#define EXTERNAL 0
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define MISO 12

int analogRead(int pin);
void analogReference(int mode);
uint32_t millis();
int digitalRead(int pin);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
void delay(unsigned long milliseconds);

#endif
