# Electronic Controlled DC Dummy Load

An Arduino-compatible electronic load built around an ATmega328P. It draws a
controlled current from a DC source and displays the measured current, voltage,
power, temperature, and accumulated capacity.

> [!WARNING]
> This is an experimental high-power project, not a certified test instrument.
> The 15 A and 200 W values are firmware ceilings, not proven continuous hardware
> ratings. Safe operation depends on the input voltage, IRFP250N safe operating
> area, heatsink, airflow, shunt and PCB heating, wiring, connectors, calibration,
> and an external fuse. Begin with a current-limited supply at low power and do not
> leave the load unattended. The refactored firmware has not yet been validated on
> the physical hardware.

## Hardware overview

The load uses a power MOSFET as a variable resistance. A precision shunt measures
the resulting current, while the firmware adjusts a DAC until measured current
matches the requested value.

```text
Source under test -> J1 -> IRFP250N -> 5 mOhm shunt -> ground
                         ^                   |
                         |                   +-> INA210 -> AD7190 ADC
ATmega328P -> AD5541 DAC -> AD8629 gate-control amplifier
```

| Part | Purpose |
| --- | --- |
| ATmega328P-AU, 16 MHz | Runs the user interface, safety checks, and current controller |
| AD7190 | Measures load voltage and shunt current |
| AD5541 | Produces the analog current command |
| AD8629 | Buffers voltage sensing and drives the MOSFET control loop |
| INA210 and 5 mOhm / 4 W shunt | Measures load current |
| IRFP250N | Dissipates the power drawn from the source |
| LM35 | Measures heatsink temperature |
| 16x2 LCD and PCF8574 | Displays settings and measurements over I2C |
| REF5050 | Provides the 5 V ADC, DAC, and analog reference |

### Connections

| Connector | Function | Notes |
| --- | --- | --- |
| J1 | DC source under test | Schematic pad 2 is load positive; pad 1 is return/ground. Confirm this against the assembled PCB before applying power. |
| J2 | Control-power input | Feeds the MP1584 supply stage. The repository does not document a safe input-voltage range or jack polarity; verify the assembled board before use. |
| J3 | AVR ISP | Programming connector for the ATmega328P |
| J4 | Cooling fan | Nominal 12 V fan output controlled by the firmware |

The complete Eagle design is in [`schematic/`](schematic/). The analog voltage
divider shown there and the firmware calibration factor do not exactly match, so
verify voltage and current readings against calibrated meters before power testing.

## Controls and display

The top LCD row shows the current setpoint, cutoff voltage, and operating status.
A blank status means idle; `*` means the load is running. The LCD cursor identifies
the digit changed by the rotary encoder.

| Control | Action |
| --- | --- |
| Rotate encoder | Change the selected current or cutoff-voltage digit |
| S1 / D4 | Move the selected digit left |
| S4 / D6 | Move the selected digit right |
| S3 / D3 | Show the previous measurement page |
| S2 / D5 | Show the next measurement page |
| Hold encoder switch | Start loading after the controller has been idle for at least 3 seconds |
| Click encoder while running | Stop loading immediately |
| Click encoder on a cleared fault | Acknowledge the fault and return to idle |

The four bottom-row pages show:

1. Measured current and voltage.
2. Calculated power and heatsink temperature.
3. Session capacity in mAh.
4. Session energy in Wh.

The mAh and Wh counters reset whenever a new load session starts. The setpoints are
saved when a session starts or stops.

## Usage guide

1. With all power disconnected, inspect the board, fit an adequate heatsink and
   fan, and add a suitable external fuse to the source-under-test connection.
2. Confirm J1 polarity and the required J2 voltage and polarity from the actual
   assembled board. Connect the fan to J4, then apply control power to J2.
3. Check that the display starts normally and that no fault is reported. Verify the
   voltage, current, and temperature readings before enabling the power stage.
4. Use S1/S4 to select a digit and rotate the encoder to set the desired current and
   low-voltage cutoff. Start with a small current.
5. Turn the current-limited source off, connect it to J1 with the verified polarity,
   and then turn the source on.
6. After the controller has been idle for at least 3 seconds, hold the encoder switch
   until loading starts. Confirm that `*` appears and watch current, power, and
   temperature while testing.
7. Click the encoder to stop. Confirm that `*` disappears before disconnecting the
   source.

The firmware limits the setpoint to 15.000 A, reduces the current request above a
calculated 200 W, trips overcurrent at 16.5 A, and trips overtemperature at 95 degC.
These protections supplement rather than replace correctly rated hardware and an
external fuse.

### Faults

Any firmware fault commands the DAC to zero and latches the load off.

| Display | Meaning | Before acknowledging |
| --- | --- | --- |
| `FAULT ADC` | ADC did not initialize or respond in time | Check ADC power, SPI wiring, and ready signal |
| `FAULT TEMP SNS` | Temperature reading is outside the plausible sensor range | Check the LM35 and its wiring |
| `FAULT OVERCUR` | Measured current exceeded 16.5 A | Remove the cause of excess current |
| `FAULT UNDERVOLT` | Source voltage fell below the configured cutoff | Recharge, replace, or disconnect the source |
| `FAULT OVERTEMP` | Temperature exceeded 95 degC | Keep cooling active and wait for the load to cool |

After correcting the cause, click the encoder when the display says
`Click to ack`. Acknowledgement only returns the controller to idle; it does not
restart the load. A condition that is still unsafe will fault again or refuse to
clear.

## Firmware build and tests

Initialize the pinned libraries, then build the Arduino Uno firmware:

```sh
git submodule update --init --recursive
make -C code clean
make -C code
```

Run the hardware-independent safety and driver tests with:

```sh
make -C code/tests clean
make -C code/tests
```

See [`code/BUILDING.md`](code/BUILDING.md) for the tested Ubuntu/WSL environment,
tool versions, generated files, and current flashing limitations.
