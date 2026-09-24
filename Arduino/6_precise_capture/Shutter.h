// ============================================================================
// SHUTTER
//
// Timer1 at /1, placing both shutter_enable edges on OC1A. A run's delay and
// capture windows form one timeline of Timer1 periods ("segments"); software
// loads each next segment while the current one runs, and the compare match
// at the end of each segment is what moves the pin.
//
// The inline functions are on the timed path and must stay inline so their
// cycle counts are fixed.
// ============================================================================

#ifndef SHUTTER_H
#define SHUTTER_H

#include <Arduino.h>


// What the match ending a segment does to the pin. Repeating the level the pin
// already has is how a segment holds it.
const uint8_t OPEN_ON_MATCH = _BV(COM1A1) | _BV(COM1A0);
const uint8_t CLOSE_ON_MATCH = _BV(COM1A1);

const uint8_t TIMER1_STOPPED = _BV(WGM12);
const uint8_t TIMER1_RUNNING = _BV(WGM12) | _BV(CS10);


// A window split into near-equal segments of at most MAX_SEGMENT_TICKS. The
// first longSegments are one tick longer than the rest.
struct Window
{
  uint16_t segments;
  uint16_t shortTop;
  uint16_t longSegments;
};

struct Segment
{
  uint16_t top;
  uint8_t onMatch;
};


Window planWindow(uint32_t us);

// Segment i of the timeline: the delay's segments, then the capture's.
Segment segmentAt(const Window &delay, const Window &capture, uint16_t i);

void beginShutter();

// Stopped, preloaded with the first segment and the confirm latency.
void armShutter(Segment first);

void closeShutter();


static inline __attribute__((always_inline)) void startShutterTimer()
{
  TCCR1B = TIMER1_RUNNING;
}

static inline __attribute__((always_inline)) void awaitSegmentEnd()
{
  while (!(TIFR1 & _BV(OCF1A))) {
  }

  TIFR1 = _BV(OCF1A);
}

// Must land before the counter reaches the previous top again, which for a
// 1 us window is 16 cycles after the match.
static inline __attribute__((always_inline)) void loadSegment(Segment next)
{
  OCR1A = next.top;
  TCCR1A = next.onMatch;
}

static inline __attribute__((always_inline)) void stopShutterTimer()
{
  TCCR1B = TIMER1_STOPPED;
}

#endif
