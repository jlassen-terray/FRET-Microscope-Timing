#include "Sequence.h"

#include "Config.h"
#include "LaserPulse.h"
#include "Settings.h"
#include "Shutter.h"

static_assert(LASER_CONFIRM_PIN == 2, "laser_confirm must be INT4");


uint32_t CompletedCycles = 0;

static const uint8_t CONFIRM_EDGE_BITS =
    LASER_CONFIRM_EDGE == RISING ? _BV(ISC41) | _BV(ISC40) : _BV(ISC41);

// Timer3 in CTC at /64: 250 ticks of 4 us.
static const uint16_t MILLISECOND_TOP = 249;


void beginSequence()
{
  pinMode(CAMERA_CAPTURING_PIN, INPUT);
  pinMode(LASER_CONFIRM_PIN, LASER_CONFIRM_INPUT_MODE);

  // The edge still sets INTF4 with the interrupt masked, which is all the
  // confirm wait polls.
  EIMSK &= ~_BV(INT4);
  EICRB = (EICRB & ~(_BV(ISC41) | _BV(ISC40))) | CONFIRM_EDGE_BITS;

  digitalWrite(LASER_ENABLE_PIN, LOW);
  pinMode(LASER_ENABLE_PIN, OUTPUT);

  TIMSK3 = 0;
  TCCR3A = 0;
  TCCR3B = _BV(WGM32) | _BV(CS31) | _BV(CS30);
  OCR3A = MILLISECOND_TOP;
}


bool cameraCapturing()
{
  return digitalRead(CAMERA_CAPTURING_PIN) == CAMERA_ACTIVE_LEVEL;
}


static inline __attribute__((always_inline)) bool confirmArrived()
{
  return EIFR & _BV(INTF4);
}

static inline bool hostSentByte()
{
  return UCSR0A & _BV(RXC0);
}

static void restartMillisecondTick()
{
  TCNT3 = 0;
  TIFR3 = _BV(OCF3A);
}

static inline bool millisecondElapsed()
{
  if (!(TIFR3 & _BV(OCF3A))) {
    return false;
  }

  TIFR3 = _BV(OCF3A);

  return true;
}


static RunOutcome runCycle(const Window &delay, const Window &capture)
{
  const uint16_t segments = delay.segments + capture.segments;

  armShutter(segmentAt(delay, capture, 0));

  // Loaded the moment the first segment ends, which for a 1 us delay is
  // sooner than it could be computed.
  Segment next = segmentAt(delay, capture, 1);

  armLaserPulse(PulseUs);

  // A disabled timeout still counts down, it just never acts, which keeps the
  // millisecond branch short enough to stay inside the 1 us budget.
  const bool timeoutEnabled = ConfirmTimeoutMs > 0;
  uint32_t msRemaining = ConfirmTimeoutMs;

  restartMillisecondTick();

  // Discard edges from before this pulse.
  EIFR = _BV(INTF4);

  fireLaserPulse();

  while (!confirmArrived()) {
    if (millisecondElapsed()) {
      if (hostSentByte()) {
        return RUN_ABORTED;
      }

      if (--msRemaining == 0 && timeoutEnabled) {
        return RUN_CONFIRM_TIMEOUT;
      }
    }
  }

  // The first reload is outside the loop so that no loop setup can land
  // between starting the timer and meeting the 1 us deadline.
  startShutterTimer();
  awaitSegmentEnd();
  loadSegment(next);

  for (uint16_t i = 2; i < segments; i++) {
    next = segmentAt(delay, capture, i);

    if (hostSentByte()) {
      return RUN_ABORTED;
    }

    awaitSegmentEnd();
    loadSegment(next);
  }

  awaitSegmentEnd();
  stopShutterTimer();

  return RUN_DONE;
}


RunOutcome runSequence()
{
  const Window delay = planWindow(DelayUs);
  const Window capture = planWindow(CaptureUs);

  RunOutcome outcome = RUN_DONE;

  CompletedCycles = 0;

  // Nothing is written during a run; a byte still leaving would need the
  // UART interrupt.
  Serial.flush();

  noInterrupts();

  digitalWrite(LASER_ENABLE_PIN, HIGH);

  while (CompletedCycles < CycleCount) {
    if (hostSentByte()) {
      outcome = RUN_ABORTED;
      break;
    }

    if (!cameraCapturing()) {
      outcome = RUN_CAMERA_NOT_CAPTURING;
      break;
    }

    outcome = runCycle(delay, capture);

    if (outcome != RUN_DONE) {
      break;
    }

    CompletedCycles++;
  }

  releaseOutputs();

  interrupts();

  return outcome;
}


void releaseOutputs()
{
  closeShutter();
  releaseLaserSignal();

  digitalWrite(LASER_ENABLE_PIN, LOW);
}
