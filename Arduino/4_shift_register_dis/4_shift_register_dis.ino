#include <SPI.h>


// ============================================================
// Pins
// ============================================================

const uint8_t LATCH_PIN = 6;

// Hardware SPI on the Mega requires SS (pin 53) to be an output,
// otherwise the SPI peripheral can drop into slave mode.
const uint8_t SPI_SS_PIN = 53;


// ============================================================
// 7-segment encoding
// ============================================================

const uint8_t BLANK = 10;

// Segments are active-low, so each pattern is stored inverted.
// The cast keeps the ~ result from widening to int and tripping -Wnarrowing.
#define SEG(bits) ((uint8_t)~(bits))

const uint8_t digitSegments[11] = {

    SEG(0b11111100), // 0
    SEG(0b01100000), // 1
    SEG(0b11011010), // 2
    SEG(0b11110010), // 3
    SEG(0b01100110), // 4
    SEG(0b10110110), // 5
    SEG(0b10111110), // 6
    SEG(0b11100000), // 7
    SEG(0b11111110), // 8
    SEG(0b11110110), // 9

    0xFF             // blank
};


// ============================================================
// Digit selection
// ============================================================

const uint8_t digitSelect[4] = {

    0b00010000,  // thousands
    0b00100000,  // hundreds
    0b01000000,  // tens
    0b10000000   // ones
};


// ============================================================
// Display state
// ============================================================

volatile uint32_t displayValue = 0;


// ============================================================
// Multiplex state
// ============================================================

volatile uint8_t currentDigit = 0;


// ============================================================
// Counter
// ============================================================

int number = 0;


// ============================================================
// Counter update rate
// ============================================================

const unsigned long UPDATE_INTERVAL_MS = 250;


// ============================================================
// Latch port
// ============================================================

volatile uint8_t* latchPort;
uint8_t latchMask;


// ============================================================
// Pack display
// ============================================================

uint32_t packDisplay(
    uint8_t thousands,
    uint8_t hundreds,
    uint8_t tens,
    uint8_t ones
)
{
    return
        ((uint32_t)digitSegments[thousands] << 24) |
        ((uint32_t)digitSegments[hundreds]  << 16) |
        ((uint32_t)digitSegments[tens]      << 8)  |
        ((uint32_t)digitSegments[ones]);
}


// ============================================================
// Setup
// ============================================================

void setup()
{
    // --------------------------------------------------------
    // Latch pin
    // --------------------------------------------------------

    pinMode(LATCH_PIN, OUTPUT);

    digitalWrite(LATCH_PIN, LOW);

    latchPort = portOutputRegister(
        digitalPinToPort(LATCH_PIN)
    );

    latchMask = digitalPinToBitMask(LATCH_PIN);


    // --------------------------------------------------------
    // Hardware SPI
    // --------------------------------------------------------

    pinMode(SPI_SS_PIN, OUTPUT);

    SPI.begin();

    SPI.setBitOrder(LSBFIRST);
    SPI.setDataMode(SPI_MODE0);
    SPI.setClockDivider(SPI_CLOCK_DIV16);


    // --------------------------------------------------------
    // Timer 2
    // --------------------------------------------------------

    TCCR2A = 0;
    TCCR2B = 0;

    // CTC mode
    TCCR2A |= (1 << WGM21);

    // 1 kHz interrupt
    OCR2A = 249;

    // Prescaler = 64
    TCCR2B |= (1 << CS22);

    // Enable Timer 2 Compare Match A interrupt
    TIMSK2 |= (1 << OCIE2A);
}


// ============================================================
// Timer 2 ISR
// ============================================================

ISR(TIMER2_COMPA_vect)
{
    uint32_t value = displayValue;


    // --------------------------------------------------------
    // Extract segment data
    // --------------------------------------------------------

    uint8_t segments;

    switch (currentDigit)
    {
        case 0:
            segments = (value >> 24) & 0xFF;
            break;

        case 1:
            segments = (value >> 16) & 0xFF;
            break;

        case 2:
            segments = (value >> 8) & 0xFF;
            break;

        default:
            segments = value & 0xFF;
            break;
    }


    // --------------------------------------------------------
    // Latch LOW
    // --------------------------------------------------------

    *latchPort &= ~latchMask;


    // --------------------------------------------------------
    // Hardware SPI
    // --------------------------------------------------------

    SPI.transfer(digitSelect[currentDigit]);
    SPI.transfer(segments);


    // --------------------------------------------------------
    // Latch HIGH
    // --------------------------------------------------------

    *latchPort |= latchMask;


    // --------------------------------------------------------
    // Next digit
    // --------------------------------------------------------

    currentDigit++;

    if (currentDigit >= 4)
    {
        currentDigit = 0;
    }
}


// ============================================================
// Main loop
// ============================================================

void loop()
{
    static unsigned long lastUpdate = 0;


    if (millis() - lastUpdate >= UPDATE_INTERVAL_MS)
    {
        lastUpdate += UPDATE_INTERVAL_MS;


        // ----------------------------------------------------
        // Increment number
        // ----------------------------------------------------

        number++;

        if (number >= 10000)
        {
            number = 0;
        }


        // ----------------------------------------------------
        // Determine digits
        // ----------------------------------------------------

        uint8_t thousands = BLANK;
        uint8_t hundreds  = BLANK;
        uint8_t tens      = BLANK;
        uint8_t ones      = number % 10;


        if (number >= 10)
        {
            tens = (number / 10) % 10;
        }

        if (number >= 100)
        {
            hundreds = (number / 100) % 10;
        }

        if (number >= 1000)
        {
            thousands = (number / 1000) % 10;
        }


        // ----------------------------------------------------
        // Build display
        // ----------------------------------------------------

        uint32_t newDisplay =
            packDisplay(
                thousands,
                hundreds,
                tens,
                ones
            );


        // ----------------------------------------------------
        // Atomic update
        // ----------------------------------------------------

        noInterrupts();

        displayValue = newDisplay;

        interrupts();
    }
}