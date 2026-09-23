# 5 — Capture Sequence

The capture controller for the FRET rig. Arduino Mega 2560.

Runs `cycle_count` iterations of: verify the camera is rolling, hand off to the
laser, wait a configured delay, then open a capture gate for a configured
window. The delay and capture edges are produced by the Timer1 compare output
rather than by software, so interrupt latency stays out of the timing path.

Builds on [sketch 2](../2_pure_hardware_trigger) for the hardware-toggle
technique and [sketch 3](../3_serial_config) for the serial command interface.

> **Status:** compiles clean, **not yet hardware tested**. The `laser_signal`
> pulse width (10 µs) and the `laser_confirm` timeout (1 s) were unspecified
> and are placeholders — both are named constants at the top of the sketch.

## Signals

| Signal | Pin | Direction | Idle |
|--------|-----|-----------|------|
| `laser_confirm` | 2 (INT4) | in | — |
| `camera_capturing` | 3 | in | — |
| `laser_enable` | 8 | out | LOW |
| `laser_signal` | 9 | out | HIGH |
| `shutter_enable` | 11 (OC1A) | out | LOW |

`shutter_enable` must stay on pin 11 — it is the Timer1 Compare A output, and
that is what produces the gate edges in hardware.

`laser_confirm` must stay on an external interrupt pin. On the Mega 2560 those
are INT0=21, INT1=20, INT2=19, INT3=18, INT4=2, INT5=3.

Inputs are treated as **active-high and push-pull**. If a source is
open-collector, switch it to `INPUT_PULLUP`, flip `CAMERA_ACTIVE_LEVEL`, and
set `LASER_CONFIRM_EDGE` to `FALLING` — all three are constants at the top of
the sketch.

## Wiring

```mermaid
flowchart LR
    CAM["Camera<br/>capture-status output"] -->|5V TTL| P3
    LCONF["Laser controller<br/>confirm output"] -->|5V TTL| P2

    subgraph MEGA["Arduino Mega 2560"]
        direction TB
        P2["Pin 2 / INT4<br/>laser_confirm<br/>INPUT, RISING"]
        P3["Pin 3<br/>camera_capturing<br/>INPUT, active HIGH"]
        P8["Pin 8<br/>laser_enable<br/>OUTPUT, idle LOW"]
        P9["Pin 9<br/>laser_signal<br/>OUTPUT, idle HIGH"]
        P11["Pin 11 / OC1A<br/>shutter_enable<br/>OUTPUT, idle LOW"]
    end

    P8 -->|5V TTL| LEN["Laser controller<br/>enable input"]
    P9 -->|5V TTL| LSIG["Laser controller<br/>trigger input"]
    P11 -->|5V TTL| SH["Shutter / gate driver<br/>gate input"]
```

## Grounding

Every signal above is referenced to the Arduino's ground. Without a shared
return the levels are meaningless and inputs can be damaged — this is the most
common way a working 5V signal kills a pin.

```mermaid
flowchart TB
    MGND["Arduino Mega GND"]
    MGND --- CGND["Camera<br/>signal return"]
    MGND --- LGND["Laser controller<br/>signal return"]
    MGND --- SGND["Shutter driver<br/>signal return"]
```

## Input protection

5V logic is what the Mega expects: V<sub>IH</sub> is 0.6 × VCC = 3.0 V, and the
absolute maximum on a pin is VCC + 0.5 V = 5.5 V. Direct connection is fine for
genuine 5V push-pull sources on short cables.

A series resistor costs nothing and limits current through the internal ESD
clamps if a line ever overshoots past 5.5 V:

```mermaid
flowchart LR
    SRC["5V source"] --> R["220R - 1k<br/>series resistor"] --> PIN["Mega input pin"]
    PIN -.->|"internal ESD clamp diodes"| RAIL["VCC / GND rails"]
```

For instruments on separate supplies, long cable runs, or any output above 5 V,
use an optocoupler instead. This also removes ground loops between instrument
chassis and protects the board when the instrument is powered but the Arduino
is not — an easy state to hit in a lab:

```mermaid
flowchart LR
    subgraph INST["Instrument side"]
        direction TB
        OUT["Output<br/>5V / 12V / 24V"] --> RL["series resistor<br/>sized for the level"] --> LED["opto LED"]
    end

    subgraph ARD["Arduino side"]
        direction TB
        PT["opto transistor"] --> PIN2["Mega input<br/>INPUT_PULLUP, active LOW"]
    end

    LED -.->|"optical - no shared ground"| PT
```

