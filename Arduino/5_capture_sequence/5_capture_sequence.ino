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


// ----------------------------------------------------------------------------
// IDENTITY
//
// Printed in the boot banner. FIRMWARE_VERSION is the thing to bump when the
// protocol changes, since that is what a host would gate its behaviour on.
//
// __DATE__ and __TIME__ are stamped by the compiler, which makes the banner
// the quickest way to confirm the board is running the build you think it is.
// One caveat: a cached build is not recompiled, so an unchanged sketch keeps
// its earlier stamp. Touch the file or clean the build if that matters.
// ----------------------------------------------------------------------------

// PROGMEM keeps these out of SRAM. FLASH_STR is the cast that lets Serial and
// String read them back from flash rather than copying them down first.
const char FIRMWARE_NAME[] PROGMEM = "FRET Capture Sequence Controller";
const char FIRMWARE_VERSION[] PROGMEM = "1.0.0";

#define FLASH_STR(s) ((const __FlashStringHelper *)(s))

// SERIAL_BAUD is defined with the rest of the serial notes further down, and
// the banner prints it.


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
// open-collector, switch it to INPUT_PULLUP in setup(), flip the active level
// here, and change the confirm edge to FALLING.
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

unsigned long LaserSignalPulseUs = 10;


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

unsigned long ConfirmTimeoutMs = 5000;


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
// VERBOSE LOGGING
//
// Set with SET VERBOSE 1. Events are timestamped the instant they happen and
// queued; the formatting and the actual serial write both happen later, in
// loop(). See the LOG RING section for why that matters.
// ----------------------------------------------------------------------------

bool VerboseEnabled = false;


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


// ----------------------------------------------------------------------------
// A RESOLVED TIMER DURATION
// ----------------------------------------------------------------------------

struct Duration
{
  unsigned long requestedUs;
  unsigned long actualUs;
  uint16_t compare;      // OCR1A value
  uint16_t prescaler;
  uint8_t clockSelect;   // CS bits for TCCR1B
};


// ----------------------------------------------------------------------------
// RESOLVE MICROSECONDS TO TIMER1 SETTINGS
//
// Returns false if the value cannot be represented.
// ----------------------------------------------------------------------------

bool resolveDuration(unsigned long us, Duration &out)
{
  if (us < MIN_US || us > MAX_US) {
    return false;
  }

  const uint16_t prescalers[5] = { 1, 8, 64, 256, 1024 };

  const uint8_t clockSelects[5] = {
      (1 << CS10),
      (1 << CS11),
      (1 << CS11) | (1 << CS10),
      (1 << CS12),
      (1 << CS12) | (1 << CS10)
  };

  for (uint8_t i = 0; i < 5; i++) {
    unsigned long ticks = (us * (unsigned long)TICKS_PER_US) / prescalers[i];

    if (ticks < 1 || ticks > 65536UL) {
      continue;
    }

    out.requestedUs = us;

    // CTC counts 0..OCR1A inclusive, so the compare value is one less than
    // the tick count.
    out.compare = (uint16_t)(ticks - 1);

    out.prescaler = prescalers[i];
    out.clockSelect = clockSelects[i];

    // What the hardware will actually produce after integer truncation.
    out.actualUs = (ticks * prescalers[i]) / TICKS_PER_US;

    return true;
  }

  return false;
}


// ----------------------------------------------------------------------------
// CONFIGURATION
//
// These are read by the Timer1 ISR. They are deliberately not volatile:
// SET commands are rejected while a sequence is running, so there is never a
// concurrent write for the ISR to trip over.
// ----------------------------------------------------------------------------

Duration DelayTime;
Duration CaptureTime;

uint16_t CycleCount = 1;


// Just the value, no "OK <LABEL>" prefix, so the boot banner can print the
// same text in a column without having to strip anything off the front.
String durationDetail(const Duration &d)
{
  return String(d.requestedUs) + "us -> " + d.actualUs + "us (" +
         ((unsigned long)d.compare + 1) + " ticks @ /" + d.prescaler + ")";
}

String describeDuration(const char *label, const Duration &d)
{
  return String("OK ") + label + " " + durationDetail(d);
}

String getDelayResponseString()
{
  return describeDuration("DELAY", DelayTime);
}

String getCaptureResponseString()
{
  return describeDuration("CAPTURE", CaptureTime);
}

String getCycleCountResponseString()
{
  return String("OK CYCLE_COUNT ") + CycleCount;
}

String getPulseResponseString()
{
  return String("OK PULSE ") + LaserSignalPulseUs + "us";
}

String getVerboseResponseString()
{
  return String("OK VERBOSE ") + (VerboseEnabled ? 1 : 0);
}

String getConfirmTimeoutResponseString()
{
  if (ConfirmTimeoutMs == 0) {
    return String("OK CONFIRM_TIMEOUT 0 (disabled)");
  }

  return String("OK CONFIRM_TIMEOUT ") + ConfirmTimeoutMs + "ms";
}


