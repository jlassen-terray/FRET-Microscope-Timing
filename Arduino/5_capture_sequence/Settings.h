// ============================================================================
// RUNTIME SETTINGS
//
// The six values SET writes and GET reads back. They are grouped because they
// share one invariant, and that invariant is what makes the rest of the
// firmware safe:
//
//     SET is rejected with ERROR BUSY while a sequence is running.
//
// The Timer1 compare ISR reads DelayTime, CaptureTime and CycleCount with no
// volatile and no interrupt guards. That is only sound because the parser
// refuses to write them mid-run, so there is never a concurrent write to
// tear. Relax the BUSY rule and these need rethinking.
//
// LaserSignalPulseUs and ConfirmTimeoutMs are read only from loop(), so they
// could safely change mid-run; they follow the same rule anyway so the
// protocol has one consistent behaviour.
//
// Compile-time wiring facts, including the range each of these is checked
// against, are in Config.h.
// ============================================================================

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

#include "Timer1.h"

extern Duration DelayTime;          // SET DELAY <us>
extern Duration CaptureTime;        // SET CAPTURE <us>
extern uint16_t CycleCount;         // SET CYCLE_COUNT <n>
extern unsigned long LaserSignalPulseUs;  // SET PULSE <us>
extern unsigned long ConfirmTimeoutMs;    // SET CONFIRM_TIMEOUT <ms>
extern bool VerboseEnabled;               // SET VERBOSE <0|1>

#endif
