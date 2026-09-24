// ============================================================================
// LASER PULSE
//
// Timer4 at /1, placing both laser_signal edges on OC4A. The signal idles HIGH;
// a forced compare drops it and the next compare match raises it, so the CPU
// is free to watch laser_confirm for the whole pulse.
// ============================================================================

#ifndef LASER_PULSE_H
#define LASER_PULSE_H

#include <Arduino.h>


const uint8_t SIGNAL_FALLS_ON_MATCH = _BV(COM4A1);
const uint8_t SIGNAL_RISES_ON_MATCH = _BV(COM4A1) | _BV(COM4A0);

const uint8_t TIMER4_STOPPED = _BV(WGM42);
const uint8_t TIMER4_RUNNING = _BV(WGM42) | _BV(CS40);


void beginLaserPulse();

void armLaserPulse(uint32_t us);

// Stop Timer4 and force the signal back to idle HIGH.
void releaseLaserSignal();


// PULSE_FALL_TO_TIMER4_TICKS is the gap between the first two writes.
static inline __attribute__((always_inline)) void fireLaserPulse()
{
  TCCR4C = _BV(FOC4A);
  TCCR4B = TIMER4_RUNNING;
  TCCR4A = SIGNAL_RISES_ON_MATCH;
}

#endif
