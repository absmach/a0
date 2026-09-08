// A0 / SIM7080G UART probe.
//
// Resolves the contradiction between "the edge counter saw thousands of
// transitions" and "UART1 received zero bytes". Two independent tools:
//
//   cap   detach UART1, timestamp raw edges on GPIO16, and histogram the
//         intervals. The most common SHORT interval is one bit period, so
//         this measures the modem's actual baud rate without depending on
//         UART framing at all.  baud = 1e6 / bit_period_us
//
//   d/u/f switch GPIO16's internal pull (45k typ) to tell a genuinely driven
//         line from a floating one. Against the TXB0108's ~4k output a real
//         drive holds at ~3.0 V; a floating line follows the pull.
//
// PWRKEY is manual. GPIO16's idle level doubles as a power-state indicator,
// because U1's VCCA comes from the modem's own VDD_EXT.
//
// Build with USB CDC On Boot = Enabled.

#define MODEM_RX_PIN 16
#define MODEM_TX_PIN 17

HardwareSerial Modem(1);

static const uint16_t kCapN = 2048;
static volatile uint32_t g_cap[kCapN];
static volatile uint16_t g_capIdx  = 0;
static volatile bool     g_capArm  = false;

static uint32_t g_rxBytes = 0;
static uint32_t g_baud    = 115200;
static bool     g_uartOn  = true;
static const char *g_pull = "floating";

static void IRAM_ATTR capISR() {
  if (g_capArm && g_capIdx < kCapN) g_cap[g_capIdx++] = micros();
}

static void uartOff() {
  if (g_uartOn) { Modem.end(); delay(20); g_uartOn = false; }
  pinMode(MODEM_RX_PIN, INPUT);
}

static void uartOn(uint32_t baud) {
  detachInterrupt(digitalPinToInterrupt(MODEM_RX_PIN));
  if (g_uartOn) { Modem.end(); delay(20); }
  // Nothing may touch this pin's mode after begin() - that is what detaches
  // the peripheral input routing and gives you edges but zero bytes.
  Modem.begin(baud, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  g_baud = baud; g_uartOn = true;
  Serial.printf("\nUART1 attached at %lu bps\n", (unsigned long)baud);
}

static void dumpByte(uint8_t b) {
  g_rxBytes++;
  if (b == '\r' || b == '\n' || (b >= 0x20 && b < 0x7F)) Serial.write(b);
  else Serial.printf("[%02X]", b);
}

static void capture(uint32_t windowMs) {
  uartOff();
  attachInterrupt(digitalPinToInterrupt(MODEM_RX_PIN), capISR, CHANGE);

  Serial.printf("\nCapturing raw edges for %lu ms - pulse PWRKEY NOW if the "
                "modem is off.\n", (unsigned long)windowMs);
  g_capIdx = 0;
  g_capArm = true;
  uint32_t t0 = millis();
  while (millis() - t0 < windowMs && g_capIdx < kCapN) delay(1);
  g_capArm = false;

  uint16_t n = g_capIdx;
  Serial.printf("edges captured: %u%s\n", n, n >= kCapN ? " (buffer full)" : "");
  if (n < 8) {
    Serial.println("Too few edges. The line is not transitioning.");
    attachInterrupt(digitalPinToInterrupt(MODEM_RX_PIN), capISR, CHANGE);
    return;
  }

  // Histogram intervals 1..200 us in 1 us bins. A UART's most common short
  // interval is exactly one bit period.
  static uint16_t hist[201];
  memset(hist, 0, sizeof(hist));
  uint32_t over = 0;
  for (uint16_t i = 1; i < n; i++) {
    uint32_t d = g_cap[i] - g_cap[i - 1];
    if (d >= 1 && d <= 200) hist[d]++;
    else over++;
  }

  Serial.println("\ntop intervals (us : count):");
  for (uint8_t k = 0; k < 6; k++) {
    uint16_t best = 0, bestIdx = 0;
    for (uint16_t i = 1; i <= 200; i++)
      if (hist[i] > best) { best = hist[i]; bestIdx = i; }
    if (!best) break;
    Serial.printf("  %3u us : %u\n", bestIdx, best);
    hist[bestIdx] = 0;
  }
  Serial.printf("  >200us or 0: %lu\n", (unsigned long)over);

  Serial.println("\nIf a clear shortest peak exists, baud = 1000000/that:");
  Serial.println("   8-9us=115200  17us=57600  26us=38400  52us=19200  104us=9600");
  Serial.println("A flat smear with many 1-3us intervals means the line is");
  Serial.println("floating and self-oscillating, not carrying data.");
}

static void pullTest(const char *name, uint8_t mode) {
  uartOff();
  pinMode(MODEM_RX_PIN, mode);
  g_pull = name;
  delay(20);
  Serial.printf("\nGPIO16 %s -> %s\n", name,
                digitalRead(MODEM_RX_PIN) ? "HIGH" : "LOW");
  if (mode == INPUT_PULLDOWN)
    Serial.println("  HIGH = U1 really is driving.  LOW = floating, U1 is not.");
  if (mode == INPUT_PULLUP)
    Serial.println("  LOW = something really pulls it down.  HIGH = floating.");
  attachInterrupt(digitalPinToInterrupt(MODEM_RX_PIN), capISR, CHANGE);
}

static bool sendAT(const String &cmd, uint32_t timeoutMs) {
  if (!g_uartOn) { Serial.println("\nUART detached - type 'uart' first."); return false; }
  while (Modem.available()) Modem.read();
  Modem.print(cmd); Modem.print("\r\n");
  String reply; uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Modem.available()) { char c = Modem.read(); reply += c; dumpByte(c); }
    if (reply.indexOf("OK") >= 0 || reply.indexOf("ERROR") >= 0) break;
  }
  return reply.indexOf("OK") >= 0;
}

