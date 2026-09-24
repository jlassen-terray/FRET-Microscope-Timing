// ============================================================================
// RUNTIME SETTINGS
//
// The five values SET writes and GET reads back. Ranges are in Config.h.
// Nothing reads these from an interrupt, so they need no guards.
// ============================================================================

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

extern uint32_t DelayUs;            // SET DELAY <us>
extern uint32_t CaptureUs;          // SET CAPTURE <us>
extern uint32_t CycleCount;         // SET CYCLE_COUNT <n>
extern uint32_t PulseUs;            // SET PULSE <us>
extern uint32_t ConfirmTimeoutMs;   // SET CONFIRM_TIMEOUT <ms>, 0 disables

#endif
