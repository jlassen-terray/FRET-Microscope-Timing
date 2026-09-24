#include "LogRing.h"

#include "Settings.h"


// Producers run in both interrupt and main context, so every push saves and
// restores SREG rather than using noInterrupts()/interrupts(). Calling
// interrupts() inside an ISR would re-enable them early and allow reentry.

struct LogEntry
{
  unsigned long timestampUs;
  uint16_t cycle;
  uint8_t event;
};

static const uint8_t LOG_CAPACITY = 32;

static LogEntry LogRing[LOG_CAPACITY];

static volatile uint8_t LogHead = 0;
static volatile uint8_t LogTail = 0;
static volatile uint8_t LogDropped = 0;


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


// The "+" column is a gap between two consecutive drained events, so the drain
// side carries state of its own. resetLog() has to clear it too, or the first
// event of a run reports the gap since the *previous* run's last event -- an
// interval that measures nothing and can be arbitrarily large.
static unsigned long LogLastUs = 0;
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


// Lines are prefixed "V " so a host parser can separate them from command
// responses. The "+" column is the gap since the previous logged event, which
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
