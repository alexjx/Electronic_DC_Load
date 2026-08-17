# Firmware Value Provenance

This document records where the firmware's electrical conversion constants and
protection limits came from. It separates values confirmed by the repository's
schematic and PCB from engineering policy values that still require physical
validation.

## Source priority

Values are resolved in this order:

1. The component actually fitted to the assembled board.
2. The [schematic](../schematic/Electronic%20DC%20Load.sch) and
   [PCB](../schematic/Electronic%20DC%20Load.brd).
3. Manufacturer datasheets for the confirmed fitted component.
4. Existing firmware behavior.
5. Conservative firmware policy where the hardware does not define a value.

The repository labels current-sense amplifier U3 as an INA210. The assembled
board is reported to use an **INA213**, so the firmware intentionally uses the
INA213 nominal gain of 50 V/V. This override should be confirmed from the
physical package marking or purchase record.

The schematic and PCB label input-divider resistor R5 as 9.9 kOhm. The purchase
record instead identifies the fitted value as **9.09 kOhm**. The firmware uses
the fitted value, giving a 10.09:1 divider with R11. A resistance measurement on
an unpowered board should confirm it when practical.

## Confirmed transfer values

| Quantity | Repository evidence | Firmware value | Confidence |
| --- | --- | ---: | --- |
| Analog reference | U1, REF5050 | 5.0 V | High |
| Current shunt | R1, `.005` | 5 mOhm | High |
| Current-sense gain | Fitted U3 reported as INA213 | 50 V/V | Medium until physically confirmed |
| Input-voltage divider | Purchase record: R5 = 9.09 kOhm; R11 = 1 kOhm | 10.09:1 | Medium until physically confirmed |
| DAC divider | R12 = 39 kOhm, R8 = 10 kOhm | 10/49 | High |
| ADC | U2, AD7190BRUZ | AD7190 | High |
| DAC | U6, AD5541ARZ | AD5541 | High |

The schematic and PCB agree on the reference, shunt, and DAC-divider values.
They disagree with the purchase record for input-divider R5. Net tracing
confirms that R5/R11 feed the voltage measurement path and R12/R8 feed the
AD8629 current-control loop.

The single source of truth for these nominal values is
[`code/control.h`](../code/control.h). The ADC adapter uses the same functions
for its uncalibrated safety readings, avoiding a second set of constants.

## Current measurement

For a 5 mOhm shunt and INA213 gain of 50 V/V:

```text
amplifier_output = load_current * 0.005 * 50
                 = load_current * 0.25

load_current = amplifier_output / 0.25
```

At 15 A, the nominal amplifier output is 3.75 V.

This equation is used for absolute current and power protection without applying
the historical calibration coefficients.

## Input-voltage measurement

The fitted R5 and R11 values form a 9.09 kOhm / 1 kOhm divider:

```text
divider_ratio = (9.09k + 1k) / 1k
              = 10.09

input_voltage = ADC_input * 10.09
```

At a 50 V input, the nominal ADC input is approximately 4.955 V. The theoretical
5 V measurement full scale corresponds to only 50.45 V at the load input. The
present 50 V firmware trip therefore has less than 1% nominal input-voltage
headroom. It should not be increased, and lowering it requires deciding the
intended maximum operating voltage. In either case, this calculation is **not**
proof that the complete load is safe to dissipate power near 50 V.

## DAC current command

The AD5541 output is divided by R12 and R8 before reaching the AD8629 current
servo. The servo compares that target with the voltage across the 5 mOhm shunt:

```text
DAC_voltage = DAC_code / 65536 * 5.0

target_shunt_voltage = DAC_voltage * 10k / (39k + 10k)

target_current = target_shunt_voltage / 0.005
```

The ideal DAC code for 15 A is:

```text
15 * 0.005 * (39k + 10k) / 10k * 65536 / 5
= 4816.896 codes
```

The current firmware rounds this to a 4,817-code hard ceiling, corresponding to
approximately 15.0003 A nominally. A strict mathematical safety floor would be
4,816 codes, corresponding to approximately 14.9972 A. Component tolerances are
much larger than this one-code difference, but flooring is the cleaner policy
when the limit must never exceed nominal 15 A.

## Calibration boundary

The existing ADC calibration coefficients have no documented provenance:

| AD7190 gain | Scale | Offset |
| --- | ---: | ---: |
| 1 | 1.00080 | 4.0 mV |
| 8 | 1.00243 | -0.60 mV |

They remain available for displayed measurements and session accounting. They
do not change the nominal current, voltage, power, or DAC safety ceilings.
Accurate display and energy results require these coefficients to be confirmed
with traceable bench measurements.

## Engineering policy values

The following values are not directly established by resistor ratios or a
validated system-level hardware rating:

| Policy | Current value | Basis and limitation | Confidence as a safe physical limit |
| --- | ---: | --- | --- |
| User current ceiling | 15 A | Historical firmware setting and nominal DAC model | Medium |
| Overcurrent trip | 16.5 A | 10% above the command ceiling; backup protection | Medium |
| Input-voltage trip | 50 V | Only 0.45 V nominal source-side ADC headroom with the fitted divider | Low |
| Measured-power trip | 200 W | Retained historical firmware value | Low |
| Continuous command limit | 180 W | 10% margin below the historical value | Low |
| Thermal derating start | 80 C | Conservative firmware policy | Low |
| Thermal trip | 95 C | Historical firmware value | Low |
| Upward current slew | 5 A/s | Firmware transient-limiting policy | Medium |
| No-source threshold | 0.1 V | Noise/compliance guard | Medium |
| Cutoff qualification | 500 ms, 0.1 V hysteresis | Noise-rejection policy | Medium |
| AD7190 conversion timeout | 100 ms | Expected conversion time plus margin | Medium |
| I2C transaction timeout | 1 ms | Expected 100 kHz bus transaction plus margin | Medium |

In particular, 180 W or 200 W must not be interpreted as a validated continuous
rating. The safe load envelope depends on the IRFP250N DC safe-operating-area
curve, drain voltage, heatsink thermal resistance, airflow, ambient temperature,
sensor placement, gate-loop stability, wiring, and protection response time.

## Required physical verification

Before relying on the limits for real power testing:

1. Confirm that U3 on the assembled board is an INA213.
2. Confirm the fitted shunt and divider values, especially the R5 discrepancy,
   including tolerances.
3. Measure the REF5050 voltage and the current-to-DAC transfer at low power.
4. Verify ADC current and voltage readings against traceable instruments.
5. Establish a conservative voltage/current/time envelope from the fitted
   IRFP250N datasheet and actual cooling system.
6. Measure startup, current-step, source-disconnect, fault, and stop transients
   with an oscilloscope.
7. Validate heatsink, fan, and LM35 behavior at controlled increasing power.
8. Confirm that the ADC and I2C timeout margins are reliable on the assembled
   hardware.

Until these checks are complete, the firmware limits should be treated as
defensive bounds derived from the available design information, not as a
certified operating envelope.
