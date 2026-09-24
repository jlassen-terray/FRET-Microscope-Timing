// ----------------------------------------------------------------------------
// TICKS PER US; Arduino Timer 1 runs at 16 MHz w/o prescaler
// ----------------------------------------------------------------------------
const uint8_t TICKS_PER_US = 16;

// ----------------------------------------------------------------------------
// TIMING RANGE
//
// Timer1 is 16 bit, so OCR1A tops out at 65535 ticks. With no prescaler that
// is 65535 / 16 = 4095 us. Values above this cannot be represented and are
// rejected rather than silently truncated.
// ----------------------------------------------------------------------------
const uint16_t MIN_US = 1;
const uint16_t MAX_US = 4095;

// ----------------------------------------------------------------------------
// DELAY CONFIGURATION
// ----------------------------------------------------------------------------
volatile uint16_t _delayUs = 1;
volatile uint16_t DelayTicks = 1 * TICKS_PER_US;

// ----------------------------------------------------------------------------
// SET DELAY MICROSECONDS; Calculates Delay Clock Ticks
//
// Returns false if the value is out of range.
// ----------------------------------------------------------------------------
bool setDelayUs(unsigned long delayUs) {
  if (delayUs < MIN_US || delayUs > MAX_US) {
    return false;
  }

  // The ISR reads both of these. A 16 bit store is two instructions on AVR,
  // so block interrupts to stop the ISR observing a half-written value.
  noInterrupts();

  _delayUs = (uint16_t)delayUs;
  DelayTicks = (uint16_t)delayUs * TICKS_PER_US;

  interrupts();

  return true;
}

// ----------------------------------------------------------------------------
// PRINTABLE DELAY DETAILS
// ----------------------------------------------------------------------------
String getDelayResponseString() {
  return String("OK DELAY ") + _delayUs + "us (" + DelayTicks + " ticks)";
}

// ----------------------------------------------------------------------------
// EXPOSURE CONFIGURATION
// ----------------------------------------------------------------------------
volatile uint16_t _exposureUs = 50;
volatile uint16_t ExposureTicks = 50 * TICKS_PER_US;

// ----------------------------------------------------------------------------
// SET EXPOSURE MICROSECONDS; Calculates Exposure Clock Ticks
//
// Returns false if the value is out of range.
// ----------------------------------------------------------------------------
bool setExposureUs(unsigned long exposureUs) {
  if (exposureUs < MIN_US || exposureUs > MAX_US) {
    return false;
  }

  noInterrupts();

  _exposureUs = (uint16_t)exposureUs;
  ExposureTicks = (uint16_t)exposureUs * TICKS_PER_US;

  interrupts();

  return true;
}

// ----------------------------------------------------------------------------
// PRINTABLE EXPOSURE DETAILS
// ----------------------------------------------------------------------------
String getExposureResponseString() {
  return String("OK EXPOSURE ") + _exposureUs + "us (" + ExposureTicks + " ticks)";
}

// ----------------------------------------------------------------------------
// CYCLE COUNT CONFIGURATION
// ----------------------------------------------------------------------------
const uint16_t MIN_CYCLE_COUNT = 1;
const uint16_t MAX_CYCLE_COUNT = 65535;

volatile uint16_t CycleCount = 1;

// ----------------------------------------------------------------------------
// PRINTABLE CYCLE COUNT DETAILS
// ----------------------------------------------------------------------------
String getCycleCountResponseString() {
  return String("OK CYCLE COUNT ") + CycleCount;
}

// ----------------------------------------------------------------------------
// LASER RETURN PIN
// ----------------------------------------------------------------------------
const uint8_t LASER_RETURN_PIN = 2;

// ----------------------------------------------------------------------------
  // Timer1 OC1A
  //
  // OC1A is Arduino Mega digital pin 11.
  //
  // Configure pin 11 as an output.
  // ----------------------------------------------------------------------------
const uint8_t SHUTTER_PIN = 11;

