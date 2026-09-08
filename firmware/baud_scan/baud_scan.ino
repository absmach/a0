// One-off diagnostic: does NOT touch PWRKEY. Assumes the modem is already
// powered on from a previous run. Tries AT at several common baud rates and
// reports which one gets a reply, or whether nothing comes back at all.

#define MODEM_RX_PIN 16
#define MODEM_TX_PIN 17

HardwareSerial Modem(1);

const long BAUDS[] = {115200, 9600, 57600, 38400, 19200, 4800};
const int NUM_BAUDS = sizeof(BAUDS) / sizeof(BAUDS[0]);

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Baud scan starting (PWRKEY untouched)...");

  for (int i = 0; i < NUM_BAUDS; i++) {
    long baud = BAUDS[i];
    Modem.begin(baud, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
    delay(200);
    while (Modem.available()) Modem.read();

    Serial.printf("--- %ld baud ---\n", baud);
    for (int attempt = 0; attempt < 3; attempt++) {
      Modem.print("AT\r\n");
      unsigned long start = millis();
      String reply;
      while (millis() - start < 800) {
        while (Modem.available()) reply += (char)Modem.read();
      }
      if (reply.length() > 0) {
        Serial.print("  raw reply: [");
        for (size_t j = 0; j < reply.length(); j++) {
          Serial.printf("%02X ", (uint8_t)reply[j]);
        }
        Serial.println("]");
      } else {
        Serial.println("  (nothing)");
      }
      delay(200);
    }
    Modem.end();
    delay(100);
  }
  Serial.println("Baud scan done.");
}

void loop() {}
