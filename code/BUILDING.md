# Building the firmware in WSL

The firmware targets an Arduino Uno-compatible ATmega328P running at 16 MHz.

## One-time setup

On Ubuntu 22.04 under WSL:

```sh
sudo apt-get update
sudo apt-get install -y arduino-core-avr arduino-mk gcc-avr avr-libc avrdude make
git submodule update --init --recursive
```

The repository pins the external Arduino libraries through Git submodules. The
Ubuntu packages provide the AVR compiler, Arduino AVR core, and Arduino-Makefile.

Versions used to reproduce the baseline build:

- Arduino AVR core: `1.8.4+dfsg1-1`
- Arduino-Makefile: `1.5.2-2.1`
- avr-gcc/avr-g++: `5.4.0`
- avr-libc: `2.0.0`
- GNU Make: `4.3`

## Build

From the repository root:

```sh
make -C code clean
make -C code
```

Build artifacts are written under `code/bin/` and ignored by Git. The `.hex`
file is the image used for flashing.

The Makefile defaults to the Ubuntu package location, `/usr/share/arduino`.
`ARDMK_DIR` and `ARDUINO_DIR` can still be overridden on the command line for a
different installation.

## WSL hardware note

Compiling does not require USB access. Uploading from WSL additionally requires
the Arduino's USB serial device to be attached to WSL and may require adjusting
`MONITOR_PORT`; flashing has not yet been verified on physical hardware.
