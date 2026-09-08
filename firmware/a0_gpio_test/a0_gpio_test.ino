// GPIO probe tool for A0 (ESP32-C6-MINI-1U). Send a GPIO number over Serial:
// it blinks that pin on/off, 3s each phase, indefinitely. Sending another
// number stops the current pin and starts blinking the new one instead.
//
// Pin list is every GPIO actually broken out on A0 per the ESP32-C6-MINI-1U
// netlist (a0/a0.kicad_sch, component U4) — excludes GPIO12/13 (USB-C D-/D+,
// used for the console) and EN (reset).

const int SAFE_PINS[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
  14, 15, 16, 17, 18, 19, 20, 21, 22, 23
};
const int NUM_SAFE_PINS = sizeof(SAFE_PINS) / sizeof(SAFE_PINS[0]);

const unsigned long BLINK_PERIOD_MS = 3000;

int activePin = -1;
bool ledState = false;
unsigned long lastToggleTime = 0;

bool isSafePin(int pin) {
  for (int i = 0; i < NUM_SAFE_PINS; i++) {
    if (SAFE_PINS[i] == pin) return true;
  }
  return false;
}

void selectPin(int pin) {
  if (activePin != -1 && activePin != pin) {
    digitalWrite(activePin, LOW);
  }
  activePin = pin;
  pinMode(activePin, OUTPUT);
  ledState = true;
  digitalWrite(activePin, HIGH);
  lastToggleTime = millis();
  Serial.printf("Blinking GPIO%d (%lums on/off)\n", pin, BLINK_PERIOD_MS);
}

void setup() {
  Serial.begin(115200);
  Serial.println("A0 GPIO test — send a pin number, e.g. 14");
  Serial.print("Available pins: ");
  for (int i = 0; i < NUM_SAFE_PINS; i++) {
    Serial.print(SAFE_PINS[i]);
    if (i < NUM_SAFE_PINS - 1) Serial.print(", ");
  }
  Serial.println();
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      int pin = line.toInt();
      if (isSafePin(pin)) {
        selectPin(pin);
      } else {
        Serial.printf("GPIO%d is not available for testing on A0.\n", pin);
      }
    }
  }

  if (activePin != -1) {
    unsigned long now = millis();
    if (now - lastToggleTime >= BLINK_PERIOD_MS) {
      ledState = !ledState;
      digitalWrite(activePin, ledState ? HIGH : LOW);
      lastToggleTime = now;
    }
  }
}
