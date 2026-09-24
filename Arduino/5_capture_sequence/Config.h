// ============================================================================
// CONFIGURATION
//
// Compile-time facts about the rig: which pins, which polarity, what the
// limits are. Nothing here changes at runtime -- the values the SET commands
// write are in Settings.h.
//
// The constants sit in the header rather than being declared extern because a
// namespace-scope const has internal linkage in C++. Each file gets its own
// copy, and for compile-time integers the compiler folds them into the
// instructions and emits nothing. A duplicated string would cost real flash,
// so the PROGMEM ones below are the exception.
// ============================================================================

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>


// ----------------------------------------------------------------------------
// IDENTITY
//
// Bump FIRMWARE_VERSION when the protocol changes, since that is what a host
// would gate its behaviour on. BUILD_STAMP is defined in the sketch file; see
// the note beside it.
//
// FLASH_STR is the cast that lets Serial and String read a PROGMEM string
// straight from flash rather than copying it into SRAM first.
// ----------------------------------------------------------------------------

extern const char FIRMWARE_NAME[] PROGMEM;
extern const char FIRMWARE_VERSION[] PROGMEM;
extern const char BUILD_STAMP[] PROGMEM;

#define FLASH_STR(s) ((const __FlashStringHelper *)(s))


// ----------------------------------------------------------------------------
// PINS
// ----------------------------------------------------------------------------

// laser_confirm must sit on an external interrupt pin.
// Mega 2560: INT0=21 INT1=20 INT2=19 INT3=18 INT4=2 INT5=3
const uint8_t LASER_CONFIRM_PIN = 2;

const uint8_t CAMERA_CAPTURING_PIN = 3;

const uint8_t LASER_ENABLE_PIN = 8;

const uint8_t LASER_SIGNAL_PIN = 9;

// shutter_enable MUST be pin 11. That is OC1A, the Timer1 Compare A output,
// which is what lets the timer drive the gate in hardware.
const uint8_t SHUTTER_ENABLE_PIN = 11;


// ----------------------------------------------------------------------------
// SIGNAL POLARITY
//
// Inputs are treated as active-high and driven push-pull. If a source is
// open-collector, switch its pinMode to INPUT_PULLUP in setup(), flip the
// active level here, and change the confirm edge to FALLING.
// ----------------------------------------------------------------------------

const uint8_t CAMERA_ACTIVE_LEVEL = HIGH;

const int LASER_CONFIRM_EDGE = RISING;

// laser_signal idles HIGH and pulses LOW to fire.
const uint8_t LASER_SIGNAL_IDLE = HIGH;
const uint8_t LASER_SIGNAL_FIRE = LOW;


// ----------------------------------------------------------------------------
// LASER SIGNAL PULSE WIDTH
//
// Set with SET PULSE <us>. Short enough to be negligible against delay_us,
// long enough for the laser controller to latch.
//
// The ceiling comes from delayMicroseconds(), which is only accurate up to
// 16383 us on AVR.
// ----------------------------------------------------------------------------

const unsigned long MIN_PULSE_US = 1;
const unsigned long MAX_PULSE_US = 16383;


// ----------------------------------------------------------------------------
// LASER CONFIRM TIMEOUT
//
// Set with SET CONFIRM_TIMEOUT <ms>. Without it a laser that never answers
// leaves the sequence wedged with laser_enable held high.
//
// 0 disables the timeout, for a laser trusted to always respond.
// ----------------------------------------------------------------------------

const unsigned long MIN_CONFIRM_TIMEOUT_MS = 0;
const unsigned long MAX_CONFIRM_TIMEOUT_MS = 600000UL;


// ----------------------------------------------------------------------------
// SERIAL
//
// 8N1, so one byte costs 10 bits on the wire:
//
//     9600 baud ->    960 byte/s -> 1.04 ms per byte
//   115200 baud -> 11520 byte/s -> 0.09 ms per byte
//
// A verbose cycle emits roughly 200 characters. At 9600 baud that is about
// 208 ms of transmission, which lands entirely in the gap between cycles.
// Raise this to 115200 when logging a fast sequence.
// ----------------------------------------------------------------------------

const unsigned long SERIAL_BAUD = 9600;


// ----------------------------------------------------------------------------
// TIMING RANGE
//
// Timer1 is 16 bit. With a prescaler chosen automatically the reachable range
// is wide, but resolution degrades as the prescaler grows:
//
//     /1     0.0625 us resolution, up to     4096 us
//     /8     0.5    us resolution, up to    32768 us
//     /64    4      us resolution, up to   262144 us
//     /256   16     us resolution, up to  1048576 us
//     /1024  64     us resolution, up to  4194304 us
//
// The smallest prescaler that can represent a value is always used, so short
// durations keep full resolution. Requests are rounded down to the resolution
// step, and the SET/GET response echoes what was actually achieved.
//
// /1024 is listed for completeness but is never selected: /256 already covers
// everything up to MAX_US.
// ----------------------------------------------------------------------------

const uint8_t TICKS_PER_US = 16;

const unsigned long MIN_US = 1;
const unsigned long MAX_US = 1000000UL;

const uint16_t MIN_CYCLE_COUNT = 1;
const uint16_t MAX_CYCLE_COUNT = 65535;

#endif
