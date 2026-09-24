#include "Timer1.h"

#include "Config.h"


bool resolveDuration(unsigned long us, Duration &out)
{
  if (us < MIN_US || us > MAX_US) {
    return false;
  }

  const uint16_t prescalers[5] = { 1, 8, 64, 256, 1024 };

  const uint8_t clockSelects[5] = {
      (1 << CS10),
      (1 << CS11),
      (1 << CS11) | (1 << CS10),
      (1 << CS12),
      (1 << CS12) | (1 << CS10)
  };

  for (uint8_t i = 0; i < 5; i++) {
    unsigned long ticks = (us * (unsigned long)TICKS_PER_US) / prescalers[i];

    if (ticks < 1 || ticks > 65536UL) {
      continue;
    }

    out.requestedUs = us;

    // CTC counts 0..OCR1A inclusive, so the compare value is one less than
    // the tick count.
    out.compare = (uint16_t)(ticks - 1);

    out.prescaler = prescalers[i];
    out.clockSelect = clockSelects[i];

    // What the hardware will actually produce after integer truncation.
    out.actualUs = (ticks * prescalers[i]) / TICKS_PER_US;

    return true;
  }

  return false;
}


String durationDetail(const Duration &d)
{
  return String(d.requestedUs) + "us -> " + d.actualUs + "us (" +
         ((unsigned long)d.compare + 1) + " ticks @ /" + d.prescaler + ")";
}


void armTimer1(const Duration &d)
{
  TCNT1 = 0;

  OCR1A = d.compare;

  // Discard a stale compare match, which would otherwise fire immediately.
  TIFR1 = (1 << OCF1A);

  // CTC mode plus the prescaler this duration resolved to.
  TCCR1B = (1 << WGM12) | d.clockSelect;
}

void stopTimer1()
{
  TCCR1B &= ~((1 << CS12) | (1 << CS11) | (1 << CS10));
}


// OC1A runs in hardware toggle mode, so the pin state depends on how many
// compare matches have happened. An odd number leaves the gate open, which is
// what an abort mid-capture produces, so there has to be a way to force it
// back to released.
//
// Writing the port will not do it. While a COM1A bit is set the waveform
// generator owns the pin, and the value it drives lives in the OC1A register,
// which keeps its state across a TCCR1A write. Disconnecting the output,
// writing PORTB low and reconnecting therefore only drives the pin low for the
// few cycles it is disconnected: the moment toggle mode comes back, the stale
// OC1A register reappears on the pin and the shutter is open again.
//
// The supported way to set OC1A directly is a forced compare. Select "clear on
// compare match" and strobe FOC1A: the compare output logic applies the COM1A
// setting to the OC1A register without raising OCF1A and without resetting the
// counter. Then restore toggle mode for the next window.
void resetShutter()
{
  TCCR1A = (1 << COM1A1);
  TCCR1C = (1 << FOC1A);

  TCCR1A = (1 << COM1A0);
}