// ----------------------------------------------------------------------------
// SEQUENCE STATE
// ----------------------------------------------------------------------------

enum SequenceState
{
  IDLE,
  WAITING_FOR_LASER_CONFIRM,
  WAITING_FOR_DELAY,
  CAPTURING,
  COMPLETE
};

volatile SequenceState State = IDLE;

volatile uint16_t CurrentCycle = 0;

enum TimerEvent
{
  TIMER_NONE,
  TIMER_SHUTTER_OPEN,
  TIMER_SHUTTER_CLOSE
};

volatile TimerEvent Timer1Event = TIMER_NONE;

// Set when a cycle begins, used to time out a missing laser_confirm.
volatile unsigned long ConfirmWaitStartedMs = 0;


// ----------------------------------------------------------------------------
// DEFERRED OUTPUT AND WORK
//
// Nothing prints from an ISR. At 9600 baud a byte costs 1.04 ms on the wire,
// so a 40-character line is roughly 40 ms -- four orders of magnitude longer
// than the windows being timed. The ISR raises a flag and loop() does the
// work.
//
// PendingNextCycle matters for a second reason: starting a cycle reads the
// camera input, pulses laser_signal with a blocking delay, and may need to
// report an error. None of that belongs in an interrupt.
// ----------------------------------------------------------------------------

volatile bool PendingCycleStarted = false;
volatile uint16_t PendingCycleNumber = 0;
volatile bool PendingDone = false;
volatile bool PendingNextCycle = false;


// ----------------------------------------------------------------------------
// LOG RING
//
// Verbose logging must not perturb what it is measuring, so it is split in
// two. Producers capture an event id and a micros() stamp into this ring --
// a handful of instructions, always placed AFTER the timer registers have
// been written, so no hardware edge ever waits on it. loop() then drains the
// ring and does the expensive part: formatting and the serial write.
//
// Producers run in both interrupt and main context, so pushes save and
// restore SREG rather than using noInterrupts()/interrupts(). Calling
// interrupts() inside an ISR would re-enable them early and allow reentry.
//
// If the ring fills, events are counted and discarded rather than blocking.
// A dropped count is reported so the log can never quietly lie about what
// happened.
// ----------------------------------------------------------------------------

enum LogEventId : uint8_t
{
  LOG_CYCLE_BEGIN,
  LOG_PULSE,
  LOG_CONFIRM,
  LOG_SHUTTER_OPEN,
  LOG_SHUTTER_CLOSE,
  LOG_DONE
};

struct LogEntry
{
  unsigned long timestampUs;
  uint16_t cycle;
  uint8_t event;
};

const uint8_t LOG_CAPACITY = 32;

LogEntry LogRing[LOG_CAPACITY];

volatile uint8_t LogHead = 0;
volatile uint8_t LogTail = 0;
volatile uint8_t LogDropped = 0;


// Safe from any context. Costs a few dozen cycles.
//
// The cycle number is passed in rather than derived, because CurrentCycle
// means different things either side of the increment in the shutter-close
// ISR: before it, the cycle in progress is CurrentCycle + 1; after it, the
// cycle that just finished is CurrentCycle. logEvent() covers the common
// case and DONE uses logEventForCycle() directly.
void logEventForCycle(uint8_t event, uint16_t cycle)
{
  if (!VerboseEnabled) {
    return;
  }

  // Stamp first, so queueing cost is not included in the measurement.
  unsigned long now = micros();

  uint8_t sreg = SREG;
  cli();

  uint8_t next = LogHead + 1;

  if (next >= LOG_CAPACITY) {
    next = 0;
  }

  if (next == LogTail) {
    if (LogDropped < 255) {
      LogDropped++;
    }

  } else {
    LogRing[LogHead].timestampUs = now;
    LogRing[LogHead].cycle = cycle;
    LogRing[LogHead].event = event;

    LogHead = next;
  }

  SREG = sreg;
}


void logEvent(uint8_t event)
{
  logEventForCycle(event, CurrentCycle + 1);
}


// The "+" column is a gap between two consecutive drained events, so the
// drain side carries state of its own. It has to be cleared here too, or the
// first event of a run reports the gap since the *previous* run's last event
// -- an interval that measures nothing and can be arbitrarily large.
unsigned long LogLastUs = 0;
bool LogHaveLast = false;

void resetLog()
{
  uint8_t sreg = SREG;
  cli();

  LogHead = 0;
  LogTail = 0;
  LogDropped = 0;

  SREG = sreg;

  LogHaveLast = false;
}


// ----------------------------------------------------------------------------
// DRAIN THE LOG
//
// Called from loop() only, and deliberately last, so arming the next cycle
// never waits on a serial write.
//
// Lines are prefixed "V " so a host parser can separate them from command
// responses. The "+" column is the gap since the previous logged event,
// which is the number worth watching: it is the measured version of
// delay_us, capture_us, and the laser's own response time.
// ----------------------------------------------------------------------------

