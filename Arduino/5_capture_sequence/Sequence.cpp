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


// Nothing prints from an ISR. At 9600 baud a byte costs 1.04 ms on the wire, so
// a 40-character line is roughly 40 ms -- four orders of magnitude longer than
// the windows being timed. The ISR raises a flag and servicePending() does the
// work.
//
// PendingNextCycle matters for a second reason: starting a cycle reads the
// camera input, pulses laser_signal with a blocking delay, and may need to
// report an error. None of that belongs in an interrupt.

static volatile bool PendingCycleStarted = false;
static volatile uint16_t PendingCycleNumber = 0;
static volatile bool PendingDone = false;
static volatile bool PendingNextCycle = false;


// CurrentCycle counts cycles *finished*, so the one in progress is one past it.
// DONE is the exception and calls logEventForCycle() directly; see the call.
static void logEvent(uint8_t event)
{
  logEventForCycle(event, CurrentCycle + 1);
}


// The laser has acknowledged. Start the configured delay; when it expires the
// Timer1 hardware toggles OC1A and the shutter gate opens with no software in
// the path.
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
    // expected, so it is free-running in CTC and will keep toggling the gate
    // every OCR1A. Whatever re-armed it did so behind the state machine; shut
    // it down and close the shutter rather than leave the pin flapping.
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
// it after that, so the delay expires, the hardware toggles the gate open, and
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


// Checks the camera, then fires the laser signal pulse. Called from loop()
// only, never from an ISR.
//
// Returns false if the cycle could not start, having already aborted the
// sequence and reported why.
static bool beginCycle()
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


bool isRunning()
{
  return State == WAITING_FOR_LASER_CONFIRM ||
         State == WAITING_FOR_DELAY ||
         State == CAPTURING;
}


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
