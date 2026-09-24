#include "Settings.h"

// The durations start out zeroed rather than at their defaults: 1 us and 50 us
// only become register values once resolveDuration() has run, so setup() fills
// these two in.
Duration DelayTime;
Duration CaptureTime;

uint16_t CycleCount = 1;

unsigned long LaserSignalPulseUs = 10;

unsigned long ConfirmTimeoutMs = 5000;

// Defaults to 20 ms for bench testing, where laser_confirm is a button.
//
// SET IT TO 0 ON RIG INSTALL. A laser controller answers once per pulse and
// needs no lockout, and 20 ms of it caps the sequence at 50 cycles/second.
unsigned long ConfirmDebounceUs = 20000;

bool VerboseEnabled = false;