void drainLog()
{
  while (LogTail != LogHead) {
    uint8_t sreg = SREG;
    cli();

    LogEntry entry = LogRing[LogTail];

    uint8_t next = LogTail + 1;

    if (next >= LOG_CAPACITY) {
      next = 0;
    }

    LogTail = next;

    SREG = sreg;

    Serial.print(F("V "));
    Serial.print(entry.cycle);
    Serial.print(' ');

    switch (entry.event)
    {
      case LOG_CYCLE_BEGIN:   Serial.print(F("CYCLE_BEGIN"));   break;
      case LOG_PULSE:         Serial.print(F("PULSE"));         break;
      case LOG_CONFIRM:       Serial.print(F("CONFIRM"));       break;
      case LOG_SHUTTER_OPEN:  Serial.print(F("SHUTTER_OPEN"));  break;
      case LOG_SHUTTER_CLOSE: Serial.print(F("SHUTTER_CLOSE")); break;
      case LOG_DONE:          Serial.print(F("DONE"));          break;
      default:                Serial.print(F("UNKNOWN"));       break;
    }

    Serial.print(F(" t="));
    Serial.print(entry.timestampUs);

    if (LogHaveLast) {
      Serial.print(F(" +"));
      Serial.print(entry.timestampUs - LogLastUs);
    }

    Serial.println();

    LogLastUs = entry.timestampUs;
    LogHaveLast = true;
  }

  if (LogDropped > 0) {
    uint8_t sreg = SREG;
    cli();

    uint8_t dropped = LogDropped;
    LogDropped = 0;

    SREG = sreg;

    Serial.print(F("V LOG_DROPPED "));
    Serial.println(dropped);
  }
}


// ----------------------------------------------------------------------------
// TIMER 1 CONTROL
// ----------------------------------------------------------------------------

void armTimer1(const Duration &d)
{
  TCNT1 = 0;

  OCR1A = d.compare;

  // Discard a stale compare match, which would otherwise fire immediately.
  TIFR1 = (1 << OCF1A);

  // CTC mode plus the prescaler this duration resolved to.
  TCCR1B = (1 << WGM12) | d.clockSelect;
}

void stopTimer1()
{
  TCCR1B &= ~((1 << CS12) | (1 << CS11) | (1 << CS10));
}


// ----------------------------------------------------------------------------
// SHUTTER
//
// OC1A runs in hardware toggle mode, so the pin state depends on how many
// compare matches have happened. An odd number leaves the gate open, which is
// what an abort mid-capture produces, so there has to be a way to force it
// back to released.
//
// Writing the port will not do it. While a COM1A bit is set the waveform
// generator owns the pin, and the value it drives lives in the OC1A register,
// which keeps its state across a TCCR1A write. Disconnecting the output,
// writing PORTB low and reconnecting therefore only drives the pin low for the
// few cycles it is disconnected: the moment toggle mode comes back, the stale
// OC1A register reappears on the pin and the shutter is open again.
//
// The supported way to set OC1A directly is a forced compare. Select
// "clear on compare match" and strobe FOC1A: the compare output logic applies
// the COM1A setting to the OC1A register without raising OCF1A and without
// resetting the counter. Then restore toggle mode for the next window.
// ----------------------------------------------------------------------------

void resetShutter()
{
  TCCR1A = (1 << COM1A1);
  TCCR1C = (1 << FOC1A);

  TCCR1A = (1 << COM1A0);
}


// ----------------------------------------------------------------------------
// SETUP
// ----------------------------------------------------------------------------

