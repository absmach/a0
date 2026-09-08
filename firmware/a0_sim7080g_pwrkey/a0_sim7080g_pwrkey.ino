// A0 (ESP32-C6) <-> SIM7080G, PWRKEY driven from GPIO1 again.
//
// PWRKEY ABSOLUTE MAXIMUM IS 2.1 V (Table 28). GPIO1 must never source into
// it. Both wiring options below only ever PULL DOWN and RELEASE.
//
// Pick the one matching your board:
//
//  OPEN_DRAIN (=1)  Q2 removed, R11 removed, Q2 pad 2 bridged to pad 1 so
//                   GPIO1 reaches PWRKEY through R8's 4.7 K. GPIO1 is
//                   open-drain: LOW pulls PWRKEY down, HIGH is high-Z and
//                   PWRKEY floats to ~1.5 V on its internal 1.8 V pull-up.
//                   R8 stays as a current limiter: if this pin is ever set
//                   to push-pull OUTPUT by mistake, it caps the fault at
//                   ~250 uA into the internal clamp instead of 3.3 V.
//                   VERIFY ONCE WITH A METER: asserted PWRKEY must be <0.4 V.
//
//  OPEN_DRAIN (=0)  A correctly-oriented NPN is fitted on the Q2 pads
//                   (base->pad 2, emitter->pad 3, collector->pad 1).
//                   GPIO1 HIGH turns it on and pulls PWRKEY low. This is the
//                   original design intent and the more robust option:
//                   Vce(sat) ~ 0.05 V regardless of pull-up stiffness.
//
// Timing (SIM7080G_Hardware_Design_V1.04):
//   Ton        1 s min, 12.6 s max   low pulse to power ON (>12.6 s = reset)
//   Toff       1.2 s min             low pulse to power OFF
//   Ton(uart)  1.8 s min             power-on issue to UART ready
//   Toff-on    2 s min               buffer between power-off and power-on
//
// Build with USB CDC On Boot = Enabled, or Serial lands on UART0 (GPIO16/17)
// and fights the modem for the same two pins.

// How PWRKEY is driven. PWRKEY ABSOLUTE MAXIMUM IS 2.1 V (Table 28), so
// GPIO1 must never present 3.3 V to it.
//
//   2 DIVIDER     R8 4.7k in series, R11 replaced with 4.7k to GND, Q2 pads
//                 bridged. Push-pull GPIO. HIGH -> 1.65 V (released),
//                 LOW -> 0.10 V (asserted). The divider clamps the worst
//                 case to ~1.7 V, so no firmware mistake can overstress the
//                 pin, and the levels barely move even if the module's
//                 internal pull-up impedance is 10k or 100k rather than the
//                 ~33k estimated from bench readings.
//
//   1 OPEN_DRAIN  Q2 pads bridged, R11 removed, GPIO1 open-drain. Works, but
//                 the asserted level depends on that internal pull-up, and
//                 ESP32-C6 VOL is spec'd at 0.1 x VDD = 0.33 V against a
//                 0.4 V VIL - typical passes, worst case does not.
//
//   0 NPN         Correctly-oriented NPN on the Q2 pads (base->pad 2,
//                 emitter->pad 3, collector->pad 1). Best levels and no
//                 standing current, but needs three rerouted wires.
#define DRIVE_DIVIDER    2
#define DRIVE_OPEN_DRAIN 1
#define DRIVE_NPN        0

#define PWRKEY_DRIVE DRIVE_DIVIDER

#define MODEM_RX_PIN 16  // ESP32 RX <- SIM7080G TX (via U1 B1)
#define MODEM_TX_PIN 17  // ESP32 TX -> SIM7080G RX (via U1 B2)
#define PWRKEY_PIN    1

#if PWRKEY_DRIVE == DRIVE_DIVIDER
  // Push-pull is required here: the 4.7k shunt to GND would drag PWRKEY to
  // ~0.19 V if the pin ever went high-Z, i.e. permanently asserted.
  #define PWRKEY_MODE    OUTPUT
  #define PWRKEY_ASSERT  LOW
  #define PWRKEY_RELEASE HIGH
  static const char *kWiring = "4.7k/4.7k divider (LOW asserts)";
#elif PWRKEY_DRIVE == DRIVE_OPEN_DRAIN
  #define PWRKEY_MODE    OUTPUT_OPEN_DRAIN
  #define PWRKEY_ASSERT  LOW
  #define PWRKEY_RELEASE HIGH
  static const char *kWiring = "open-drain direct (LOW asserts)";
#else
  #define PWRKEY_MODE    OUTPUT
  #define PWRKEY_ASSERT  HIGH
  #define PWRKEY_RELEASE LOW
  static const char *kWiring = "NPN on Q2 pads (HIGH asserts)";
#endif

#define MODEM_BAUD 115200

HardwareSerial Modem(1);

static void dumpByte(uint8_t b) {
  if (b == '\r' || b == '\n' || (b >= 0x20 && b < 0x7F)) Serial.write(b);
  else Serial.printf("[%02X]", b);
}

static void drain(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    while (Modem.available()) dumpByte(Modem.read());
  }
}

static bool sendAT(const String &cmd, uint32_t timeoutMs) {
  while (Modem.available()) Modem.read();
  Modem.print(cmd);
  Modem.print("\r\n");

  String reply;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (Modem.available()) {
      char c = (char)Modem.read();
      reply += c;
      dumpByte((uint8_t)c);
    }
    if (reply.indexOf("OK") >= 0 || reply.indexOf("ERROR") >= 0) break;
  }
  return reply.indexOf("OK") >= 0;
}

