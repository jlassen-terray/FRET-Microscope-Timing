// ============================================================================
// TIMER 1
//
// The hardware that places the shutter edges, and the arithmetic that turns a
// microsecond request into register values it can produce.
//
// resolveDuration() touches no registers and no globals, which makes it the
// one piece of this firmware that can be compiled and tested on a host. It
// owns both the prescaler selection and the CTC off-by-one.
// ============================================================================

#ifndef TIMER1_H
#define TIMER1_H

#include <Arduino.h>


// A microsecond request and the register values that produce it. actualUs is
// what the hardware will really deliver, which differs from requestedUs
// wherever the prescaler's resolution step cannot express it exactly.
struct Duration
{
  unsigned long requestedUs;
  unsigned long actualUs;
  uint16_t compare;      // OCR1A value
  uint16_t prescaler;
  uint8_t clockSelect;   // CS bits for TCCR1B
};


// False if the request cannot be represented at any prescaler.
bool resolveDuration(unsigned long us, Duration &out);

// Human-readable form, without an "OK <LABEL>" prefix. Commands.cpp adds that
// for the SET/GET responses; the boot banner prints this text bare, in a
// column, so it must not have anything to strip off the front.
String durationDetail(const Duration &d);

void armTimer1(const Duration &d);
void stopTimer1();

// What the next compare match does to the shutter gate. Set and clear rather
// than toggle; see the comment on the definition.
void openShutterOnMatch();
void closeShutterOnMatch();

// Force the shutter gate back to released. See the comment on the definition
// for why writing the port will not do it.
void resetShutter();

#endif