void setup()
{
  Serial.begin(SERIAL_BAUD);

  // --------------------------------------------------------------------------
  // Inputs
  // --------------------------------------------------------------------------

  pinMode(CAMERA_CAPTURING_PIN, INPUT);
  pinMode(LASER_CONFIRM_PIN, INPUT);

  // --------------------------------------------------------------------------
  // Outputs
  //
  // Write before pinMode so the pin never glitches to the wrong level as it
  // switches from input to output.
  // --------------------------------------------------------------------------

  digitalWrite(LASER_ENABLE_PIN, LOW);
  pinMode(LASER_ENABLE_PIN, OUTPUT);
  digitalWrite(LASER_ENABLE_PIN, LOW);

  digitalWrite(LASER_SIGNAL_PIN, LASER_SIGNAL_IDLE);
  pinMode(LASER_SIGNAL_PIN, OUTPUT);
  digitalWrite(LASER_SIGNAL_PIN, LASER_SIGNAL_IDLE);

  digitalWrite(SHUTTER_ENABLE_PIN, LOW);
  pinMode(SHUTTER_ENABLE_PIN, OUTPUT);
  digitalWrite(SHUTTER_ENABLE_PIN, LOW);

  // --------------------------------------------------------------------------
  // Timer1
  // --------------------------------------------------------------------------

  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  // COM1A0: toggle OC1A on every compare match.
  TCCR1A = (1 << COM1A0);

  // Enable the Compare A interrupt. Without this the sequence cannot advance.
  TIMSK1 = (1 << OCIE1A);

  // --------------------------------------------------------------------------
  // Defaults
  // --------------------------------------------------------------------------

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


// ----------------------------------------------------------------------------
// LASER CONFIRM INTERRUPT
//
// The laser has acknowledged. Start the configured delay; when it expires the
// Timer1 hardware toggles OC1A and the shutter gate opens with no software in
// the path.
// ----------------------------------------------------------------------------

void laserConfirmISR()
{
  if (State != WAITING_FOR_LASER_CONFIRM) {
    return;
  }

  State = WAITING_FOR_DELAY;

  Timer1Event = TIMER_SHUTTER_OPEN;

  armTimer1(DelayTime);

  // After arming, never before: the delay window starts at the line above.
  logEvent(LOG_CONFIRM);
}


// ----------------------------------------------------------------------------
// TIMER 1 COMPARE-A INTERRUPT
//
// The shutter transition has ALREADY happened in hardware by the time this
// runs. The ISR only sequences what comes next.
// ----------------------------------------------------------------------------

ISR(TIMER1_COMPA_vect)
{
  switch (Timer1Event)
  {
    // ------------------------------------------------------------------------
    // SHUTTER OPENED
    // ------------------------------------------------------------------------
    case TIMER_SHUTTER_OPEN:
      State = CAPTURING;

      Timer1Event = TIMER_SHUTTER_CLOSE;

      armTimer1(CaptureTime);

      logEvent(LOG_SHUTTER_OPEN);

      break;

    // ------------------------------------------------------------------------
    // SHUTTER RELEASED
    // ------------------------------------------------------------------------
    case TIMER_SHUTTER_CLOSE:
      stopTimer1();

      logEvent(LOG_SHUTTER_CLOSE);

      Timer1Event = TIMER_NONE;

      CurrentCycle++;

      if (CurrentCycle >= CycleCount) {
        State = COMPLETE;

        digitalWrite(LASER_ENABLE_PIN, LOW);

        // CurrentCycle has already been incremented, so it is now the number
        // of the cycle that just finished -- which is the one DONE belongs to.
        logEventForCycle(LOG_DONE, CurrentCycle);

        PendingDone = true;

      } else {
        // loop() starts the next cycle; see PendingNextCycle above.
        PendingNextCycle = true;
      }

      break;

    // ------------------------------------------------------------------------
    // A MATCH NOBODY ASKED FOR
    //
    // The timer is running with no transition expected, so it is free-running
    // in CTC and will keep toggling the gate every OCR1A. Whatever re-armed it
    // did so behind the state machine; shut it down and close the shutter
    // rather than leaving the pin flapping.
    // ------------------------------------------------------------------------
    default:
      stopTimer1();

      resetShutter();

      break;
  }
}


// ----------------------------------------------------------------------------
// STOP EVERYTHING AND RETURN OUTPUTS TO A SAFE STATE
//
// Always called from loop(), and runs with interrupts off. That is load
// bearing rather than tidiness: laserConfirmISR fires on whatever the laser
// does, and it re-arms Timer1 whenever it finds State still set to
// WAITING_FOR_LASER_CONFIRM. Tearing down with interrupts on leaves a window
// between stopTimer1() and State = IDLE where a late confirm edge starts the
// timer back up behind us. Nothing stops it after that, so the delay expires,
// the hardware toggles the gate open, and the compare ISR finds no pending
// event and does nothing -- shutter held open with the state machine
// reporting IDLE.
//
// A confirm timeout that lands in the same millisecond as the laser finally
// answering is the easy way to hit it.
// ----------------------------------------------------------------------------

void abortSequence()
{
  noInterrupts();

  stopTimer1();

  Timer1Event = TIMER_NONE;

  State = IDLE;

  PendingNextCycle = false;

  digitalWrite(LASER_SIGNAL_PIN, LASER_SIGNAL_IDLE);
  digitalWrite(LASER_ENABLE_PIN, LOW);

  resetShutter();

  interrupts();
}


// ----------------------------------------------------------------------------
// BEGIN ONE CYCLE
//
// Checks the camera, then fires the laser signal pulse.
// Always called from loop(), never from an ISR.
//
// Returns false if the cycle could not start; the sequence is aborted in that
// case and the reason has already been reported.
// ----------------------------------------------------------------------------

bool beginCycle()
{
  if (digitalRead(CAMERA_CAPTURING_PIN) != CAMERA_ACTIVE_LEVEL) {
    abortSequence();

    Serial.println("ERROR CAMERA NOT CAPTURING");

    return false;
  }

  // Arm before pulsing. If the laser answers immediately, the confirm
  // interrupt must already see the right state or the edge is lost.
  State = WAITING_FOR_LASER_CONFIRM;

  ConfirmWaitStartedMs = millis();

  PendingCycleNumber = CurrentCycle + 1;
  PendingCycleStarted = true;

  logEvent(LOG_CYCLE_BEGIN);

  // laser_signal idles HIGH and dips LOW to fire.
  //
  // Nothing goes between these three lines. A log push here would widen the
  // pulse by however long it took.
  digitalWrite(LASER_SIGNAL_PIN, LASER_SIGNAL_FIRE);

  delayMicroseconds((unsigned int)LaserSignalPulseUs);

  digitalWrite(LASER_SIGNAL_PIN, LASER_SIGNAL_IDLE);

  // Stamped at the trailing edge, so the gap from CYCLE_BEGIN is the
  // measured pulse width plus a little call overhead.
  logEvent(LOG_PULSE);

  return true;
}


// ----------------------------------------------------------------------------
// START A SEQUENCE
// ----------------------------------------------------------------------------

void startSequence()
{
  if (isRunning()) {
    Serial.println("ERROR BUSY");

    return;
  }

  CurrentCycle = 0;

  Timer1Event = TIMER_NONE;
  PendingNextCycle = false;

  resetLog();

  stopTimer1();
  resetShutter();

  digitalWrite(LASER_ENABLE_PIN, HIGH);

  // Announce only once the first cycle is actually underway, so a camera
  // failure reports the error alone rather than STARTED followed by ERROR.
  if (beginCycle()) {
    Serial.println("STARTED");
  }
}


// ----------------------------------------------------------------------------
// IS A SEQUENCE CURRENTLY RUNNING
// ----------------------------------------------------------------------------

bool isRunning()
{
  return State == WAITING_FOR_LASER_CONFIRM ||
         State == WAITING_FOR_DELAY ||
         State == CAPTURING;
}


// ----------------------------------------------------------------------------
// PARSE AN UNSIGNED ARGUMENT
//
// Returns false unless the whole argument is a plain non-negative integer.
// ----------------------------------------------------------------------------

bool parseUnsigned(String value, unsigned long &out)
{
  value.trim();

  if (value.length() == 0) {
    return false;
  }

  for (unsigned int i = 0; i < value.length(); i++) {
    if (!isDigit(value[i])) {
      return false;
    }
  }

  unsigned long parsed = value.toInt();

  // Rejects anything that overflowed toInt(), e.g. "99999999999".
  if (value != String(parsed)) {
    return false;
  }

  out = parsed;

  return true;
}


// ----------------------------------------------------------------------------
// COMMANDS
// ----------------------------------------------------------------------------

const String GET_DELAY_COMMAND = "GET DELAY";
const String SET_DELAY_COMMAND = "SET DELAY";

const String GET_CAPTURE_COMMAND = "GET CAPTURE";
const String SET_CAPTURE_COMMAND = "SET CAPTURE";

const String GET_CYCLE_COUNT_COMMAND = "GET CYCLE_COUNT";
const String SET_CYCLE_COUNT_COMMAND = "SET CYCLE_COUNT";

const String GET_PULSE_COMMAND = "GET PULSE";
const String SET_PULSE_COMMAND = "SET PULSE";

const String GET_CONFIRM_TIMEOUT_COMMAND = "GET CONFIRM_TIMEOUT";
const String SET_CONFIRM_TIMEOUT_COMMAND = "SET CONFIRM_TIMEOUT";

const String GET_VERBOSE_COMMAND = "GET VERBOSE";
const String SET_VERBOSE_COMMAND = "SET VERBOSE";


// ----------------------------------------------------------------------------
// HELP
//
// Lists every command the parser accepts.
//
// Body lines are indented and the block is bracketed by OK HELP and
// OK HELP END, so a host reading line by line can swallow the whole thing
// without having to recognise each entry.
//
// The limits are built from the constants rather than written into the text,
// so they cannot drift out of step with what the parser actually enforces.
//
// The text lives in flash via F(). It is about 700 bytes, which would be a
// tenth of the Mega's SRAM if it sat in RAM with the String work below.
// ----------------------------------------------------------------------------

const uint8_t HELP_TEXT_COLUMN = 28;
const uint8_t HELP_DESCRIPTION_COLUMN = 42;

void padTo(String &line, uint8_t column)
{
  while (line.length() < column) {
    line += ' ';
  }
}

void printHelpLine(const __FlashStringHelper *usage,
                   const __FlashStringHelper *text)
{
  String line = String("  ") + usage;

  padTo(line, HELP_TEXT_COLUMN);

  Serial.println(line + text);
}

void printHelpField(const __FlashStringHelper *usage,
                    const String &limits,
                    const __FlashStringHelper *description)
{
  String line = String("  ") + usage;

  padTo(line, HELP_TEXT_COLUMN);

  line += limits;

  padTo(line, HELP_DESCRIPTION_COLUMN);

  Serial.println(line + description);
}

void printHelp()
{
  Serial.println(F("OK HELP"));

  printHelpLine(F("START"), F("run CYCLE_COUNT cycles"));
  printHelpLine(F("ABORT | STOP"), F("stop, drop laser_enable, free shutter"));
  printHelpLine(F("STATUS"), F("state, cycle progress, camera level"));
  printHelpLine(F("INFO"), F("reprint the boot banner"));
  printHelpLine(F("HELP | ?"), F("this list"));

  Serial.println();

  printHelpField(F("SET DELAY <us>"),
                 String(MIN_US) + "-" + MAX_US + "us",
                 F("laser_confirm to shutter open"));

  printHelpField(F("SET CAPTURE <us>"),
                 String(MIN_US) + "-" + MAX_US + "us",
                 F("shutter gate width"));

  printHelpField(F("SET CYCLE_COUNT <n>"),
                 String(MIN_CYCLE_COUNT) + "-" + MAX_CYCLE_COUNT,
                 F("cycles per START"));

  printHelpField(F("SET PULSE <us>"),
                 String(MIN_PULSE_US) + "-" + MAX_PULSE_US + "us",
                 F("laser_signal low-pulse width"));

  printHelpField(F("SET CONFIRM_TIMEOUT <ms>"),
                 String(MIN_CONFIRM_TIMEOUT_MS) + "-" + MAX_CONFIRM_TIMEOUT_MS + "ms",
                 F("confirm wait, 0 disables"));

  printHelpField(F("SET VERBOSE <0|1>"),
                 String("0-1"),
                 F("measured event log"));

  Serial.println();

  Serial.println(F("  GET reads back any of the six settings, e.g. GET DELAY."));
  Serial.println(F("  SET, HELP and INFO are rejected with ERROR BUSY while running."));

  Serial.println(F("OK HELP END"));
}


// ----------------------------------------------------------------------------
// BOOT BANNER
//
// Printed once from setup(), and again on demand via INFO. Answers the three
// questions asked at the start of every bring-up session: what is running on
// this board, how is it wired, and what is it currently set to.
//
// Bracketed by INFO and INFO END on the same principle as the HELP block, so
// a host reading line by line can swallow the whole thing without recognising
// each row. READY still follows on boot and is still the last line printed,
// so an existing host that waits for READY is unaffected by any of this.
//
// The banner is deliberately not the boot marker. READY is. That keeps INFO
// honest when it reprints the same block mid-session, and it means a stray
// READY still tells a host the board reset under it.
//
// Text is held in flash with F(). The values are what force the String work,
// and they are transient.
// ----------------------------------------------------------------------------

const uint8_t BANNER_VALUE_COLUMN = 22;
const uint8_t BANNER_NOTE_COLUMN = 27;

void printBannerRow(const __FlashStringHelper *name, const String &value)
{
  String line = String("    ") + name;

  padTo(line, BANNER_VALUE_COLUMN);

  Serial.println(line + value);
}

void printBannerPin(const __FlashStringHelper *name,
                    uint8_t pin,
                    const __FlashStringHelper *note)
{
  String line = String("    ") + name;

  padTo(line, BANNER_VALUE_COLUMN);

  line += pin;

  padTo(line, BANNER_NOTE_COLUMN);

  Serial.println(line + note);
}

void printBanner()
{
  Serial.println(F("INFO"));

  Serial.println(String("  ") + FLASH_STR(FIRMWARE_NAME));

  Serial.println(String("  version ") + FLASH_STR(FIRMWARE_VERSION) +
                 "   built " + F(__DATE__) + " " + F(__TIME__));

  Serial.println(String("  Arduino Mega 2560   serial ") + SERIAL_BAUD + " 8N1");

  Serial.println();

  Serial.println(F("  Pins"));

  printBannerPin(F("laser_enable"), LASER_ENABLE_PIN,
                 F("output, held HIGH for the run"));

  printBannerPin(F("laser_signal"), LASER_SIGNAL_PIN,
                 F("output, idles HIGH, pulses LOW"));

  printBannerPin(F("laser_confirm"), LASER_CONFIRM_PIN,
                 F("input, interrupt on RISING"));

  printBannerPin(F("camera_capturing"), CAMERA_CAPTURING_PIN,
                 F("input, active HIGH"));

  printBannerPin(F("shutter_enable"), SHUTTER_ENABLE_PIN,
                 F("output, OC1A, driven by Timer1"));

  Serial.println();

  Serial.println(F("  Settings"));

  printBannerRow(F("DELAY"), durationDetail(DelayTime));
  printBannerRow(F("CAPTURE"), durationDetail(CaptureTime));
  printBannerRow(F("CYCLE_COUNT"), String(CycleCount));
  printBannerRow(F("PULSE"), String(LaserSignalPulseUs) + "us");

  printBannerRow(F("CONFIRM_TIMEOUT"),
                 ConfirmTimeoutMs == 0 ? String("0 (disabled)")
                                       : String(ConfirmTimeoutMs) + "ms");

  printBannerRow(F("VERBOSE"), VerboseEnabled ? F("1 (event log on)") : F("0"));

  Serial.println();

  // Read live rather than assumed. A miswired or idle camera shows up here at
  // boot instead of as an ERROR CAMERA NOT CAPTURING on the first START.
  Serial.println(F("  Inputs now"));

  printBannerRow(F("camera_capturing"),
                 digitalRead(CAMERA_CAPTURING_PIN) == CAMERA_ACTIVE_LEVEL
                     ? F("CAPTURING") : F("IDLE"));

  printBannerRow(F("laser_confirm"),
                 digitalRead(LASER_CONFIRM_PIN) == HIGH ? F("HIGH") : F("LOW"));

  Serial.println();

  Serial.println(F("  HELP lists the commands, STATUS reports live state."));

  Serial.println(F("INFO END"));
}


// ----------------------------------------------------------------------------
// HANDLE A "SET <duration>" COMMAND
// ----------------------------------------------------------------------------

void handleSetDuration(const String &argument,
                       const char *label,
                       Duration &target,
                       String (*describe)())
{
  unsigned long value;

  if (!parseUnsigned(argument, value)) {
    Serial.println(String("ERROR ") + label + " VALUE");

    return;
  }

  Duration resolved;

  if (!resolveDuration(value, resolved)) {
    Serial.println(String("ERROR ") + label + " RANGE " + MIN_US + "-" + MAX_US + "us");

    return;
  }

  target = resolved;

  Serial.println(describe());
}


// ----------------------------------------------------------------------------
// PARSE AND RANGE-CHECK A PLAIN SCALAR ARGUMENT
//
// Reports the failure itself and returns false; on success the value is in
// out and the caller stores it wherever it belongs.
// ----------------------------------------------------------------------------

bool parseScalarArg(const String &argument,
                    const char *label,
                    unsigned long minValue,
                    unsigned long maxValue,
                    unsigned long &out)
{
  unsigned long value;

  if (!parseUnsigned(argument, value)) {
    Serial.println(String("ERROR ") + label + " VALUE");

    return false;
  }

  if (value < minValue || value > maxValue) {
    Serial.println(String("ERROR ") + label + " RANGE " + minValue + "-" + maxValue);

    return false;
  }

  out = value;

  return true;
}


// ----------------------------------------------------------------------------
// PROCESS A COMMAND
// ----------------------------------------------------------------------------

void processCommand(String command)
{
  command.trim();
  command.toUpperCase();

  // --------------------------------------------------------------------------
  // START
  // --------------------------------------------------------------------------
  if (command == "START") {
    startSequence();

    return;
  }

  // --------------------------------------------------------------------------
  // ABORT
  //
  // Not in the spec, but a laser controller needs a way to stop.
  // --------------------------------------------------------------------------
  if (command == "ABORT" || command == "STOP") {
    abortSequence();

    Serial.println("OK ABORTED");

    return;
  }

  // --------------------------------------------------------------------------
  // STATUS
  // --------------------------------------------------------------------------
  if (command == "STATUS") {
    Serial.print("OK STATUS ");
    Serial.print(isRunning() ? "RUNNING" : (State == COMPLETE ? "COMPLETE" : "IDLE"));
    Serial.print(" CYCLE ");
    Serial.print(CurrentCycle);
    Serial.print("/");
    Serial.print(CycleCount);
    Serial.print(" CAMERA ");
    Serial.println(
        digitalRead(CAMERA_CAPTURING_PIN) == CAMERA_ACTIVE_LEVEL ? "CAPTURING" : "IDLE");

    return;
  }

  // --------------------------------------------------------------------------
  // HELP
  //
  // Rejected while running for a different reason than SET is. The block is
  // around 700 bytes, which is about 700 ms at 9600 baud once the 64 byte
  // transmit buffer backs up, and loop() is what starts each next cycle.
  // Blocking it for that long would stretch the gap between cycles.
  // --------------------------------------------------------------------------
  if (command == "HELP" || command == "?") {
    if (isRunning()) {
      Serial.println("ERROR BUSY");

      return;
    }

    printHelp();

    return;
  }

  // --------------------------------------------------------------------------
  // INFO
  //
  // Reprints the boot banner. A host that opened the port after boot missed
  // it, which is the common case, so it needs to be askable for.
  //
  // Held to the same BUSY rule as HELP, and for the same reason: the block is
  // long enough at 9600 baud that printing it would stretch the gap between
  // cycles.
  // --------------------------------------------------------------------------
  if (command == "INFO") {
    if (isRunning()) {
      Serial.println("ERROR BUSY");

      return;
    }

    printBanner();

    return;
  }

  // --------------------------------------------------------------------------
  // GETs
  // --------------------------------------------------------------------------
  if (command == GET_DELAY_COMMAND) {
    Serial.println(getDelayResponseString());

    return;
  }

  if (command == GET_CAPTURE_COMMAND) {
    Serial.println(getCaptureResponseString());

    return;
  }

  if (command == GET_CYCLE_COUNT_COMMAND) {
    Serial.println(getCycleCountResponseString());

    return;
  }

  if (command == GET_PULSE_COMMAND) {
    Serial.println(getPulseResponseString());

    return;
  }

  if (command == GET_CONFIRM_TIMEOUT_COMMAND) {
    Serial.println(getConfirmTimeoutResponseString());

    return;
  }

  if (command == GET_VERBOSE_COMMAND) {
    Serial.println(getVerboseResponseString());

    return;
  }

  // --------------------------------------------------------------------------
  // SETs
  //
  // Rejected while running. That is what makes it safe for the Timer1 ISR to
  // read the configuration without volatile or interrupt guards.
  // --------------------------------------------------------------------------
  if (command.startsWith("SET ")) {
    if (isRunning()) {
      Serial.println("ERROR BUSY");

      return;
    }

    if (command.startsWith(SET_DELAY_COMMAND)) {
      handleSetDuration(command.substring(SET_DELAY_COMMAND.length()),
                        "DELAY", DelayTime, getDelayResponseString);

      return;
    }

    if (command.startsWith(SET_CAPTURE_COMMAND)) {
      handleSetDuration(command.substring(SET_CAPTURE_COMMAND.length()),
                        "CAPTURE", CaptureTime, getCaptureResponseString);

      return;
    }

    if (command.startsWith(SET_CYCLE_COUNT_COMMAND)) {
      unsigned long value;

      if (parseScalarArg(command.substring(SET_CYCLE_COUNT_COMMAND.length()),
                         "CYCLE_COUNT", MIN_CYCLE_COUNT, MAX_CYCLE_COUNT, value)) {
        CycleCount = (uint16_t)value;

        Serial.println(getCycleCountResponseString());
      }

      return;
    }

    if (command.startsWith(SET_PULSE_COMMAND)) {
      unsigned long value;

      if (parseScalarArg(command.substring(SET_PULSE_COMMAND.length()),
                         "PULSE", MIN_PULSE_US, MAX_PULSE_US, value)) {
        LaserSignalPulseUs = value;

        Serial.println(getPulseResponseString());
      }

      return;
    }

    if (command.startsWith(SET_CONFIRM_TIMEOUT_COMMAND)) {
      unsigned long value;

      if (parseScalarArg(command.substring(SET_CONFIRM_TIMEOUT_COMMAND.length()),
                         "CONFIRM_TIMEOUT",
                         MIN_CONFIRM_TIMEOUT_MS, MAX_CONFIRM_TIMEOUT_MS, value)) {
        ConfirmTimeoutMs = value;

        Serial.println(getConfirmTimeoutResponseString());
      }

      return;
    }

    if (command.startsWith(SET_VERBOSE_COMMAND)) {
      unsigned long value;

      if (parseScalarArg(command.substring(SET_VERBOSE_COMMAND.length()),
                         "VERBOSE", 0, 1, value)) {
        VerboseEnabled = (value != 0);

        // Drop anything queued under the previous setting so timestamps in
        // the log always belong to the run being watched.
        resetLog();

        Serial.println(getVerboseResponseString());
      }

      return;
    }
  }

  Serial.println("ERROR UNKNOWN COMMAND");
}


// ----------------------------------------------------------------------------
// DRAIN WORK THE ISRs HANDED BACK TO US
// ----------------------------------------------------------------------------

void servicePending()
{
  // Arm the next cycle before printing anything. Serial writes are slow
  // enough to show up as inter-cycle jitter if they happen first.
  if (PendingNextCycle) {
    PendingNextCycle = false;

    beginCycle();
  }

  if (PendingCycleStarted) {
    noInterrupts();

    uint16_t cycle = PendingCycleNumber;
    PendingCycleStarted = false;

    interrupts();

    Serial.print("CYCLE ");
    Serial.println(cycle);
  }

  if (PendingDone) {
    PendingDone = false;

    Serial.println("DONE");
  }

  // Laser never answered. A timeout of 0 means the check is disabled.
  if (ConfirmTimeoutMs > 0 &&
      State == WAITING_FOR_LASER_CONFIRM &&
      millis() - ConfirmWaitStartedMs > ConfirmTimeoutMs) {
    abortSequence();

    Serial.println("ERROR LASER CONFIRM TIMEOUT");
  }

  // Last, so the next cycle is already armed before we spend time on the
  // serial write.
  if (VerboseEnabled) {
    drainLog();
  }
}


// ----------------------------------------------------------------------------
// MAIN LOOP
// ----------------------------------------------------------------------------

void loop()
{
  servicePending();

  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');

    processCommand(command);
  }
}
