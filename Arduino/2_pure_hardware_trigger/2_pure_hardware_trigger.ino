// ============================================================
// Arduino Mega 2560
//
// Button:
//     Pin 2 -> button -> GND
//
// LED:
//     Pin 11 (OC1A) -> 330 ohm -> LED -> GND
//
// Behavior:
//     1. Hold button.
//     2. Release button.
//     3. Timer1 starts.
//     4. Timer1 counts for 1 second.
//     5. Timer1 hardware toggles OC1A (pin 11).
//     6. Timer1 interrupt stops the timer.
//
// The actual LED transition is performed by TIMER1 hardware.
// digitalWrite() is NOT used for the LED.
// ============================================================

const uint8_t BUTTON_PIN = 2;

// OC1A is Arduino Mega digital pin 11.
const uint8_t LED_PIN = 11;

// Timer1 runs at 16 MHz.
// Prescaler = 256.
//
// 16,000,000 / 256 = 62,500 counts/sec
//
// For 1 second:
//     62,500 counts
//
// CTC counts 0 through OCR1A, so:
//     OCR1A = 62,499

const uint16_t TIMER_COMPARE = 62499;


// Mechanical contacts bounce for several milliseconds on release, and each
// bounce edge would re-trigger the interrupt and restart Timer1. Ignore any
// release that lands within this window of the previous one.

const unsigned long DEBOUNCE_US = 20000UL;

volatile unsigned long lastReleaseUs = 0;


// ============================================================
// SETUP
// ============================================================

void setup()
{
    // --------------------------------------------------------
    // Button
    //
    // Internal pull-up:
    //
    // Released = HIGH
    // Pressed  = LOW
    // --------------------------------------------------------

    pinMode(BUTTON_PIN, INPUT_PULLUP);


    // --------------------------------------------------------
    // Timer1 OC1A
    //
    // OC1A is Arduino Mega digital pin 11.
    //
    // Configure pin 11 as an output.
    // --------------------------------------------------------

    pinMode(LED_PIN, OUTPUT);


    // Start LED LOW.
    digitalWrite(LED_PIN, LOW);


    // --------------------------------------------------------
    // Configure Timer1
    // --------------------------------------------------------

    // Clear Timer1 configuration.
    TCCR1A = 0;
    TCCR1B = 0;

    // Reset counter.
    TCNT1 = 0;

    // 1 second delay.
    OCR1A = TIMER_COMPARE;


    // --------------------------------------------------------
    // Configure OC1A
    //
    // COM1A0 = 1
    //
    // Timer1 hardware will toggle OC1A whenever
    // the timer reaches OCR1A.
    // --------------------------------------------------------

    TCCR1A = (1 << COM1A0);


    // --------------------------------------------------------
    // Enable Timer1 Compare A interrupt.
    //
    // This interrupt is ONLY used to stop the timer after
    // the hardware has performed the toggle.
    //
    // It does NOT control the LED.
    // --------------------------------------------------------

    TIMSK1 = (1 << OCIE1A);


    // --------------------------------------------------------
    // Button release interrupt
    //
    // INPUT_PULLUP means:
    //
    //     pressed  = LOW
    //     released = HIGH
    //
    // Therefore RISING detects the release.
    // --------------------------------------------------------

    attachInterrupt(
        digitalPinToInterrupt(BUTTON_PIN),
        buttonReleased,
        RISING
    );
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
    // Nothing required.
    //
    // Button release and Timer1 are handled by hardware
    // interrupts.
}


// ============================================================
// BUTTON RELEASE
// ============================================================
//
// Called when:
//     LOW -> HIGH
//
// This means the button has been released.
//
// This function starts Timer1.
// ============================================================

void buttonReleased()
{
    // Reject contact bounce.
    //
    // micros() is safe to read from an ISR and advances between separate
    // interrupt invocations, which is all the comparison needs.

    unsigned long now = micros();

    if (now - lastReleaseUs < DEBOUNCE_US)
    {
        return;
    }

    lastReleaseUs = now;


    // Reset timer to zero.
    TCNT1 = 0;

    // Discard a compare match left pending from a previous run, which would
    // otherwise fire the ISR immediately and toggle the LED early.
    TIFR1 = (1 << OCF1A);

    // Start Timer1.
    //
    // WGM12 = CTC mode
    // CS12  = prescaler /256
    //
    // Timer now counts:
    //
    //     0 -> 62499
    //
    // which takes approximately 1 second.

    TCCR1B = (1 << WGM12) | (1 << CS12);
}


// ============================================================
// TIMER1 COMPARE INTERRUPT
// ============================================================
//
// This occurs when Timer1 reaches OCR1A.
//
// IMPORTANT:
//
// The hardware has ALREADY toggled OC1A at this point.
//
// This ISR simply stops the timer so that another toggle
// doesn't happen one second later.
// ============================================================

ISR(TIMER1_COMPA_vect)
{
    // Stop Timer1.
    //
    // Clearing the clock-select bits stops the counter.

    TCCR1B &= ~((1 << CS12) |
                (1 << CS11) |
                (1 << CS10));
}