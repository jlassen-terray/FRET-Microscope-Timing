// ============================================================================
// THE SEQUENCE
//
// The state machine, the two interrupt handlers that advance a cycle, the
// teardown that returns the outputs to a safe state, and the loop()-side work
// the ISRs hand back.
//
// The division of labour: interrupts only touch state and timer registers,
// and everything that reads a pin, blocks, or prints happens in
// servicePending() instead.
//
// Only the state a host can observe is exposed. The pending-work flags, the
// Timer1 event selector and beginCycle() are private to Sequence.cpp.
// ============================================================================

#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <Arduino.h>


// IDLE and COMPLETE are both "not running"; isRunning() is the test to use
// rather than comparing against a particular state.
enum SequenceState
{
  IDLE,
  WAITING_FOR_LASER_CONFIRM,
  WAITING_FOR_DELAY,
  CAPTURING,
  COMPLETE
};

extern volatile SequenceState State;

// Cycles finished so far, so 0 before the first one completes.
extern volatile uint16_t CurrentCycle;

bool isRunning();

void startSequence();

// Stop everything and return the outputs to a safe state. From loop() only;
// see the definition for why it runs with interrupts off.
void abortSequence();

// Drain the work the ISRs handed back. From loop() only.
void servicePending();

// Attached to LASER_CONFIRM_PIN by setup().
void laserConfirmISR();

#endif
