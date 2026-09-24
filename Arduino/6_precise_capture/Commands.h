// ============================================================================
// SERIAL COMMANDS
//
// The host-facing protocol, HELP, and the boot banner. Only ever runs between
// sequences, so it is free to block on serial and use String.
// ============================================================================

#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>

// Read and execute one newline-terminated command if the host sent one.
void readCommand();

void printBanner();

#endif
