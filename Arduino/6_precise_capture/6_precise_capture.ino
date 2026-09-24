// ============================================================================
// PRECISE CAPTURE SEQUENCE
//
// Arduino Mega 2560
//
// Runs cycle_count iterations of:
//
//     ensure camera_capturing is HIGH
//     pulse laser_signal LOW then HIGH   <- Timer4 hardware, both edges
//     wait for laser_confirm             <- INT4 edge flag, polled
//     wait delay_us                      <- Timer1
//     assert shutter_enable              <- Timer1 hardware set
//     wait capture_us                    <- Timer1
//     release shutter_enable             <- Timer1 hardware clear
//
// laser_enable is held HIGH for the run. Interrupts are off from START to the
// last edge, so every interval lands within 1 us of its setting. The README
// has the timing budget.
// ============================================================================


// ============================================================================
// MODULES
//
//     Config      pins, polarity, limits, latency compensation
//     Settings    the five values SET and GET operate on
//     Shutter     Timer1: the delay and capture windows on OC1A
//     LaserPulse  Timer4: the laser_signal pulse on OC4A
//     Sequence    the run: camera check, confirm wait, window feed
//     Commands    the serial protocol, HELP, and the banner
// ============================================================================


#include "Commands.h"
#include "Config.h"
#include "LaserPulse.h"
#include "Sequence.h"
#include "Shutter.h"


// Compile time of this file, not the binary. Build with --clean when it has
// to be right.
const char BUILD_STAMP[] PROGMEM = __DATE__ " " __TIME__;


void setup()
{
  Serial.begin(SERIAL_BAUD);

  beginShutter();
  beginLaserPulse();
  beginSequence();

  printBanner();

  Serial.println("READY");
}


void loop()
{
  readCommand();
}