// ----------------------------------------------------------------------------
// SEQUENCE STATE
// ----------------------------------------------------------------------------
enum SequenceState
{
  IDLE,
  WAITING_FOR_LASER_RETURN,
  WAITING_FOR_DELAY,
  EXPOSING,
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

// ----------------------------------------------------------------------------
// SYSTEM ENABLE
// ----------------------------------------------------------------------------
volatile bool SystemEnabled = false;

// ----------------------------------------------------------------------------
// DEFERRED SERIAL OUTPUT
//
// Nothing prints from an ISR. At 9600 baud a single line blocks for roughly a
// millisecond, which is far longer than the exposures being timed here. The
// ISRs raise these flags and loop() does the printing.
// ----------------------------------------------------------------------------
volatile bool PendingCycleStarted = false;
volatile uint16_t PendingCycleNumber = 0;
volatile bool PendingDone = false;

// ----------------------------------------------------------------------------
// SHUTTER RESET
//
// OC1A is driven in hardware toggle mode, so the shutter state depends on how
// many compare matches have occurred. An odd number leaves it open, so there
// has to be a way to force it back to closed.
//
// Writing the port will not do it. While a COM1A bit is set the waveform
// generator owns the pin, and the value it drives lives in the OC1A register,
// which keeps its state across a TCCR1A write. Disconnecting the output,
// writing the port low and reconnecting only holds the pin low for the few
// cycles it is disconnected: the moment toggle mode comes back, the stale OC1A
// register reappears on the pin and the shutter is open again.
//
// The supported way to set OC1A directly is a forced compare. Select
// "clear on compare match" and strobe FOC1A: the compare output logic applies
// the COM1A setting to the OC1A register without raising OCF1A and without
// resetting the counter. Then restore toggle mode for the next exposure.
// ----------------------------------------------------------------------------
void resetShutter() {
  TCCR1A = (1 << COM1A1);
  TCCR1C = (1 << FOC1A);

  TCCR1A = (1 << COM1A0);
}

// ----------------------------------------------------------------------------
// TIMER 1 CONTROL
// ----------------------------------------------------------------------------
void armTimer1(uint16_t ticks) {
  TCNT1 = 0;

  OCR1A = ticks;

  // Discard a stale compare match, which would otherwise fire immediately.
  TIFR1 = (1 << OCF1A);

  // CTC mode, no prescaler.
  TCCR1B = (1 << WGM12) | (1 << CS10);
}

void stopTimer1() {
  TCCR1B &= ~((1 << CS12) | (1 << CS11) | (1 << CS10));
}

// ----------------------------------------------------------------------------
// SETUP
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);

  pinMode(LASER_RETURN_PIN, INPUT_PULLUP);

  pinMode(SHUTTER_PIN, OUTPUT);

  digitalWrite(SHUTTER_PIN, LOW);

  // --------------------------------------------------------------------------
  // Configure Timer1
  // --------------------------------------------------------------------------

  // Clear Timer1 configuration.
  TCCR1A = 0;
  TCCR1B = 0;

  // Reset counter.
  TCNT1 = 0;

  // --------------------------------------------------------------------------
  // Configure OC1A
  //
  // COM1A0 = 1
  //
  // Timer1 hardware will toggle OC1A whenever
  // the timer reaches OCR1A.
  // --------------------------------------------------------------------------

  TCCR1A = (1 << COM1A0);

  // --------------------------------------------------------------------------
  // Enable the Timer1 Compare A interrupt.
  //
  // Without this the ISR below never runs and the sequence cannot advance.
  // --------------------------------------------------------------------------

  TIMSK1 = (1 << OCIE1A);

  // --------------------------------------------------------------------------
  // Laser return interrupt.
  //
  // INPUT_PULLUP means idle is HIGH, so the laser asserting its return signal
  // is a falling edge.
  // --------------------------------------------------------------------------

  attachInterrupt(
      digitalPinToInterrupt(LASER_RETURN_PIN),
      laserReturnISR,
      FALLING
  );
}

// ----------------------------------------------------------------------------
// LASER RETURN INTERRUPT; Advances the sequence once the laser reports back
//
// Starts the configured delay. When it expires the Timer1 hardware toggles
// OC1A, opening the shutter without any software in the path.
// ----------------------------------------------------------------------------
void laserReturnISR() {
  if (State != WAITING_FOR_LASER_RETURN) {
    return;
  }

  State = WAITING_FOR_DELAY;

  Timer1Event = TIMER_SHUTTER_OPEN;

  armTimer1(DelayTicks);
}

