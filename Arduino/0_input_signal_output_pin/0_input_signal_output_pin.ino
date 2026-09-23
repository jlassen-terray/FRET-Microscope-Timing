const int BUTTON_PIN = 2;
const int LED_PIN = 3;

void setup() {
  Serial.begin(9600);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
}

bool buttonPinLow = false;

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW)
  {
    digitalWrite(LED_PIN, HIGH);

    if (buttonPinLow == false)
    {
      Serial.println("BUTTON_PIN == LOW");

      buttonPinLow = true;
    }
  }
  else
  {
    digitalWrite(LED_PIN, LOW);

    if (buttonPinLow)
    {
      Serial.println("BUTTON_PIN == HIGH");

      buttonPinLow = false;
    }
  }
}
