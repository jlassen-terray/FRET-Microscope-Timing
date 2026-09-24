#include "Sequence.h"

#include "Config.h"
#include "LogRing.h"
#include "Settings.h"
#include "Timer1.h"


volatile SequenceState State = IDLE;

volatile uint16_t CurrentCycle = 0;

// Which shutter transition the next Timer1 compare match represents.
enum TimerEvent
{
  TIMER_NONE,
  TIMER_SHUTTER_OPEN,
  TIMER_SHUTTER_CLOSE
};

static volatile TimerEvent Timer1Event = TIMER_NONE;

// Set when a cycle begins, used to time out a missing laser_confirm.
static volatile unsigned long ConfirmWaitStartedMs = 0;

// HaveConfirmEdge keeps the first edge after boot from being measured against
// a zero stamp, which would reject it while micros() is still small.
static volatile unsigned long LastConfirmEdgeUs = 0;
static volatile bool HaveConfirmEdge = false;

// The line's level as read after the most recent edge. Once a bounce train
// ends, that is the level the line settled at.
static volatile uint8_t ConfirmSettledLevel = LOW;

// digitalRead() costs microseconds; the port read costs a few cycles.
static volatile uint8_t *ConfirmPinInput;
static uint8_t ConfirmPinMask;

// Taken in laserConfirmISR, logged from the compare ISR; see the call.
static volatile uint16_t ConfirmLogTicks = 0;


// Nothing prints from an ISR. At 9600 baud a byte costs 1.04 ms on the wire, so
// a 40-character line is roughly 40 ms -- four orders of magnitude longer than
// the windows being timed. The ISR raises a flag and servicePending() does the
// work.
//
// PendingNextCycle matters for a second reason: starting a cycle reads the
// camera input, pulses laser_signal with a blocking delay, and may need to
// report an error. None of that belongs in an interrupt.

static volatile bool PendingDone = false;
static volatile bool PendingNextCycle = false;


// CurrentCycle counts cycles *finished*, so the one in progress is one past it.
// DONE is the exception and calls logEventForCycle() directly; see the call.
static void logEvent(uint8_t event)
{
  logEventForCycle(event, CurrentCycle + 1);
}


static inline uint8_t readConfirmPin()
{
  return (*ConfirmPinInput & ConfirmPinMask) ? HIGH : LOW;
}


// Start the configured delay; when it expires the Timer1 hardware sets OC1A
// and the shutter gate opens with no software in the path. Forced inline so
// the laser path pays no call overhead ahead of armTimer1().
static inline __attribute__((always_inline)) void startDelay()
{
  State = WAITING_FOR_DELAY;

  Timer1Event = TIMER_SHUTTER_OPEN;

  // Timer1 is stopped here, so the mode can change before the window starts.
  openShutterOnMatch();

  armTimer1(DelayTime);

  // Stamp only; the push waits for the compare ISR. Anything spent here
  // delays that ISR, and so the capture re-arm, and so the close edge. Taken
  // whether or not VERBOSE is on, so turning it on changes nothing here.
  ConfirmLogTicks = readLogClock();
}


// A contact bounces on press *and* release, and each bounce train holds both
// edge directions, so no single-edge interrupt can tell them apart: a FALLING
// interrupt fires on both trains. This listens on CHANGE instead, so the quiet
// is timed across every edge, and each train is judged by its first edge
// against the level the line had settled at. The delay starts only on a train
// that leaves DEBOUNCED_IDLE_LEVEL -- one per press, on the edge configured.
static inline __attribute__((always_inline)) void debouncedConfirm()
{
  unsigned long now = micros();

  // Unguarded read, sound for the same reason DelayTime's is; see Settings.h.
  bool quiet = !HaveConfirmEdge || now - LastConfirmEdgeUs >= ConfirmDebounceUs;

  // Stamped in every state, so edges during the cycle count as noise too.
  LastConfirmEdgeUs = now;
  HaveConfirmEdge = true;

  if (State == WAITING_FOR_LASER_CONFIRM) {
    if (quiet && ConfirmSettledLevel == DEBOUNCED_IDLE_LEVEL) {
      startDelay();

    } else if (!quiet) {
      logEvent(LOG_CONFIRM_BOUNCE);
    }
  }

  // After arming, so the read is no latency on delay_us.
  ConfirmSettledLevel = readConfirmPin();
}


// The State guard rejects the bounce edges that land inside the cycle a
// confirm started, but a cycle can finish in under 100 us, so later ones are
// taken as the *next* cycle's confirm. ConfirmDebounceUs is the lockout. Gated
// on non-zero so a laser pays nothing, not even the micros() read --
// everything ahead of armTimer1() is latency on the front of delay_us.
void laserConfirmISR()
{
  if (ConfirmDebounceUs > 0) {
    debouncedConfirm();

    return;
  }

  if (State != WAITING_FOR_LASER_CONFIRM) {
    return;
  }

  startDelay();
}


