// ============================================================================
// THE SEQUENCE
//
// runSequence() owns the board from START until its last edge. Interrupts are
// off for the whole run and every wait is a poll, so the only code between an
// edge and the timer that answers it is the loop watching for that edge.
//
// Any byte from the host aborts a run. The byte is left in the UART, so once
// the run returns the command it belongs to is read and answered as usual.
// ============================================================================

#ifndef SEQUENCE_H
#define SEQUENCE_H

#include <Arduino.h>


enum RunOutcome
{
  RUN_DONE,
  RUN_ABORTED,
  RUN_CAMERA_NOT_CAPTURING,
  RUN_CONFIRM_TIMEOUT
};

// Cycles the last run finished.
extern uint32_t CompletedCycles;

void beginSequence();

bool cameraCapturing();

RunOutcome runSequence();

// Laser off, shutter closed, laser_signal idle.
void releaseOutputs();

#endif
