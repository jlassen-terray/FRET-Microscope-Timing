const int INPUT_PIN = 2;
const int LED_PIN = 3;

// Timer1 at 16 MHz with prescaler 256 gives 62,500 counts/sec.
// CTC counts 0..OCR1A inclusive, so 1 second is 62,499 -- not 62,500.
const uint16_t TIMER_COMPARE = 62499;

volatile bool inputSeen = false;
volatile bool timerExpired = false;
volatile bool ledState = false;

void setup() {
  Serial.begin(9600);

  pinMode(INPUT_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);

  // Stop Timer1
  TCCR1A = 0;
  TCCR1B = 0;

  OCR1A = TIMER_COMPARE;

  // Enable Timer1 compare interrupt
  TIMSK1 |= (1 << OCIE1A);

  // Input interrupt
  attachInterrupt(digitalPinToInterrupt(INPUT_PIN), inputTriggered, FALLING);
}

void loop() {
  // Serial output happens here, never in an ISR. Writing to the UART inside
  // an interrupt blocks for ~1 ms per line at 9600 baud, which would distort
  // the very timing this sketch exists to measure.
  if (inputSeen) {
    inputSeen = false;

    Serial.println("Input Triggered");
  }

  if (timerExpired) {
    timerExpired = false;

    digitalWrite(LED_PIN, ledState);

    Serial.println("Timer Expired");
  }
}

void inputTriggered() {
  // Reset timer
  TCNT1 = 0;

  // Discard any compare match left pending from a previous run, otherwise
  // the ISR fires immediately instead of one second from now.
  TIFR1 = (1 << OCF1A);

  // CTC mode, prescaler = 256
  TCCR1B = (1 << WGM12) | (1 << CS12);

  inputSeen = true;
}

ISR(TIMER1_COMPA_vect) {
  // Stop timer
  TCCR1B = 0;

  ledState = !ledState;

  // Tell loop() that the timer expired
  timerExpired = true;
}
