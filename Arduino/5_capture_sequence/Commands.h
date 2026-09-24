// ============================================================================
// SERIAL COMMANDS
//
// The whole host-facing protocol: the responses, the argument parsing, the
// two multi-line blocks (HELP and the boot banner), and the dispatcher.
//
// Nothing in this module is reachable from an interrupt, so it is free to use
// String and to block on a serial write. Everything except these three
// entry points is private to Commands.cpp.
// ============================================================================

#ifndef COMMANDS_H
#define COMMANDS_H

#include <Arduino.h>

// Read one newline-terminated command if the host sent one.
void readCommand();

// Parse and execute. Case-insensitive; reports its own errors.
void processCommand(String command);

// The boot banner. Printed once at startup and again on demand via INFO.
void printBanner();

#endif
