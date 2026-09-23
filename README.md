# FRET Microscope Timing

Firmware experiments for hardware-timed laser/shutter sequencing on a FRET
microscope. Target board is an **Arduino Mega 2560** (16 MHz, ATmega2560).

The sketches are a numbered ladder — each one adds a capability on the way to
the real controller. They are meant to be read in order, not as independent
utilities.

## Sketches

| # | Sketch | What it demonstrates |
|---|--------|----------------------|
| 0 | `0_input_signal_output_pin` | Read an input pin, drive an output pin, report edges over serial. |
| 1 | `1_hardware_trigger_delay_timer` | External interrupt starts Timer1; the compare ISR toggles the output ~1 s later. |
| 2 | `2_pure_hardware_trigger` | Same delay, but Timer1 hardware toggles OC1A directly — no `digitalWrite()` in the signal path. |
| 3 | `3_serial_config` | Serial command interface for delay / exposure / cycle count, plus the capture-sequence state machine. |
| 4 | `4_shift_register_dis` | 4-digit 7-segment display driven over hardware SPI, multiplexed from a Timer2 ISR. |

## Wiring

Common to the trigger sketches (0–3):

```
Trigger input   Pin 2  (INT4) -- button or laser-return signal -- GND
                        uses INPUT_PULLUP: idle = HIGH, asserted = LOW

Output / LED    Pin 11 (OC1A) -- 330 ohm -- LED -- GND
```

Pin 11 is chosen specifically because it is **OC1A**, the Timer1 Compare A
output. That lets Timer1 drive the pin in hardware, which is what removes
interrupt-latency jitter from the timing path.

Display sketch (4):

```
Latch (RCLK)    Pin 6
SPI MOSI        Pin 51
SPI SCK         Pin 52
SPI SS          Pin 53   (must be OUTPUT for hardware SPI master mode)
```

Shift registers are clocked `LSBFIRST`, SPI mode 0. Two bytes per refresh:
digit select, then segment data. Segment bytes are active-low.

## Serial protocol (sketch 3)

9600 baud, newline-terminated commands.

| Command | Response |
|---------|----------|
| `ENABLE` | `OK ENABLED` |
| `DISABLE` | `OK DISABLED` |
| `START` | `STARTED`, then `LASER <n>` per cycle, then `DONE` |
| `GET DELAY` / `SET DELAY <us>` | `OK DELAY <us>us (<ticks> ticks)` |
| `GET EXPOSURE` / `SET EXPOSURE <us>` | `OK EXPOSURE <us>us (<ticks> ticks)` |
| `GET CYCLE_COUNT` / `SET CYCLE_COUNT <n>` | `OK CYCLE COUNT <n>` |

Delay and exposure accept **1–4095 µs**. Timer1 is 16 bit and runs unprescaled
at 16 MHz, so 65535 ticks / 16 = 4095 µs is the longest period it can express.
Out-of-range values are rejected, not truncated. Widening this range means
introducing a prescaler, which costs timing resolution.

Errors: `ERROR NOT ENABLED`, `ERROR BUSY`, `ERROR UNKNOWN COMMAND`,
`ERROR <FIELD> VALUE` (unparseable), `ERROR <FIELD> RANGE <lo>-<hi>`.

`DISABLE` aborts any running sequence and returns the shutter to closed.

## Timer reference

Timer1 at 16 MHz with prescaler 256 gives 62,500 counts/sec. CTC counts
`0..OCR1A` inclusive, so a 1 second period is `OCR1A = 62499`.

Without a prescaler, Timer1 ticks at 16 MHz — 16 ticks per microsecond. That is
the basis for the `us -> ticks` conversion in sketch 3.

## Design notes

**No `Serial` inside an ISR.** A single line at 9600 baud blocks for roughly a
millisecond — orders of magnitude longer than the exposures being timed. The
ISRs raise flags and `loop()` does the printing.

**Shutter transitions are hardware.** OC1A is left in Timer1 toggle mode, so
the delay and exposure edges are produced by the timer peripheral itself. No
`digitalWrite()` sits in the timing path, which is what keeps interrupt latency
out of the measurement.

Because toggle mode is parity-dependent, `resetShutter()` momentarily
disconnects the compare output to force a known-closed state at the start of a
sequence and on abort.

## Status

Sketch 3 runs the full state machine — laser return, delay, exposure, cycle
count — with Timer1 driving the shutter edges. Turning the physical laser on
and off is still stubbed with `TODO` markers pending that pin assignment.
