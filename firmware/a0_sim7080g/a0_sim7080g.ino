// A0 (ESP32-C6) <-> SIM7080G module, stacked on MiBUS1 (J1/J3).
// Pinout verified from a0/a0.kicad_sch and modules/sim7080g/sim7080g.kicad_sch netlists.
//
// MiBUS1 pin | ESP32-C6  | SIM7080G module
// -----------|-----------|------------------
// J1-1       | GPIO14 *  | DTR
// J1-2       | GPIO1     | PWRKEY
// J1-3       | GPIO2     | RTS
// J1-7 / J3-8| +3V3 / GND| VCC / GND
// J3-1       | GPIO3     | RING
// J3-2       | GPIO0     | CTS
// J3-3       | GPIO16    | TX  (modem TX -> ESP32 RX)
// J3-4       | GPIO17    | RX  (ESP32 TX -> modem RX)
//
// * GPIO14 reaches MiBUS1 through the U1 (SMXS-02K-TP) slide switch on A0 —
//   it must be set to the MiBUS1 side, not MiBUS2, or DTR will float.
//
// RTS/CTS (GPIO2/GPIO0) are left unused here — they need a solder jumper on
// the module to be connected at all, so touching those pins does nothing.
// DTR (GPIO14) is also left unused — the modem ignores it until AT+CSCLK=1
// is set, which we never do here, so it has no effect at power-on defaults.

#define MODEM_RX_PIN   16  // ESP32 RX  <- SIM7080G TX
#define MODEM_TX_PIN   17  // ESP32 TX  -> SIM7080G RX
#define MODEM_PWRKEY   1

#define MODEM_BAUD     115200

HardwareSerial Modem(1);

void modemPowerOn() {
  // GPIO1 does not drive the modem's real PWRKEY pin directly — on the
  // module PCB it drives NPN transistor Q2 (base via R8, base pulled to GND
  // by R11), whose collector is the actual PWRKEY pin (U2/39). That inverts
  // the signal: GPIO1 HIGH turns Q2 on and pulls PWRKEY low (asserted);
  // GPIO1 LOW releases it back high (idle). This also toggles power rather
  // than setting an absolute level — if the modem was already on, this
  // pulse turns it OFF instead.
  //
  // Per SIMCom spec: PWRKEY low >1.0s turns ON, low >1.2s turns OFF. 1100ms
  // sits inside that ON-only window with margin on both sides — a pulse at
  // or above 1200ms risks toggling an already-on modem back off instead.
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);   // idle (PWRKEY released/high)
  delay(100);
  digitalWrite(MODEM_PWRKEY, HIGH);  // assert (PWRKEY pulled low)
  delay(1100);
  digitalWrite(MODEM_PWRKEY, LOW);   // release
  delay(4500);  // SIM7080G needs ~4-5s before UART/RDY comes up
}

String modemSendAT(const String &cmd, unsigned long timeoutMs = 2000) {
  while (Modem.available()) Modem.read();
  Modem.print(cmd);
  Modem.print("\r\n");

  String reply;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (Modem.available()) reply += (char)Modem.read();
    if (reply.indexOf("OK") != -1 || reply.indexOf("ERROR") != -1) break;
  }
  return reply;
}

// SIM7080G needs a few "AT" pulses to autobaud-sync before it replies.
bool modemWaitReady(uint8_t attempts = 10) {
  for (uint8_t i = 0; i < attempts; i++) {
    String reply = modemSendAT("AT", 1000);
    if (reply.indexOf("OK") != -1) return true;
    delay(500);
  }
  return false;
}

void setup() {
  Serial.begin(115200);

  Modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

  Serial.println("Powering on SIM7080G...");
  modemPowerOn();

  if (!modemWaitReady()) {
    Serial.println("Modem did not respond to AT — check power/PWRKEY toggle state and wiring.");
    return;
  }
  Serial.println("Modem is up.");
  Serial.println(modemSendAT("ATI"));
  Serial.println(modemSendAT("AT+CSQ"));
}

void loop() {
  // AT passthrough: type commands in the Serial Monitor, they go to the
  // modem. Send "PWR" to re-pulse PWRKEY by hand (e.g. if the modem powered
  // off instead of on) without reflashing.
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("PWR")) {
      Serial.println("Pulsing PWRKEY...");
      modemPowerOn();
      Serial.println(modemWaitReady() ? "Modem is up." : "Still no response.");
    } else if (cmd.length() > 0) {
      Serial.println(modemSendAT(cmd));
    }
  }
  while (Modem.available()) {
    Serial.write(Modem.read());
  }
}
