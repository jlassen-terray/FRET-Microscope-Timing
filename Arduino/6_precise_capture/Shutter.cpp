#include "Shutter.h"

#include "Config.h"

static_assert(SHUTTER_ENABLE_PIN == 11, "shutter_enable must be OC1A");


Window planWindow(uint32_t us)
{
  uint32_t ticks = us * TICKS_PER_US;

  Window window;

  window.segments = (ticks + MAX_SEGMENT_TICKS - 1) / MAX_SEGMENT_TICKS;
  window.shortTop = ticks / window.segments - 1;
  window.longSegments = ticks % window.segments;

  return window;
}


static uint16_t topOf(const Window &window, uint16_t i)
{
  return i < window.longSegments ? window.shortTop + 1 : window.shortTop;
}

Segment segmentAt(const Window &delay, const Window &capture, uint16_t i)
{
  if (i < delay.segments) {
    bool opensShutter = i + 1 == delay.segments;

    return { topOf(delay, i), opensShutter ? OPEN_ON_MATCH : CLOSE_ON_MATCH };
  }

  i -= delay.segments;

  bool closesShutter = i + 1 == capture.segments;

  return { topOf(capture, i), closesShutter ? CLOSE_ON_MATCH : OPEN_ON_MATCH };
}


void beginShutter()
{
  TIMSK1 = 0;

  closeShutter();

  pinMode(SHUTTER_ENABLE_PIN, OUTPUT);
}


void armShutter(Segment first)
{
  stopShutterTimer();

  TCNT1 = CONFIRM_TO_TIMER1_TICKS;

  loadSegment(first);

  TIFR1 = _BV(OCF1A);
}


// While COM1A is set the pin shows the OC1A latch, not PORTB, and a forced
// compare in clear mode is the way to write that latch.
void closeShutter()
{
  stopShutterTimer();

  TCCR1A = CLOSE_ON_MATCH;
  TCCR1C = _BV(FOC1A);
}