static void help() {
  Serial.println();
  Serial.println("  cap      - detach UART, capture raw edges, histogram baud");
  Serial.println("  d / u / f- GPIO16 pull-down / pull-up / float test");
  Serial.println("  uart     - re-attach UART1 at the current baud");
  Serial.println("  b<rate>  - set baud and attach, e.g. b9600");
  Serial.println("  at       - probe AT 10x");
  Serial.println("  scan     - sweep baud rates");
  Serial.println("  ?        - this list");
  Serial.println();
}

static void baudScan() {
  static const uint32_t r[] = {115200, 9600, 19200, 38400, 57600, 230400, 460800};
  for (uint8_t i = 0; i < sizeof(r) / sizeof(r[0]); i++) {
    uartOn(r[i]);
    delay(150);
    for (uint8_t k = 0; k < 4; k++) {
      Serial.printf("  %lu try %u: ", (unsigned long)r[i], k + 1);
      if (sendAT("AT", 800)) { Serial.printf("\n*** OK at %lu ***\n", (unsigned long)r[i]); return; }
      Serial.println();
    }
  }
  Serial.println("no response at any rate");
}

void setup() {
  Serial.begin(115200);
  Modem.begin(g_baud, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  delay(1500);
  Serial.println();
  Serial.println("========================================");
  Serial.println(" A0 / SIM7080G UART probe");
  Serial.println("========================================");
  help();
}

void loop() {
  static String line;
  static uint32_t last = 0;
  static bool wasOn = false;

  if (g_uartOn) while (Modem.available()) dumpByte(Modem.read());

  if (millis() - last >= 2000) {
    last = millis();
    bool on = g_uartOn ? digitalRead(MODEM_RX_PIN) : digitalRead(MODEM_RX_PIN);
    if (on != wasOn) { Serial.printf("\n*** modem went %s ***\n", on ? "ON" : "OFF"); wasOn = on; }
    Serial.printf("[MODEM %s | uart %s %lu | pull %s | rx=%lu]\n",
                  on ? "ON " : "OFF", g_uartOn ? "on " : "off",
                  (unsigned long)g_baud, g_pull, (unsigned long)g_rxBytes);
  }

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line.length()) {
        if      (line.equalsIgnoreCase("cap"))  capture(6000);
        else if (line == "d") pullTest("pull-down", INPUT_PULLDOWN);
        else if (line == "u") pullTest("pull-up",   INPUT_PULLUP);
        else if (line == "f") pullTest("floating",  INPUT);
        else if (line.equalsIgnoreCase("uart")) uartOn(g_baud);
        else if (line.equalsIgnoreCase("at"))   { for (int i=0;i<10;i++){ Serial.printf("  AT %d: ",i+1); if (sendAT("AT",800)) {Serial.println("\n  -> OK"); break;} Serial.println(); delay(300);} }
        else if (line.equalsIgnoreCase("scan")) baudScan();
        else if (line == "?") help();
        else if (line.length() > 1 && (line[0]=='b'||line[0]=='B') && isDigit(line[1])) {
          uint32_t r = line.substring(1).toInt();
          if (r >= 300) uartOn(r);
        }
        else sendAT(line, 3000);
        line = "";
      }
    } else line += c;
  }
}
