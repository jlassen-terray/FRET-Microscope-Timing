#include "LogRing.h"

#include "Config.h"
#include "Settings.h"


#if !defined(TCNT5)
#error "The log clock is Timer5, which is a Mega 2560 peripheral."
#endif


// ----------------------------------------------------------------------------
// THE LOG CLOCK
//
// micros() steps in 4 us -- Timer0 is prescaled /64 and the low bits do not
// exist. On the gaps worth watching that step is most of the measurement: a
// 10 us pulse reads 8 or 12, and a 50 us capture is only ever right to 8%.
//
// Timer1 cannot fill them in, despite being the finer clock. It is stopped
// for every gap software produces -- the pulse, the laser's answer, the
// turnaround -- and runs only across delay_us and capture_us, which are
// OCR1A+1 ticks by construction and therefore already known exactly. The one
// clock that is free when the interesting gaps happen is a spare timer, so
// the log keeps its own: Timer5, 16 bit, free-running at /1, one tick per
// 62.5 ns.
//
// No interrupt is attached and none is wanted. The counter is only ever read,
// so the log cannot delay laserConfirmISR or the compare ISR -- which is the
// whole point of placing the shutter edges in hardware, and an overflow ISR
// at any useful resolution would spend it.
//
// Timer5's compare outputs stay disconnected, so pins 44-46 remain GPIO. The
// cost is analogWrite() on those three, which this sketch does not use.
// Timer3 and Timer4 were left alone because their output pins collide with
// the signals above.
// ----------------------------------------------------------------------------

void beginLog()
{
  TCCR5A = 0;
  TCCR5B = (1 << CS50);
  TCNT5 = 0;
  TIMSK5 = 0;
}


// Producers run in both interrupt and main context, so every push saves and
// restores SREG rather than using noInterrupts()/interrupts(). Calling
// interrupts() inside an ISR would re-enable them early and allow reentry.

struct LogEntry
{
  unsigned long timestampUs;
  uint16_t fineTicks;
  uint16_t cycle;
  uint8_t event;
};

static const uint8_t LOG_CAPACITY = 32;

static LogEntry LogRing[LOG_CAPACITY];

static volatile uint8_t LogHead = 0;
static volatile uint8_t LogTail = 0;
static volatile uint8_t LogDropped = 0;


// Called with interrupts off.
static void pushEntry(uint8_t event, uint16_t cycle, unsigned long now,
                      uint16_t fine)
{
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
    LogRing[LogHead].fineTicks = fine;
    LogRing[LogHead].cycle = cycle;
    LogRing[LogHead].event = event;

    LogHead = next;
  }
}

void logEventForCycle(uint8_t event, uint16_t cycle)
{
  if (!VerboseEnabled) {
    return;
  }

  // Stamp first, so queueing cost is not included in the measurement.
  unsigned long now = micros();

  uint8_t sreg = SREG;
  cli();

  // A 16 bit timer read goes through the shared TEMP register, so it has to
  // be guarded against an ISR touching TCNT1 or OCR1A mid-read. It costs
  // nothing extra here: the ring push needs the guard anyway.
  //
  // Reading it after micros() rather than before leaves the two stamps an
  // interrupt apart at worst. The drain only needs them to agree to within
  // half a Timer5 wrap -- 2048 us -- to unwrap correctly.
  uint16_t fine = TCNT5;

  pushEntry(event, cycle, now, fine);

  SREG = sreg;
}

void logEventAt(uint8_t event, uint16_t cycle, uint16_t fineTicks,
                unsigned long ageUs)
{
  if (!VerboseEnabled) {
    return;
  }

  unsigned long now = micros() - ageUs;

  uint8_t sreg = SREG;
  cli();

  pushEntry(event, cycle, now, fineTicks);

  SREG = sreg;
}


// The "+" column is a gap between two consecutive drained events, so the drain
// side carries state of its own. resetLog() has to clear it too, or the first
// event of a run reports the gap since the *previous* run's last event -- an
// interval that measures nothing and can be arbitrarily large.
static unsigned long LogLastUs = 0;
static uint16_t LogLastFine = 0;
static bool LogHaveLast = false;

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


// Timer5 wraps every 65536 ticks, so the tick delta alone cannot tell a 10 us
// gap from a 4106 us one. The micros() delta says which wrap it landed in --
// it only has to be right to within half a wrap, and it is good to a few us --
// and the tick delta then gives the exact position inside it.
//
// Past FINE_MAX_US the multiply below would be at risk and the precision
// would be meaningless anyway: a gap that long is a laser or a person being
// waited on. Those print as whole microseconds.
static const unsigned long FINE_MAX_US = 1000000UL;

static void printGapUs(unsigned long coarseUs, uint16_t fineDelta)
{
  if (coarseUs >= FINE_MAX_US) {
    Serial.print(coarseUs);
    Serial.print(F("us"));

    return;
  }

  long offset = (long)(coarseUs * TICKS_PER_US) - (long)fineDelta;

  long wraps = (offset + 32768L) / 65536L;

  if (wraps < 0) {
    wraps = 0;
  }

  unsigned long ticks = (unsigned long)wraps * 65536UL + fineDelta;

  Serial.print(ticks / TICKS_PER_US);
  Serial.print('.');

  // One tick is 0.0625 us, so the fraction is a multiple of 625 and always
  // wants four digits.
  unsigned int frac = (unsigned int)(ticks % TICKS_PER_US) * 625;

  if (frac == 0) {
    Serial.print(F("0000"));

  } else {
    if (frac < 1000) {
      Serial.print('0');
    }

    Serial.print(frac);
  }

  Serial.print(F("us"));
}


// Lines are prefixed "V " so a host parser can separate them from command
// responses. Both numbers carry a "us" suffix: unlabelled they read like the
// Timer1 tick counts SET and GET echo, which they are not. The "+" column is
// the gap since the previous logged event, which
// is the number worth watching: it is the measured version of delay_us,
// capture_us, and the laser's own response time.

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
      case LOG_CONFIRM_BOUNCE:
                              Serial.print(F("CONFIRM_BOUNCE")); break;
      case LOG_SHUTTER_OPEN:  Serial.print(F("SHUTTER_OPEN"));  break;
      case LOG_SHUTTER_CLOSE: Serial.print(F("SHUTTER_CLOSE")); break;
      case LOG_DONE:          Serial.print(F("DONE"));          break;
      default:                Serial.print(F("UNKNOWN"));       break;
    }

    Serial.print(F(" t="));
    Serial.print(entry.timestampUs);
    Serial.print(F("us"));

    if (LogHaveLast) {
      Serial.print(F(" +"));
      printGapUs(entry.timestampUs - LogLastUs,
                 (uint16_t)(entry.fineTicks - LogLastFine));
    }

    Serial.println();

    LogLastUs = entry.timestampUs;
    LogLastFine = entry.fineTicks;
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
