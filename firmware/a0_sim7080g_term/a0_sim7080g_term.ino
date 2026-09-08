// A0 / SIM7080G terminal, for a board where PWRKEY is pulsed BY HAND.
//
// Confirmed working at this point: the modem boots (VDD_EXT = 1.8 V at C3),
// and U1 passes its TX data through to GPIO16. So this sketch does not touch
// PWRKEY at all - it just shows modem state and talks AT.
//
// GPIO16's idle level doubles as a power-state indicator, because U1's VCCA
// comes from the modem's own VDD_EXT:
//    HIGH = modem on  (TXD idling high through U1)
//    LOW  = modem off (U1 unpowered, line floating)
// STATUS (U2 pin 42) is not routed to the header, so this is the only state
// feedback available. Check it BEFORE pulsing PWRKEY - PWRKEY is a toggle,
// and pulsing a running modem powers it down.
//
// Build with USB CDC On Boot = Enabled, or Serial binds to UART0 on
// GPIO16/17 and fights the modem for those pins.

#define MODEM_RX_PIN 16
#define MODEM_TX_PIN 17
#define MODEM_BAUD   115200

HardwareSerial Modem(1);

static uint32_t g_rxBytes = 0;
static uint32_t g_baud    = MODEM_BAUD;

static void dumpByte(uint8_t b) {
  g_rxBytes++;
  if (b == '\r' || b == '\n' || (b >= 0x20 && b < 0x7F)) Serial.write(b);
  else Serial.printf("[%02X]", b);
}

static bool modemOn() { return digitalRead(MODEM_RX_PIN); }

static void setBaud(uint32_t baud) {
  Modem.end();
  delay(50);
  Modem.begin(baud, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  g_baud = baud;
  delay(100);
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

// Autobaud is the shipped default (0 bps), so the modem needs a few uppercase
// "AT" strings before it locks on. Early silence is expected.
static bool probeAT(uint8_t attempts) {
  for (uint8_t i = 1; i <= attempts; i++) {
    Serial.printf("\n  AT %u/%u: ", i, attempts);
    if (sendAT("AT", 1000)) { Serial.println("\n  -> OK"); return true; }
    delay(300);
  }
  Serial.println("\n  -> no response");
  return false;
}

static void baudScan() {
  static const uint32_t kBauds[] = {115200, 9600, 19200, 38400, 57600,
                                    230400, 460800, 921600};
  if (!modemOn()) {
    Serial.println("\nModem reads OFF (GPIO16 low). Pulse PWRKEY first.");
    return;
  }
  Serial.println("\n=== BAUD SCAN ===");
  for (uint8_t i = 0; i < sizeof(kBauds) / sizeof(kBauds[0]); i++) {
    Serial.printf("\n-- %lu bps", (unsigned long)kBauds[i]);
    setBaud(kBauds[i]);
    if (probeAT(4)) {
      Serial.printf("\n*** RESPONDS AT %lu bps ***\n", (unsigned long)kBauds[i]);
      return;
    }
  }
  Serial.println("\nNothing. Restoring 115200.");
  setBaud(MODEM_BAUD);
}

static void help() {
  Serial.println();
  Serial.println("  at      - probe AT at the current baud");
  Serial.println("  scan    - sweep baud rates");
  Serial.println("  info    - ATI / AT+CSQ / AT+CPIN? / AT+CGMR");
  Serial.println("  b<rate> - set baud, e.g. b9600");
  Serial.println("  ?       - this list");
  Serial.println("  <other> - sent to the modem as an AT command");
  Serial.println();
  Serial.println("PWRKEY is manual. Check the MODEM line below before pulsing:");
  Serial.println("pulsing a running modem powers it DOWN.");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  Modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  pinMode(MODEM_RX_PIN, INPUT);  // ensure UART0's driver is off this pin

  delay(1500);
  Serial.println();
  Serial.println("========================================");
  Serial.println(" A0 / SIM7080G terminal  (manual PWRKEY)");
  Serial.println("========================================");
  help();
}

void loop() {
  static String line;
  static uint32_t last = 0;
  static bool wasOn = false;

  while (Modem.available()) dumpByte(Modem.read());

  if (millis() - last >= 2000) {
    last = millis();
    bool on = modemOn();
    if (on != wasOn) {
      Serial.printf("\n*** modem went %s ***\n", on ? "ON" : "OFF");
      wasOn = on;
    }
    Serial.printf("[MODEM %s | %lu bps | rx=%lu bytes]\n",
                  on ? "ON " : "OFF", (unsigned long)g_baud,
                  (unsigned long)g_rxBytes);
  }

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line.length()) {
        if (line.equalsIgnoreCase("at")) {
          if (!modemOn()) Serial.println("\nModem reads OFF. Pulse PWRKEY first.");
          else probeAT(10);
        } else if (line.equalsIgnoreCase("scan")) {
          baudScan();
        } else if (line.equalsIgnoreCase("info")) {
          sendAT("ATI", 2000);
          sendAT("AT+CSQ", 2000);
          sendAT("AT+CPIN?", 2000);
          sendAT("AT+CGMR", 2000);
        } else if (line.equalsIgnoreCase("?")) {
          help();
        } else if (line.length() > 1 && (line[0] == 'b' || line[0] == 'B') &&
                   isDigit(line[1])) {
          uint32_t r = line.substring(1).toInt();
          if (r >= 300) { setBaud(r); Serial.printf("\nbaud -> %lu\n", (unsigned long)r); }
        } else {
          sendAT(line, 3000);
        }
        line = "";
      }
    } else {
      line += c;
    }
  }
}