Note the optoisolated form inverts the signal and needs a pull-up, so it is the
`INPUT_PULLUP` / `FALLING` configuration described under Signals.

## Sequence

```mermaid
sequenceDiagram
    participant PC as Host serial
    participant M as Arduino Mega
    participant C as Camera
    participant L as Laser controller
    participant S as Shutter gate

    PC->>M: START
    Note over M: cycle := 0
    M->>L: laser_enable HIGH

    loop cycle_count times
        M->>C: read camera_capturing
        C-->>M: HIGH (abort if LOW)
        M->>L: laser_signal LOW 10us then HIGH
        L-->>M: laser_confirm (rising edge)
        Note over M: Timer1 waits delay_us
        M-->>S: shutter_enable HIGH (Timer1 hardware)
        Note over M: Timer1 waits capture_us
        M-->>S: shutter_enable LOW (Timer1 hardware)
        M-->>PC: CYCLE n
    end

    M->>L: laser_enable LOW
    M-->>PC: DONE
```

## State machine

```mermaid
stateDiagram-v2
    [*] --> IDLE

    IDLE --> WAIT_CONFIRM: START, camera HIGH<br/>pulse laser_signal
    IDLE --> IDLE: START, camera LOW<br/>ERROR CAMERA NOT CAPTURING

    WAIT_CONFIRM --> WAIT_DELAY: laser_confirm<br/>arm Timer1 delay_us
    WAIT_CONFIRM --> IDLE: 1s elapsed<br/>ERROR LASER CONFIRM TIMEOUT

    WAIT_DELAY --> CAPTURING: compare match<br/>shutter opens in hardware<br/>arm Timer1 capture_us

    CAPTURING --> WAIT_CONFIRM: compare match, cycles remain<br/>shutter closes in hardware<br/>re-check camera, pulse again
    CAPTURING --> COMPLETE: compare match, last cycle<br/>shutter closes, laser_enable LOW

    COMPLETE --> WAIT_CONFIRM: START
    CAPTURING --> IDLE: ABORT
    WAIT_DELAY --> IDLE: ABORT
    WAIT_CONFIRM --> IDLE: ABORT
```

The shutter transitions are marked *in hardware* because Timer1 drives OC1A
directly — the ISR runs after the edge has already happened and only decides
what comes next.

## Commands

9600 baud, newline-terminated, case-insensitive.

| Command | Notes |
|---------|-------|
| `SET DELAY <us>` / `GET DELAY` | 1–1000000 µs |
| `SET CAPTURE <us>` / `GET CAPTURE` | 1–1000000 µs |
| `SET CYCLE_COUNT <n>` / `GET CYCLE_COUNT` | 1–65535 |
| `START` | Emits `STARTED`, `CYCLE <n>` per cycle, then `DONE` |
| `ABORT` (or `STOP`) | Drops laser_enable, releases the shutter |
| `STATUS` | State, cycle progress, live camera level |

`SET` is rejected with `ERROR BUSY` while a sequence is running. That
restriction is what makes it safe for the Timer1 ISR to read the configuration
without volatile qualifiers or interrupt guards.

Errors: `ERROR BUSY`, `ERROR CAMERA NOT CAPTURING`,
`ERROR LASER CONFIRM TIMEOUT`, `ERROR UNKNOWN COMMAND`,
`ERROR <FIELD> VALUE`, `ERROR <FIELD> RANGE <lo>-<hi>`.

`ABORT`/`STOP`, the confirm timeout, and `STATUS` were not in the original
spec. They were added because a laser controller needs a stop, a silent laser
should not be able to wedge the sequence with `laser_enable` held high, and
bring-up is easier with a status read. All three are easy to strip.

## Timing resolution

Timer1 auto-selects the smallest prescaler that can represent each duration, so
short windows keep full resolution and long ones are still reachable:

| Prescaler | Resolution | Max |
|-----------|-----------|-----|
| /1 | 0.0625 µs | 4095 µs |
| /8 | 0.5 µs | 32767 µs |
| /64 | 4 µs | 262140 µs |
| /256 | 16 µs | 1048560 µs |

`GET`/`SET` echo both the requested and the achieved value, e.g.
`OK DELAY 5000us -> 5000us (1250 ticks @ /64)`, so truncation is always visible
rather than silent.

## Build

```
arduino-cli compile --fqbn arduino:avr:mega . --warnings all
```