// ----------------------------------------------------------------------------
// TIMER 1 COMPARE-A INTERRUPT
//
// The shutter transition has ALREADY happened in hardware by the time this
// runs. This ISR only sequences what comes next.
// ----------------------------------------------------------------------------
ISR(TIMER1_COMPA_vect) {
  switch (Timer1Event)
  {
    // ------------------------------------------------------------------------
    // OPEN SHUTTER
    // ------------------------------------------------------------------------
    case TIMER_SHUTTER_OPEN:
      // Hardware has toggled OC1A high; the shutter is now open.

      State = EXPOSING;

      Timer1Event = TIMER_SHUTTER_CLOSE;

      // Timer stays running; re-point it at the exposure length.
      armTimer1(ExposureTicks);

      break;

    // ------------------------------------------------------------------------
    // CLOSE SHUTTER
    // ------------------------------------------------------------------------
    case TIMER_SHUTTER_CLOSE:
      // Hardware has toggled OC1A low; the shutter is now closed.

      stopTimer1();

      Timer1Event = TIMER_NONE;

      CurrentCycle++;

      if (CurrentCycle >= CycleCount) {
        State = COMPLETE;

        PendingDone = true;
      } else {
        // Start next cycle.
        State = IDLE;

        startLaserCycle();
      }

      break;

    // ------------------------------------------------------------------------
    // A MATCH NOBODY ASKED FOR
    //
    // The timer is running with no transition expected, so it is free-running
    // in CTC and will keep toggling the shutter every OCR1A. Shut it down and
    // close the shutter rather than leaving the pin flapping.
    // ------------------------------------------------------------------------
    default:
      stopTimer1();

      resetShutter();

      break;
  }
}

// ----------------------------------------------------------------------------
// START ONE LASER/EXPOSURE CYCLE
// ----------------------------------------------------------------------------
void startLaserCycle() {
  if (SystemEnabled == false) {
    return;
  }

  // TODO: Turn laser on

  State = WAITING_FOR_LASER_RETURN;

  PendingCycleNumber = CurrentCycle + 1;
  PendingCycleStarted = true;
}

// ----------------------------------------------------------------------------
// START A FULL CAPTURE SEQUENCE OF CycleCount CYCLES
// ----------------------------------------------------------------------------
void startSequence() {
  if (SystemEnabled == false) {
    Serial.println("ERROR NOT ENABLED");

    return;
  }

  if (State != IDLE && State != COMPLETE) {
    Serial.println("ERROR BUSY");

    return;
  }

  // Rewind the cycle counter. Without this a second START after COMPLETE
  // finishes instantly, because CurrentCycle is still at CycleCount.
  CurrentCycle = 0;

  Timer1Event = TIMER_NONE;

  stopTimer1();

  resetShutter();

  startLaserCycle();

  Serial.println("STARTED");
}

// ----------------------------------------------------------------------------
// ABORT; Returns all outputs to a safe state
//
// Runs with interrupts off. laserReturnISR re-arms Timer1 whenever it finds
// State still set to WAITING_FOR_LASER_RETURN, so tearing down with interrupts
// on leaves a window between stopTimer1() and State = IDLE where a late return
// edge starts the timer back up behind us. Nothing stops it after that: the
// delay expires, the hardware toggles the shutter open, and the compare ISR
// finds no pending event and does nothing.
// ----------------------------------------------------------------------------
void abortSequence() {
  noInterrupts();

  stopTimer1();

  Timer1Event = TIMER_NONE;

  State = IDLE;

  CurrentCycle = 0;

  resetShutter();

  interrupts();

  // TODO: Turn laser off
}

