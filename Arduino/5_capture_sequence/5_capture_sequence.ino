// ============================================================================
// CAPTURE SEQUENCE
//
// Arduino Mega 2560
//
// Runs cycle_count iterations of:
//
//     ensure camera_capturing is HIGH
//     pulse laser_signal LOW then HIGH
//     wait for laser_confirm
//     wait delay_us            <- Timer1
//     assert shutter_enable    <- Timer1 hardware toggle
//     wait capture_us          <- Timer1
//     release shutter_enable   <- Timer1 hardware toggle
//
// laser_enable is raised once when the sequence starts and dropped when it
// finishes or aborts.
//
// The delay and capture edges are produced by the Timer1 compare output, not
// by software, so interrupt latency stays out of the timing path. See
// sketch 2 for the bare version of that technique.
// ============================================================================


// ============================================================================
// MODULES
//
// Each has a header for the interface and a .cpp for the reasoning. They
// depend on each other in one direction, so the stack reads bottom up.
//
//     Config      pins, polarity, limits, firmware identity
//     Timer1      durations, arming, the shutter output
//     Settings    the six values SET and GET operate on
//     LogRing     the verbose event ring, its drain, and the log clock
//     Sequence    the state machine and the two ISRs
//     Commands    the serial protocol, HELP, and the banner
//
// The README covers what each boundary is for.
// ============================================================================


#include "Commands.h"
#include "Config.h"
#include "LogRing.h"
#include "Sequence.h"
#include "Settings.h"
#include "Timer1.h"


// The banner prints this as the build time. It is the compile time of this
// file, not of the binary: the build caches per file, so editing only a module
// leaves it stale. Build with --clean when it has to be right.
const char BUILD_STAMP[] PROGMEM = __DATE__ " " __TIME__;


void setup()
{
  Serial.begin(SERIAL_BAUD);

  pinMode(CAMERA_CAPTURING_PIN, INPUT);
  pinMode(LASER_CONFIRM_PIN, INPUT);

  // Write before pinMode, and again after, so the pin never glitches to the
  // wrong level as it switches from input to output.
  digitalWrite(LASER_ENABLE_PIN, LOW);
  pinMode(LASER_ENABLE_PIN, OUTPUT);
  digitalWrite(LASER_ENABLE_PIN, LOW);

  digitalWrite(LASER_SIGNAL_PIN, LASER_SIGNAL_IDLE);
  pinMode(LASER_SIGNAL_PIN, OUTPUT);
  digitalWrite(LASER_SIGNAL_PIN, LASER_SIGNAL_IDLE);

  digitalWrite(SHUTTER_ENABLE_PIN, LOW);
  pinMode(SHUTTER_ENABLE_PIN, OUTPUT);
  digitalWrite(SHUTTER_ENABLE_PIN, LOW);

  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  // COM1A0: toggle OC1A on every compare match.
  TCCR1A = (1 << COM1A0);

  // Enable the Compare A interrupt. Without this the sequence cannot advance.
  TIMSK1 = (1 << OCIE1A);

  // Timer5, free-running, read by the verbose log only. Started regardless of
  // VERBOSE: it costs nothing to leave counting and nothing has to change
  // when VERBOSE is turned on mid-session.
  beginLog();

  // Durations are stored as resolved register values, so the defaults have to
  // go through resolveDuration() rather than being written out in Settings.
  resolveDuration(1, DelayTime);
  resolveDuration(50, CaptureTime);

  attachInterrupt(
      digitalPinToInterrupt(LASER_CONFIRM_PIN),
      laserConfirmISR,
      LASER_CONFIRM_EDGE
  );

  // Banner first, then READY. READY stays the last line of boot output, which
  // is what a host waits for.
  printBanner();

  Serial.println("READY");
}


void loop()
{
  // servicePending() first, always. It is what arms the next cycle, and
  // readCommand() can block on a serial write for hundreds of milliseconds.
  servicePending();

  readCommand();
}
