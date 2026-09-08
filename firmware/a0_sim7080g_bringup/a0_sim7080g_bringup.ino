// A0 (ESP32-C6) <-> SIM7080G bring-up harness.
//
// For a board with Q2 REMOVED. GPIO1 no longer reaches PWRKEY at all, so this
// sketch cannot power the modem — it prompts YOU to pulse PWRKEY by hand and
// times the sequence against the SIM7080G_Hardware_Design_V1.04 numbers.
//
// Touch PWRKEY (U2 pin 39 / Q2 pad 1) to GND only. Never to a rail: the pin's
// absolute maximum is 2.1 V (Table 28) and it idles at ~1.5 V from its own
// internal diode pull-up to 1.8 V.
//
// Datasheet timings used below:
//   Ton         1 s min, 12.6 s max   PWRKEY low to power ON (>12.6 s = reset)
//   Toff        1.2 s min             PWRKEY low to power OFF
//   Ton(uart)   1.8 s min             power-on issue to UART ready
//   Toff-on     2 s min               buffer between power-off and power-on
//
// MUST be built with USB CDC On Boot = Enabled, or Serial lands on UART0
// (GPIO16/17) and fights the modem for the same two pins.

#define MODEM_RX_PIN   16  // ESP32 RX  <- SIM7080G TX  (via U1 B1)
#define MODEM_TX_PIN   17  // ESP32 TX  -> SIM7080G RX  (via U1 B2)
#define MODEM_PWRKEY    1  // dead with Q2 removed; held as INPUT

#define MODEM_BAUD 115200

HardwareSerial Modem(1);

static const uint32_t kBauds[] = {115200, 9600, 19200, 38400, 57600, 230400};

// GPIO16 is also the C6's U0TXD, wired to the modem's TX output through U1 —
// two outputs on one node whenever UART0 is alive. Counting edges here says
// whether the modem is actually transmitting, which a DMM cannot: an idle
// UART is just a DC high, and real data is microseconds wide.
static volatile uint32_t g_edges = 0;

static void IRAM_ATTR onRxEdge() { g_edges++; }

static void reportLine(const char *when) {
  Serial.printf("  [%s] GPIO16 level=%s  edges=%lu\n", when,
                digitalRead(MODEM_RX_PIN) ? "HIGH" : "LOW",
                (unsigned long)g_edges);
}

// Print modem bytes as text, with a hex escape for anything unprintable so a
// baud mismatch reads as garbage-with-hex rather than silence.
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

static void flushModem() {
  while (Modem.available()) Modem.read();
}

