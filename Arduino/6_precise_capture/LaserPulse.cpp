#include "LaserPulse.h"

#include "Config.h"

static_assert(LASER_SIGNAL_PIN == 6, "laser_signal must be OC4A");


// The latch is set before the pin becomes an output, so it never shows LOW.
void beginLaserPulse()
{
  TIMSK4 = 0;

  releaseLaserSignal();

  pinMode(LASER_SIGNAL_PIN, OUTPUT);
}


void armLaserPulse(uint32_t us)
{
  TCCR4B = TIMER4_STOPPED;

  TCNT4 = PULSE_FALL_TO_TIMER4_TICKS;
  OCR4A = us * TICKS_PER_US - 1;

  TCCR4A = SIGNAL_FALLS_ON_MATCH;
}


void releaseLaserSignal()
{
  TCCR4B = TIMER4_STOPPED;

  TCCR4A = SIGNAL_RISES_ON_MATCH;
  TCCR4C = _BV(FOC4A);
}
