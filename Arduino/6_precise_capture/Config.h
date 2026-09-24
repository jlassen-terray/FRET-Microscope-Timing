// ============================================================================
// CONFIGURATION
//
// Compile-time facts about the rig: pins, polarity, limits, and the two
// latency constants that line the timers up with the edges they answer. The
// values SET writes live in Settings.h.
// ============================================================================

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>


extern const char FIRMWARE_NAME[] PROGMEM;
extern const char FIRMWARE_VERSION[] PROGMEM;
extern const char BUILD_STAMP[] PROGMEM;

#define FLASH_STR(s) ((const __FlashStringHelper *)(s))


// ----------------------------------------------------------------------------
// PINS
//
// Three of these are fixed by the hardware that times them:
//
//     laser_confirm   pin 2  = INT4, whose edge flag is polled directly
//     laser_signal    pin 6  = OC4A, Timer4 places both pulse edges
//     shutter_enable  pin 11 = OC1A, Timer1 places both gate edges
// ----------------------------------------------------------------------------

const uint8_t LASER_CONFIRM_PIN = 2;
const uint8_t CAMERA_CAPTURING_PIN = 3;
const uint8_t LASER_SIGNAL_PIN = 6;
const uint8_t LASER_ENABLE_PIN = 8;
const uint8_t SHUTTER_ENABLE_PIN = 11;


// ----------------------------------------------------------------------------
// SIGNAL POLARITY
//
// For an open-collector or optoisolated source: INPUT_PULLUP, FALLING, and
// flip CAMERA_ACTIVE_LEVEL.
// ----------------------------------------------------------------------------

const uint8_t CAMERA_ACTIVE_LEVEL = HIGH;

const uint8_t LASER_CONFIRM_INPUT_MODE = INPUT;
const int LASER_CONFIRM_EDGE = RISING;


// ----------------------------------------------------------------------------
// LIMITS
// ----------------------------------------------------------------------------

const uint8_t TICKS_PER_US = 16;

const uint32_t MIN_WINDOW_US = 1;
const uint32_t MAX_WINDOW_US = 1000000UL;

// One Timer4 period at /1.
const uint32_t MIN_PULSE_US = 1;
const uint32_t MAX_PULSE_US = 4096;

const uint32_t MIN_CYCLE_COUNT = 1;
const uint32_t MAX_CYCLE_COUNT = 65535;

const uint32_t MIN_CONFIRM_TIMEOUT_MS = 0;
const uint32_t MAX_CONFIRM_TIMEOUT_MS = 600000UL;

// Long windows run as several Timer1 periods of at most this length. A run
// checks the UART between periods, and at 9600 baud the UART holds three
// bytes, about 3 ms. Two periods of 16384 ticks are 2.048 ms.
const uint32_t MAX_SEGMENT_TICKS = 16384UL;


// ----------------------------------------------------------------------------
// LATENCY COMPENSATION
//
// Cycles between an edge the firmware reacts to and the timer it starts.
// Preloaded into the timer so the interval is measured from the edge itself.
// Counted from the compiled output; see "Calibration" in the README.
// ----------------------------------------------------------------------------

const uint16_t CONFIRM_TO_TIMER1_TICKS = 8;
const uint16_t PULSE_FALL_TO_TIMER4_TICKS = 2;


const unsigned long SERIAL_BAUD = 9600;

#endif
