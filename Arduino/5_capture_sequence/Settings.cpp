#include "Settings.h"

// The durations start out zeroed rather than at their defaults: 1 us and 50 us
// only become register values once resolveDuration() has run, so setup() fills
// these two in.
Duration DelayTime;
Duration CaptureTime;

uint16_t CycleCount = 1;

unsigned long LaserSignalPulseUs = 10;

unsigned long ConfirmTimeoutMs = 5000;

bool VerboseEnabled = false;
