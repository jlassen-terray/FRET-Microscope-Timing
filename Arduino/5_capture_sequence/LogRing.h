// ============================================================================
// LOG RING
//
// Verbose logging must not perturb what it is measuring, so it is split in
// two. Producers capture an event id and two clock stamps into a ring -- a
// handful of instructions, always placed AFTER the timer registers have been
// written, so no hardware edge ever waits on it. loop() then drains the ring
// and does the expensive part: formatting and the serial write.
//
// The two stamps are micros(), which is coarse but never wraps in a run, and
// a free-running Timer5, which resolves 62.5 ns but wraps every 4096 us. The
// drain combines them into an exact gap. See the log clock note in the .cpp
// for why Timer1 is not the one used.
//
// If the ring fills, events are counted and discarded rather than blocking.
// A dropped count is reported so the log can never quietly lie about what
// happened.
//
// Callers hand in an event id and a cycle number and nothing else, which is
// what keeps this module independent of the sequence it observes.
// ============================================================================

#ifndef LOGRING_H
#define LOGRING_H

#include <Arduino.h>

enum LogEventId : uint8_t
{
  LOG_CYCLE_BEGIN,
  LOG_PULSE,
  LOG_CONFIRM,
  LOG_SHUTTER_OPEN,
  LOG_SHUTTER_CLOSE,
  LOG_DONE
};


// Start the free-running log clock. Call once from setup(), before any
// logging. Attaches no interrupt.
void beginLog();

// Safe from any context. Costs a few dozen cycles.
//
// The cycle number is passed in rather than read from the sequence state,
// because the caller is the only one who knows which cycle an event belongs
// to: in the shutter-close ISR, CurrentCycle means different things either
// side of the increment. Sequence.cpp wraps this for the common case.
void logEventForCycle(uint8_t event, uint16_t cycle);

// Discard anything queued, and clear the drain-side state with it.
void resetLog();

// Format and write everything queued. Called from loop() only.
void drainLog();

#endif
