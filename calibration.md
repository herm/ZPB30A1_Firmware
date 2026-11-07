# Calibration

The ZPB30A1 drives the load current open-loop: the MCU sets a PWM value and
there is no current feedback (this hardware design is discussed in
[upstream issue #2](https://github.com/herm/ZPB30A1_Firmware/issues/2)). The
relationship between the requested current and the PWM value is linear but
**unit-specific**. The constants in `software/config.h` (`LOAD_CAL_OFFSET`,
`LOAD_CAL_SLOPE`) were measured on the author's device and can be significantly
different on another unit. A typical uncalibrated unit draws 650 mA while set
to 500 mA.

The firmware can correct this per unit with a **two-point current calibration**
stored in the EEPROM. The on-device wizard is the recommended method; the
compile-time method at the end is a fallback for development and for units that
are so far off that the wizard rejects the fit.

The calibration is only as good as the external meter and the stability of the
source, so use a decent DMM (or a power supply with a trusted current readout).

> The device's own current display and the `I` field of the `VAL:` log show the
> **setpoint**, not the measured current. Always calibrate against an external
> meter.

## What you need

* A power supply that can deliver the calibration currents. The default points
  are 500 mA and 3 A, so a 12 V / 5 A supply is comfortable.
* A DMM with a current range that covers the highest point (10 A range is
  typical). Put it in series with the load:
  `PSU + -> DMM + ... DMM - -> P+ ... P- -> PSU -`.
* The usual load wiring, able to carry the HI current.

Before starting, set the power supply to about 12 V and its current limit
**above the current you expect to measure**, not just above the setpoint. An
uncalibrated unit can draw considerably more than requested. If the supply
goes into current limit, its voltage collapses, the load cannot regulate, and
the wizard aborts the run.

## On-device calibration (recommended)

### Step by step

1. Power the unit and make sure the load is **off** and no error is showing.
   The wizard refuses to start while the load is active or an error is
   present (the press then appears to be ignored).

2. Enter the calibration hub:

   `Main menu -> ... -> CAL -> CURR`

   Turn the encoder to `...` and press it, turn to `CAL` and press it, then
   press `CURR`.

3. **LO point.** The top display shows `LO`, the bottom shows the LO setpoint
   (500 mA on a fresh unit, or the stored point). Press the encoder to edit the
   setpoint, rotate to change it, press to commit. The display then shows
   `LO` / `GO`.

4. Press `GO`. The load switches on and regulates at the LO setpoint. The
   bottom display blinks while the load settles (about 0.5 s). When the current
   is regulated the display changes to `LO` / `RDY`.

   If the load cannot regulate (supply in current limit, voltage too low,
   cutoff active, any error), the run aborts and you are back at the hub
   without saving anything.

5. Read the actual current on the DMM. Press the encoder at `RDY`; the
   measurement editor opens, pre-filled with the setpoint. Set it to the DMM
   reading and commit.

   The load stays on at the LO setpoint while you prepare the HI point. This
   is intentional: it keeps the DMM and the loop in the same state.

6. **HI point.** The top display shows `HI` and the bottom shows the HI
   setpoint (3000 mA by default). Repeat the same sequence:
   edit if needed -> `GO` -> wait for `RDY` -> enter the DMM reading.

   The HI setpoint must be above the LO setpoint; the fit needs at least
   200 mA between the two setpoints and 100 mA between the two measurements.

7. After the HI measurement the load is switched off automatically and the fit
   is checked. The display shows `OK` / `SAV` (the bottom display has only
   three digits, so `SAVE` is truncated). Press the encoder once more to write
   the calibration to the EEPROM. `DONE` confirms the write.

8. Power-cycle the unit and verify a few setpoints against the DMM. The wizard
   now pre-fills the stored LO/HI points when you open it again.

### Aborting

* The **RUN** button aborts from every non-editor screen: the load is switched
  off and nothing is saved. While a value is being edited, the first RUN press
  cancels the edit; a second press aborts the wizard.
* 60 s without any user action aborts the run and switches the load off.
* Any error (overvoltage, overload, cutoff, supply, ...) aborts the run.

### Reset to the compile-time defaults

`CAL -> DEF` restores the constants from `software/config.h`, clears the
calibration flags, and stores the default points (500 mA / 3000 mA). The first
press shows `DEF` / `SUR` (SURE truncated); press again to confirm (`DONE`), or
press RUN to cancel. `DEF` is refused while the load is running. The reset is
stored in the EEPROM, so it survives a power cycle.

### Screen reference

| Top  | Bottom            | Meaning                                                        |
|------|-------------------|----------------------------------------------------------------|
| `LO` | setpoint          | LO point; press to edit                                        |
| `LO` | `GO`              | LO point committed; press to start the run                     |
| `LO` | blinking setpoint | load on, settling                                              |
| `LO` | `RDY`             | regulated; press to enter the DMM reading                      |
| `HI` | as above          | same sequence for the HI point                                 |
| `OK` | `SAV`             | fit valid; press to store (`SAVE` truncated to three digits)   |
| `DONE` | blank           | stored; returns to the hub after about 1 s                     |
| `ERR` | `002` ... `006`  | fit rejected; see the error-code table                         |
| `ERR` | `WR`              | EEPROM write or verify failed; the old calibration stays active |
| `CAL` | blank            | calibration hub                                                |
| `CURR` | blank           | start the current wizard                                       |
| `DEF` | `SUR`             | confirm the reset                                              |

### Error codes

The reason is shown as `ERR` plus a three-digit number:

| Code | Meaning | What to do |
|------|---------|------------|
| `002` | Range: a setpoint or measurement is outside 200 ... 10000 mA, the setpoints are less than 200 mA apart, or the measurements are less than 100 mA apart. | Use valid, well-separated points. |
| `003` | Gain: measured/requested is outside 0.5 ... 2.0. | Check the DMM, wiring and setpoints. If the unit is more than 2x off, use the compile-time method to get it closer first, then repeat the wizard. |
| `004` | Slope: the resulting slope is outside 150000 ... 700000 or more than 2x different from the current constants. | Usually a bad measurement; repeat the run. |
| `005` | Offset: the measured intercept is outside +/-2000 mA, or the resulting offset constant is out of range. | Check the points and repeat. |
| `006` | PWM: the fit would need a PWM <= 0 at the LO point or > 0xFFFF at the HI point, i.e. the points cannot be realized by the hardware. | Raise the LO point or lower the HI point, and check the supply. |
| `WR`  | EEPROM write/verify failed. | The previous calibration is still active. Check the EEPROM, then retry; `DEF` can be used to rewrite a clean default block. |

### Troubleshooting

* **The run aborts immediately after `GO`.** The supply is current-limiting or
  its voltage is too low. Raise the current limit above the expected
  *measured* current and keep the voltage around 12 V. Disable the undervoltage
  cutoff (`... -> CUTO -> ENAB -> OFF`) if it is set close to the supply
  voltage.
* **`003` on a very uncalibrated unit.** The wizard rejects gains outside
  0.5 ... 2.0. Use the compile-time method below to get the unit within that
  window, then run the wizard again.
* **Unstable reading.** Use short, thick leads and give the loop a few
  seconds. The load's current loop is deliberately slow.
* **The load stays on between the LO and HI points.** This is intentional.
  Keep the DMM connected and do not change the wiring during the run.

## Verifying

* Power-cycle the unit; the wizard should pre-fill the stored LO/HI points.
* Check at least five setpoints across the range with the DMM. A result within
  about 2 % is realistic over the calibrated range; outside it the accuracy
  degrades.
* The `VAL:` lines on the UART report the setpoint, not the measured current
  (see [serial protocol.md](serial%20protocol.md)).

## Compile-time calibration (fallback, advanced)

Use this method if you prefer not to use the menu, or if the unit is so far off
that the wizard's gain check rejects it.

A valid calibration in the EEPROM **overrides** the compile-time constants. Run
`CAL -> DEF` (or `make clear_eeprom`) before relying on the values in
`software/config.h`.

### Current/PWM relationship

The firmware converts the current setpoint to the raw PWM compare value
(16-bit `TIM1->CCR1`) as:

    PWM = (current_mA * LOAD_CAL_SLOPE - LOAD_CAL_OFFSET) / 2^16

* `LOAD_CAL_SLOPE` (`slope`): slope, in 2^16 PWM LSBs per mA.
* `LOAD_CAL_OFFSET` (`offset`): the Q16 term subtracted from the slope term;
  the actual y-intercept is `-LOAD_CAL_OFFSET / 2^16`, so a positive value
  lowers the PWM.

### Measuring

1. Flash the firmware as-is (see the README for flashing).
2. Connect a trusted ammeter in series with the load, plus a suitable power
   supply and the usual P+/P- wiring.
3. Run the load in CC mode at several setpoints across your working range and
   record `I_set` and the current on the meter, for example:

   | I_set (mA) | I_measured (mA) |
   |------------|-----------------|
   | 350        | 421             |
   | 830        | 1000            |
   | 1500       | 1720            |

   Your numbers will differ; that is exactly why calibration is needed.

### Fitting the constants

1. Open `hardware/i_load.ods` (LibreOffice etc.). It implements the firmware
   formula above: enter your measured points, and the spreadsheet computes the
   linear fit (`slope`, `offset`) and the resulting error per point.
2. Replace the measurement column with your own values and read the resulting
   `slope` and `offset`.
3. An error of about 1-2 % over the measured range is a realistic result.

Quicker alternative: scale `LOAD_CAL_OFFSET` by the relative error you measure
(e.g. if the real current is 20 % too high, reduce `LOAD_CAL_OFFSET` by 20 %),
reflash and repeat until close enough. This works because the offset dominates
the error on most units.

### Applying

1. Set `LOAD_CAL_OFFSET` / `LOAD_CAL_SLOPE` in `software/config.h`.
2. Rebuild and reflash, then verify the measurement points again and refine if
   necessary. See the README ("Chip") for flash endurance.
3. `make clear_eeprom` (or `CAL -> DEF`) removes a stored calibration so the
   compile-time values take effect.

## How the runtime calibration works

* The wizard applies two requested currents (`LO`, `HI`), waits for the loop to
  regulate, and asks for the actual currents measured on the DMM. From those
  two points it computes the measured gain and intercept and corrects the
  reference `slope`/`offset` that was active during the run. It does not
  attempt to measure PWM directly (there is no feedback path for that).
* The fit is validated before it can be stored: gain 0.5 ... 2.0, intercept
  +/-2000 mA, slope 150000 ... 700000, |offset| <= 5e8, and realizability (the
  LO point must need a positive PWM, the HI point must fit in 0xFFFF). A fit
  that would be clamped is rejected with `006`.
* The calibration is a separate 40-byte, versioned block at EEPROM address
  `0x40` (magic `0x5A`, version 1, CRC-8). It is independent of `settings_t`.
  The block is written payload-first, then the CRC, then the magic byte, and
  read back for verification. An interrupted write leaves a block that fails a
  check, so the next boot falls back to the compile-time defaults.
* At runtime `calibration_current_to_pwm()` performs
  `PWM = (I*slope - offset) / 2^16` in Q4 and clamps to `[0, 0xFFFF]`. The
  solver validates with the same core conversion before clamping, so the two
  cannot drift apart.
* Unknown channel flags are ignored, so a block written by newer firmware can
  still be read. `CAL_VERSION` changes only if the layout itself changes.

## Voltage and temperature (not user-calibratable in v1)

The `Vl`/`Vs` voltage channels, the +12 V input and the temperature channel use
compile-time constants in `software/config.h`:

* `ADC_CAL_LOAD_T` / `ADC_CAL_LOAD_M` - load-terminal voltage
* `ADC_CAL_SENSE_T` / `ADC_CAL_SENSE_M` - sense voltage
* `ADC_CAL_12V` - +12 V input divider
* `ADC_CAL_TEMP_T` / `ADC_CAL_TEMP_M` - temperature

These channels are measurement-only and are usually closer unit-to-unit than
the open-loop current path, so v1 does not expose them in the wizard. The
EEPROM block already reserves the `V_load` and `V_sense` fields and flag bits,
so a later version can add them without changing the layout or `CAL_VERSION`.

Temperature is deliberately out of scope: the linear model is the dominant
error source, and the correct fix would be a lookup table or a
Steinhart-Hart equation in `adc.c`, not a two-point fit. The +12 V input is only
used for the 10 V supply threshold and the log field, so a separate calibration
would add little value.

The measurement data and fitting spreadsheets for these channels are in
`hardware/` (`v_sense.ods`, `Temperature.ods`).