// Autobaud is the shipped default, so the modem needs a few uppercase "AT"
// strings before it locks on. Early attempts returning nothing is normal.
static bool probeAT(uint8_t attempts) {
  for (uint8_t i = 1; i <= attempts; i++) {
    Serial.printf("\n  AT %u/%u: ", i, attempts);
    if (sendAT("AT", 1000)) { Serial.println("\n  -> OK"); return true; }
    delay(400);
  }
  Serial.println("\n  -> no response");
  return false;
}

// One pulse. holdMs picks what it means: ~1.1 s turns a stopped modem on
// without reaching the 1.2 s power-off threshold, so it is safe to send at an
// unknown state. 2 s is more reliable for power-on but WILL power down a
// modem that is already running.
static void pulsePwrkey(uint32_t holdMs) {
  Serial.printf("PWRKEY asserted for %lu ms (%s)\n",
                (unsigned long)holdMs, kWiring);
  digitalWrite(PWRKEY_PIN, PWRKEY_ASSERT);
  delay(holdMs);
  digitalWrite(PWRKEY_PIN, PWRKEY_RELEASE);
  Serial.println("PWRKEY released");
}

static void powerOn(uint32_t holdMs) {
  pulsePwrkey(holdMs);
  Serial.println("Waiting for UART (Ton(uart) >= 1.8 s):");
  Serial.print("    ");
  drain(6000);
  Serial.println();

  if (probeAT(12)) {
    Serial.println("\n*** MODEM IS UP ***\n");
    sendAT("ATI", 2000);
    sendAT("AT+CSQ", 2000);
    sendAT("AT+CPIN?", 2000);
  } else {
    Serial.println("\nNo response. Check, in order:");
    Serial.println("  1. +1V8 at C3. 1.8 V = booted, 0 V = pulse did not take.");
    Serial.println("  2. PWRKEY while asserted - must be below 0.4 V.");
    Serial.println("     Above that and the modem never sees a valid low.");
    Serial.println("  3. 'watch' to see whether the modem TX line moves at all.");
  }
}

// Report the modem TX line without touching power state.
static void watchLine(uint32_t ms) {
  Serial.printf("\nWatching GPIO%d for %lu ms...\n", MODEM_RX_PIN,
                (unsigned long)ms);
  Modem.end();
  pinMode(MODEM_RX_PIN, INPUT);
  delay(5);

  uint32_t edges = 0, minPulse = 0xFFFFFFFF, last = micros();
  int prev = digitalRead(MODEM_RX_PIN);
  Serial.printf("  start level: %s\n", prev ? "HIGH" : "LOW");

  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    int now = digitalRead(MODEM_RX_PIN);
    if (now != prev) {
      uint32_t held = micros() - last;
      if (held && held < minPulse) minPulse = held;
      last = micros();
      prev = now;
      edges++;
    }
  }

  Serial.printf("  edges=%lu  final=%s  ", (unsigned long)edges,
                prev ? "HIGH" : "LOW");
  if (minPulse != 0xFFFFFFFF) Serial.printf("min pulse=%lu us", (unsigned long)minPulse);
  else Serial.print("no transitions - nothing is driving the line");
  Serial.println();

  Modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
}

static void help() {
  Serial.println();
  Serial.println("  on      - 1.1 s pulse (safe at unknown state: turns on,");
  Serial.println("            too short to power off a running modem)");
  Serial.println("  on2     - 2 s pulse (more reliable power-on, but WILL");
  Serial.println("            power down a modem that is already running)");
  Serial.println("  off     - 1.5 s pulse to power down, then wait 2 s");
  Serial.println("  watch   - sample the modem TX line, no power change");
  Serial.println("  help    - this list");
  Serial.println("  <other> - sent to the modem as an AT command");
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  // Release before enabling the driver so the pin cannot glitch low at boot.
  digitalWrite(PWRKEY_PIN, PWRKEY_RELEASE);
  pinMode(PWRKEY_PIN, PWRKEY_MODE);
  digitalWrite(PWRKEY_PIN, PWRKEY_RELEASE);

  Modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  delay(1500);
  Serial.println();
  Serial.println("========================================");
  Serial.println(" A0 / SIM7080G  - PWRKEY on GPIO1");
  Serial.println("========================================");
  Serial.printf("Wiring: %s\n", kWiring);
#if PWRKEY_DRIVE == DRIVE_DIVIDER
  Serial.println("Expect PWRKEY 1.55-1.65 V released, ~0.10 V asserted.");
  Serial.println("Released near 0.2 V  -> the Q2 pad 2-to-1 bridge is missing.");
  Serial.println("Released near 2.8 V  -> shunt is still 47k. OVER the 2.1 V");
  Serial.println("   absolute max: power down and fix before leaving it on.");
#elif PWRKEY_DRIVE == DRIVE_OPEN_DRAIN
  Serial.println("Confirm ONCE with a meter: PWRKEY asserted must be <0.4 V,");
  Serial.println("released ~1.5 V. If asserted sits higher, bypass R8 or");
  Serial.println("switch to DRIVE_DIVIDER.");
#endif
  Serial.println();
  help();
}

void loop() {
  static String line;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line.length()) {
        if (line.equalsIgnoreCase("on"))         powerOn(1100);
        else if (line.equalsIgnoreCase("on2"))   powerOn(2000);
        else if (line.equalsIgnoreCase("off"))   { pulsePwrkey(1500); delay(2000); }
        else if (line.equalsIgnoreCase("watch")) watchLine(3000);
        else if (line.equalsIgnoreCase("help"))  help();
        else                                     sendAT(line, 3000);
        line = "";
      }
    } else {
      line += c;
    }
  }

  while (Modem.available()) dumpByte(Modem.read());
}