// Send one AT command, echo everything that comes back, return true on OK.
static bool sendAT(const String &cmd, uint32_t timeoutMs) {
  flushModem();
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

// The modem ships in autobaud (0 bps default) and needs a few uppercase "AT"
// strings before it locks on, so one attempt proving nothing is expected.
static bool probeAT(uint8_t attempts) {
  for (uint8_t i = 1; i <= attempts; i++) {
    Serial.printf("\n  AT probe %u/%u: ", i, attempts);
    if (sendAT("AT", 1000)) {
      Serial.println("\n  -> OK");
      return true;
    }
    delay(400);
  }
  Serial.println("\n  -> no response");
  return false;
}

static void countdown(const char *what, uint8_t secs) {
  for (uint8_t i = secs; i > 0; i--) {
    Serial.printf("%s in %u...\n", what, i);
    delay(1000);
  }
}

static void guidedPowerOn() {
  Serial.println();
  Serial.println("=== GUIDED POWER-ON ===");
  Serial.println("Have the wire ready on PWRKEY (U2 pin 39 / Q2 pad 1).");
  Serial.println("WARNING: this is a toggle. If the modem is ALREADY running,");
  Serial.println("a 2 s pulse powers it DOWN. Check +1V8 at C3 first if unsure.");
  Serial.println();

  countdown("TOUCH PWRKEY TO GND", 3);

  Serial.println();
  Serial.println(">>> TOUCH PWRKEY TO GND NOW <<<");
  uint32_t t0 = millis();
  for (uint8_t i = 1; i <= 2; i++) {
    delay(1000);
    Serial.printf("    holding... %u.0 s\n", i);
  }
  Serial.println(">>> RELEASE PWRKEY NOW <<<");
  Serial.printf("    (held %lu ms; Ton window is 1000-12600 ms)\n",
                (unsigned long)(millis() - t0));
  Serial.println();

  // Ton(uart) is 1.8 s min from the power-on issue; give it margin and show
  // anything the modem emits on its own (it normally prints RDY).
  reportLine("at release");
  g_edges = 0;

  // Ton(uart) is 1.8 s min from the power-on issue; give it margin and show
  // anything the modem emits on its own (it normally prints RDY).
  Serial.println("Waiting for UART (Ton(uart) >= 1.8 s). Unsolicited output:");
  Serial.print("    ");
  drain(6000);
  Serial.println();
  reportLine("after 6 s");
  if (g_edges == 0) {
    Serial.println("  -> ZERO edges: nothing is driving GPIO16. The modem is");
    Serial.println("     not transmitting, or U1 is not passing A->B.");
  } else {
    Serial.println("  -> edges seen: something IS driving the line. If AT still");
    Serial.println("     fails, it is a baud/framing problem, not a dead link.");
  }

  Serial.println("Probing with AT at 115200...");
  if (probeAT(12)) {
    Serial.println();
    Serial.println("*** MODEM IS UP ***");
    Serial.println();
    sendAT("ATI", 2000);
    sendAT("AT+CSQ", 2000);
    sendAT("AT+CPIN?", 2000);
  } else {
    Serial.println();
    Serial.println("No AT response. Next checks, in order:");
    Serial.println("  1. Measure +1V8 at C3.");
    Serial.println("       1.8 V -> modem booted; UART path is the problem.");
    Serial.println("       0 V   -> it never started; PWRKEY pulse did not take.");
    Serial.println("  2. If +1V8 is good, scope U2 pin 1 (SIM_TX, 1.8 V A-side)");
    Serial.println("     during boot. Activity there but nothing here means U1");
    Serial.println("     is still not passing - recheck the OE bodge and VCCA.");
    Serial.println("  3. Type 'scan' to sweep baud rates.");
    Serial.println("  4. Before pulsing again, wait >= 2 s (Toff-on) and");
    Serial.println("     remember a second pulse on a running modem kills it.");
  }
  Serial.println();
  Serial.println("Type 'help' for commands.");
}

static void baudScan() {
  Serial.println();
  Serial.println("=== BAUD SCAN ===");
  for (uint8_t i = 0; i < sizeof(kBauds) / sizeof(kBauds[0]); i++) {
    Serial.printf("\n-- %lu bps\n", (unsigned long)kBauds[i]);
    Modem.end();
    delay(50);
    Modem.begin(kBauds[i], SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    delay(100);
    if (probeAT(3)) {
      Serial.printf("\n*** RESPONDS AT %lu bps ***\n", (unsigned long)kBauds[i]);
      return;
    }
  }
  Serial.println("\nNo response at any rate. Restoring 115200.");
  Modem.end();
  delay(50);
  Modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
}

static void help() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  go      - guided PWRKEY power-on sequence (prompts you)");
  Serial.println("  scan    - sweep baud rates looking for an AT response");
  Serial.println("  off     - guided power-OFF (1.2 s+ pulse)");
  Serial.println("  help    - this list");
  Serial.println("  <other> - sent to the modem as an AT command");
  Serial.println();
}

static void guidedPowerOff() {
  Serial.println();
  Serial.println("=== GUIDED POWER-OFF ===");
  countdown("TOUCH PWRKEY TO GND", 3);
  Serial.println(">>> TOUCH PWRKEY TO GND NOW <<<");
  delay(1500);  // Toff is 1.2 s min
  Serial.println(">>> RELEASE PWRKEY NOW <<<");
  Serial.println("Wait >= 2 s (Toff-on) before powering on again.");
  Serial.println();
}

void setup() {
  Serial.begin(115200);

  // Q2 is off the board, so this pin drives nothing. Held as an input so it
  // cannot inject anything into the R8/R11 stub.
  pinMode(MODEM_PWRKEY, INPUT);

  Modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  // GPIO16 doubles as U0TXD. Modem.begin() should have released it, but force
  // the output driver off so the ESP32 cannot fight the modem's TX, then watch
  // the line for real transitions.
  pinMode(MODEM_RX_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(MODEM_RX_PIN), onRxEdge, CHANGE);

  delay(1500);  // let the USB CDC console attach before printing
  Serial.println();
  Serial.println("========================================");
  Serial.println(" A0 / SIM7080G bring-up  (Q2 REMOVED)");
  Serial.println("========================================");
  Serial.println("PWRKEY is manual. GPIO1 does nothing on this board.");
  Serial.println("Touch PWRKEY to GND ONLY - never to a rail (2.1 V abs max).");
  Serial.println();
  Serial.println("Before starting, confirm with a meter:");
  Serial.println("  PWRKEY (U2 pin 39) idles at ~1.5 V   <- Q2 really is gone");
  Serial.println("  U1 pin 10 (OE) tied to U1 pin 2 (VCCA)");
  Serial.println();
  Serial.println("Type 'go' when ready, then follow the prompts.");
  Serial.println();
}

void loop() {
  static String line;

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line.length()) {
        if (line.equalsIgnoreCase("go"))        guidedPowerOn();
        else if (line.equalsIgnoreCase("scan")) baudScan();
        else if (line.equalsIgnoreCase("off"))  guidedPowerOff();
        else if (line.equalsIgnoreCase("help")) help();
        else if (line.equalsIgnoreCase("pins")) { g_edges = 0; delay(1000); reportLine("1 s sample"); }
        else                                    sendAT(line, 3000);
        line = "";
      }
    } else {
      line += c;
    }
  }

  // Always surface whatever the modem says on its own.
  while (Modem.available()) dumpByte(Modem.read());
}