// ----------------------------------------------------------------------------
// PARSE AN UNSIGNED ARGUMENT
//
// Returns false unless the whole argument is a plain non-negative integer.
// ----------------------------------------------------------------------------
bool parseUnsigned(String value, unsigned long &out) {
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
// CONFIGURABLE COMMANDS
// ----------------------------------------------------------------------------
const String GET_DELAY_COMMAND = "GET DELAY";
const String SET_DELAY_COMMAND = "SET DELAY";

const String GET_EXPOSURE_COMMAND = "GET EXPOSURE";
const String SET_EXPOSURE_COMMAND = "SET EXPOSURE";

const String GET_CYCLE_COUNT_COMMAND = "GET CYCLE_COUNT";
const String SET_CYCLE_COUNT_COMMAND = "SET CYCLE_COUNT";

// ----------------------------------------------------------------------------
// PROCESS A COMMAND
// ----------------------------------------------------------------------------
void processCommand(String command) {
  command.trim();

  // --------------------------------------------------------------------------
  // ENABLE
  // --------------------------------------------------------------------------
  if (command == "ENABLE") {
    SystemEnabled = true;

    Serial.println("OK ENABLED");

    return;
  }

  // --------------------------------------------------------------------------
  // DISABLE
  // --------------------------------------------------------------------------
  if (command == "DISABLE") {
    SystemEnabled = false;

    abortSequence();

    Serial.println("OK DISABLED");

    return;
  }

  // --------------------------------------------------------------------------
  // START CAPTURE SEQUENCE
  // --------------------------------------------------------------------------
  if (command == "START") {
    startSequence();

    return;
  }

  // --------------------------------------------------------------------------
  // GET DELAY
  // --------------------------------------------------------------------------
  if (command == GET_DELAY_COMMAND) {
    Serial.println(getDelayResponseString());

    return;
  }

  // --------------------------------------------------------------------------
  // SET DELAY
  // --------------------------------------------------------------------------
  if (command.startsWith(SET_DELAY_COMMAND)) {
    unsigned long value;

    if (!parseUnsigned(command.substring(SET_DELAY_COMMAND.length()), value)) {
      Serial.println("ERROR DELAY VALUE");

    } else if (!setDelayUs(value)) {
      Serial.println(String("ERROR DELAY RANGE ") + MIN_US + "-" + MAX_US + "us");

    } else {
      Serial.println(getDelayResponseString());
    }

    return;
  }

  // --------------------------------------------------------------------------
  // GET EXPOSURE
  // --------------------------------------------------------------------------
  if (command == GET_EXPOSURE_COMMAND) {
    Serial.println(getExposureResponseString());

    return;
  }

  // --------------------------------------------------------------------------
  // SET EXPOSURE
  // --------------------------------------------------------------------------
  if (command.startsWith(SET_EXPOSURE_COMMAND)) {
    unsigned long value;

    if (!parseUnsigned(command.substring(SET_EXPOSURE_COMMAND.length()), value)) {
      Serial.println("ERROR EXPOSURE VALUE");

    } else if (!setExposureUs(value)) {
      Serial.println(String("ERROR EXPOSURE RANGE ") + MIN_US + "-" + MAX_US + "us");

    } else {
      Serial.println(getExposureResponseString());
    }

    return;
  }

  // --------------------------------------------------------------------------
  // GET CYCLE COUNT
  // --------------------------------------------------------------------------
  if (command == GET_CYCLE_COUNT_COMMAND) {
    Serial.println(getCycleCountResponseString());

    return;
  }

  // --------------------------------------------------------------------------
  // SET CYCLE COUNT
  // --------------------------------------------------------------------------
  if (command.startsWith(SET_CYCLE_COUNT_COMMAND)) {
    unsigned long value;

    if (!parseUnsigned(command.substring(SET_CYCLE_COUNT_COMMAND.length()), value)) {
      Serial.println("ERROR CYCLE COUNT VALUE");

    } else if (value < MIN_CYCLE_COUNT || value > MAX_CYCLE_COUNT) {
      Serial.println(
          String("ERROR CYCLE COUNT RANGE ") + MIN_CYCLE_COUNT + "-" + MAX_CYCLE_COUNT);

    } else {
      noInterrupts();

      CycleCount = (uint16_t)value;

      interrupts();

      Serial.println(getCycleCountResponseString());
    }

    return;
  }

  Serial.println("ERROR UNKNOWN COMMAND");
}

// ----------------------------------------------------------------------------
// DRAIN ANYTHING THE ISRs ASKED US TO PRINT
// ----------------------------------------------------------------------------
void flushPendingOutput() {
  if (PendingCycleStarted) {
    noInterrupts();

    uint16_t cycle = PendingCycleNumber;

    PendingCycleStarted = false;

    interrupts();

    Serial.print("LASER ");
    Serial.println(cycle);
  }

  if (PendingDone) {
    PendingDone = false;

    Serial.println("DONE");
  }
}

// ----------------------------------------------------------------------------
// MAIN LOOP
// ----------------------------------------------------------------------------
void loop() {
  flushPendingOutput();

  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');

    processCommand(command);
  }
}
