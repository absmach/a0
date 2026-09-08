// Pin-state utility for A0 / SIM7080G bring-up.
//
// GPIO1  (PWRKEY drive) - driven on command so you can meter it at J2 pin 2
//                         or R8's pad and confirm the MCU side works.
// GPIO16 (modem TX in)  - watched continuously: level, edge count, and the
//                         shortest pulse seen, which is one bit period.
//
// SAFETY: GPIO1 boots as an INPUT and only drives when you ask it to.
// This is safe ONLY while GPIO1 is isolated from PWRKEY - i.e. no transistor
// fitted and the Q2 pad 2 -> pad 1 bridge NOT made. If that bridge exists
// without a 4.7k shunt on R11, driving GPIO1 high puts ~2.8 V on PWRKEY,
// over its 2.1 V absolute maximum.
//
// Build with USB CDC On Boot = Enabled.

#define PWRKEY_PIN 1
#define MODEM_RX   16

static volatile uint32_t g_edges = 0;
static volatile uint32_t g_lastUs = 0;
static volatile uint32_t g_minPulse = 0xFFFFFFFF;

static bool     g_driving = false;
static bool     g_square  = false;
static uint32_t g_lastTog = 0;
static const char *g_pull = "floating";

static void IRAM_ATTR onEdge() {
  uint32_t now = micros();
  uint32_t held = now - g_lastUs;
  if (held && held < g_minPulse) g_minPulse = held;
  g_lastUs = now;
  g_edges++;
}

static void setPull(const char *name, uint8_t mode) {
  detachInterrupt(digitalPinToInterrupt(MODEM_RX));
  pinMode(MODEM_RX, mode);
  g_pull = name;
  delay(5);
  Serial.printf("\nGPIO16 %s -> reads %s\n", name,
                digitalRead(MODEM_RX) ? "HIGH" : "LOW");
  if (mode == INPUT_PULLDOWN)
    Serial.println("  stays HIGH = genuinely driven.  goes LOW = floating.");
  attachInterrupt(digitalPinToInterrupt(MODEM_RX), onEdge, CHANGE);
}

static void driveGpio1(int level) {
  g_square = false;
  pinMode(PWRKEY_PIN, OUTPUT);
  digitalWrite(PWRKEY_PIN, level);
  g_driving = true;
  Serial.printf("\nGPIO1 driven %s - meter it at J2 pin 2 or R8's pad.\n",
                level ? "HIGH (expect ~3.3 V)" : "LOW (expect ~0 V)");
}

static void releaseGpio1() {
  g_square = false;
  g_driving = false;
  pinMode(PWRKEY_PIN, INPUT);
  Serial.println("\nGPIO1 released (input, high-Z).");
}

static void help() {
  Serial.println();
  Serial.println("GPIO1 (PWRKEY drive):");
  Serial.println("  h - drive HIGH      l - drive LOW");
  Serial.println("  t - 1 Hz square     z - release (input)");
  Serial.println("GPIO16 (modem TX):");
  Serial.println("  f - floating input  u - pull-up   d - pull-down");
  Serial.println("  r - reset counters  ? - this list");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  pinMode(PWRKEY_PIN, INPUT);   // safe default: drives nothing
  pinMode(MODEM_RX, INPUT);
  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" A0 pin state  -  GPIO1 drive / GPIO16 watch");
  Serial.println("========================================");
  Serial.println("GPIO1 boots as INPUT and drives only on command.");
  Serial.println("Only drive it while GPIO1 is isolated from PWRKEY");
  Serial.println("(no transistor, no Q2 pad 2-to-1 bridge).");
  help();

  g_lastUs = micros();
  attachInterrupt(digitalPinToInterrupt(MODEM_RX), onEdge, CHANGE);
}

void loop() {
  if (g_square && millis() - g_lastTog >= 500) {
    g_lastTog = millis();
    digitalWrite(PWRKEY_PIN, !digitalRead(PWRKEY_PIN));
  }

  static uint32_t last = 0;
  static uint32_t prevEdges = 0;
  if (millis() - last >= 2000) {
    last = millis();
    uint32_t e = g_edges, d = e - prevEdges;
    prevEdges = e;

    Serial.printf("GPIO1: %-18s | GPIO16[%s]: %s edges=%lu (+%lu) ",
                  g_square ? "1 Hz square"
                           : (g_driving ? (digitalRead(PWRKEY_PIN) ? "driven HIGH"
                                                                  : "driven LOW")
                                        : "input (high-Z)"),
                  g_pull,
                  digitalRead(MODEM_RX) ? "HIGH" : "LOW ",
                  (unsigned long)e, (unsigned long)d);

    if (g_minPulse != 0xFFFFFFFF) {
      Serial.printf("min=%lu us", (unsigned long)g_minPulse);
      uint32_t p = g_minPulse;
      if      (p >= 7  && p <= 11)  Serial.print(" ~115200");
      else if (p >= 15 && p <= 20)  Serial.print(" ~57600");
      else if (p >= 23 && p <= 30)  Serial.print(" ~38400");
      else if (p >= 45 && p <= 60)  Serial.print(" ~19200");
      else if (p >= 95 && p <= 115) Serial.print(" ~9600");
    } else {
      Serial.print("no edges");
    }
    Serial.println();
  }

  while (Serial.available()) {
    char c = (char)Serial.read();
    switch (c) {
      case 'h': driveGpio1(HIGH); break;
      case 'l': driveGpio1(LOW);  break;
      case 'z': releaseGpio1();   break;
      case 't':
        pinMode(PWRKEY_PIN, OUTPUT);
        g_driving = true; g_square = true;
        Serial.println("\nGPIO1 toggling at 1 Hz.");
        break;
      case 'f': setPull("floating",  INPUT);          break;
      case 'u': setPull("pull-up",   INPUT_PULLUP);   break;
      case 'd': setPull("pull-down", INPUT_PULLDOWN); break;
      case 'r':
        g_edges = 0; g_minPulse = 0xFFFFFFFF;
        Serial.println("\ncounters cleared");
        break;
      case '?': help(); break;
    }
  }
}