void attachConfirmInterrupt()
{
  ConfirmPinInput = portInputRegister(digitalPinToPort(LASER_CONFIRM_PIN));
  ConfirmPinMask = digitalPinToBitMask(LASER_CONFIRM_PIN);

  ConfirmSettledLevel = readConfirmPin();
  HaveConfirmEdge = false;

  attachInterrupt(digitalPinToInterrupt(LASER_CONFIRM_PIN),
                  laserConfirmISR,
                  ConfirmDebounceUs > 0 ? CHANGE : LASER_CONFIRM_EDGE);
}


// The shutter transition has ALREADY happened in hardware by the time this
// runs. The ISR only sequences what comes next.
ISR(TIMER1_COMPA_vect)
{
  switch (Timer1Event)
  {
    case TIMER_SHUTTER_OPEN:
      State = CAPTURING;

      Timer1Event = TIMER_SHUTTER_CLOSE;

      armTimer1(CaptureTime);

      // After arming, never before: the delay window may still be matching,
      // and in clear mode one more match would close the gate early.
      closeShutterOnMatch();

      // Both windows are in hardware now, so logging costs no edge. The
      // confirm was one delay_us ago, plus ISR latency the unwrap absorbs.
      logEventAt(LOG_CONFIRM, CurrentCycle + 1, ConfirmLogTicks,
                 DelayTime.actualUs);

      logEvent(LOG_SHUTTER_OPEN);

      break;

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
        // servicePending() starts the next cycle; see PendingNextCycle above.
        PendingNextCycle = true;
      }

      break;

    // A match nobody asked for. The timer is running with no transition
    // expected, so it is free-running in CTC and matching every OCR1A.
    // Whatever re-armed it did so behind the state machine; shut it down and
    // close the shutter rather than trust whichever mode it was left in.
    default:
      stopTimer1();

      resetShutter();

      break;
  }
}


// Runs with interrupts off, and that is load bearing rather than tidiness.
// laserConfirmISR fires on whatever the laser does, and it re-arms Timer1
// whenever it finds State still set to WAITING_FOR_LASER_CONFIRM. Tearing down
// with interrupts on leaves a window between stopTimer1() and State = IDLE
// where a late confirm edge starts the timer back up behind us. Nothing stops
// it after that, so the delay expires, the hardware sets the gate open, and
// the compare ISR finds no pending event and does nothing -- shutter held open
// with the state machine reporting IDLE.
//
// A confirm timeout landing in the same millisecond as the laser finally
// answering is the easy way to hit it.
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


// Checks the camera, writes everything the cycle has to say, then fires the
// laser signal pulse. Called from loop() only, never from an ISR.
//
// All serial output for a cycle goes out, and is flushed, before the pulse.
// Bytes still leaving once the laser can answer run the USART interrupt, and
// a confirm edge landing on one waits a few us for it -- latency on the open
// edge. The cost is turnaround: the cycle waits for the wire, about 10 ms for
// the CYCLE line at 9600 and a quarter second with VERBOSE on.
//
// Returns false if the cycle could not start, having already aborted the
// sequence and reported why.
static bool beginCycle(bool announceStart)
{
  if (digitalRead(CAMERA_CAPTURING_PIN) != CAMERA_ACTIVE_LEVEL) {
    abortSequence();

    Serial.println("ERROR CAMERA NOT CAPTURING");

    return false;
  }

  if (VerboseEnabled) {
    drainLog();
  }

  if (announceStart) {
    Serial.println("STARTED");
  }

  Serial.print("CYCLE ");
  Serial.println(CurrentCycle + 1);

  Serial.flush();

  // Arm before pulsing. If the laser answers immediately, the confirm
  // interrupt must already see the right state or the edge is lost. After
  // the flush, not before, so a stray edge cannot start a cycle unfired.
  State = WAITING_FOR_LASER_CONFIRM;

  ConfirmWaitStartedMs = millis();

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

  // STARTED is written by beginCycle() once the camera check passes, so a
  // camera failure reports the error alone rather than STARTED then ERROR.
  beginCycle(true);
}


bool isRunning()
{
  return State == WAITING_FOR_LASER_CONFIRM ||
         State == WAITING_FOR_DELAY ||
         State == CAPTURING;
}


void servicePending()
{
  if (PendingNextCycle) {
    PendingNextCycle = false;

    beginCycle(false);
  }

  // The log first, so DONE stays the last line of a run.
  if (PendingDone) {
    PendingDone = false;

    if (VerboseEnabled) {
      drainLog();
    }

    Serial.println("DONE");
  }

  // Laser never answered. A timeout of 0 means the check is disabled.
  if (ConfirmTimeoutMs > 0 &&
      State == WAITING_FOR_LASER_CONFIRM &&
      millis() - ConfirmWaitStartedMs > ConfirmTimeoutMs) {
    abortSequence();

    Serial.println("ERROR LASER CONFIRM TIMEOUT");
  }

  // Between runs only. During one, beginCycle() drains ahead of the pulse.
  if (VerboseEnabled && !isRunning()) {
    drainLog();
  }
}
