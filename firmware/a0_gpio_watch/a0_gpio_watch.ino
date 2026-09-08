// Edge watcher on GPIO16 = MiBUS1_RX = SIM7080G_TX (modem TX, via U1 B1).
//
// Prints every transition with a timestamp and how long the previous state
// lasted. The SHORTEST duration seen is one bit period, which gives the baud
// rate directly:  8.7 us = 115200,  17.4 us = 57600,  26 us = 38400,
//                52 us = 19200,    104 us = 9600.
//
// Nothing here touches PWRKEY. Pulse it by hand while this runs.
//
// Build with USB CDC On Boot = Enabled, otherwise Serial lands on UART0 and
// drives this very pin.

#define WATCH_PIN 16

struct Ev {
  uint32_t us;
  uint8_t  level;
};

static const uint16_t kBufSize = 512;
static volatile Ev       g_buf[kBufSize];
static volatile uint16_t g_head = 0;
static volatile uint16_t g_tail = 0;
static volatile uint32_t g_dropped = 0;

static uint32_t g_edges   = 0;
static uint32_t g_minPulse = 0xFFFFFFFF;
static uint32_t g_lastUs  = 0;
static uint8_t  g_lastLvl = 0;
static const char *g_mode = "floating";

static void IRAM_ATTR onEdge() {
  uint16_t next = (uint16_t)((g_head + 1) % kBufSize);
  if (next == g_tail) { g_dropped++; return; }
  g_buf[g_head].us    = micros();
  g_buf[g_head].level = (uint8_t)digitalRead(WATCH_PIN);
  g_head = next;
}

static void setMode(const char *name, uint8_t mode) {
  detachInterrupt(digitalPinToInterrupt(WATCH_PIN));
  pinMode(WATCH_PIN, mode);
  g_mode = name;
  delay(5);
  Serial.printf("\n-- input mode: %s, line now reads %s\n", name,
                digitalRead(WATCH_PIN) ? "HIGH" : "LOW");
  if (mode == INPUT_PULLDOWN)
    Serial.println("   stays HIGH = something is really driving it."
                   "  goes LOW = the line is floating.");
  if (mode == INPUT_PULLUP)
    Serial.println("   stays LOW = something is really pulling it down."
                   "  goes HIGH = floating.");
  attachInterrupt(digitalPinToInterrupt(WATCH_PIN), onEdge, CHANGE);
}

static void resetStats() {
  noInterrupts();
  g_head = g_tail = 0;
  g_dropped = 0;
  interrupts();
  g_edges = 0;
  g_minPulse = 0xFFFFFFFF;
  Serial.println("\n-- counters cleared");
}

static void banner() {
  Serial.println();
  Serial.println("========================================");
  Serial.printf(" GPIO%d edge watcher  (modem TX line)\n", WATCH_PIN);
  Serial.println("========================================");
  Serial.println("Pulse PWRKEY to GND by hand and watch this.");
  Serial.println();
  Serial.println("  f = float (plain input)   u = pull-up");
  Serial.println("  d = pull-down             r = reset counters");
  Serial.println();
  Serial.println("Shortest pulse = one bit time:");
  Serial.println("  8.7us=115200  17us=57600  26us=38400  52us=19200  104us=9600");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  pinMode(WATCH_PIN, INPUT);
  delay(1500);
  banner();
  g_lastLvl = digitalRead(WATCH_PIN);
  g_lastUs  = micros();
  Serial.printf("start: line is %s\n\n", g_lastLvl ? "HIGH" : "LOW");
  attachInterrupt(digitalPinToInterrupt(WATCH_PIN), onEdge, CHANGE);
}

void loop() {
  // Drain the ISR ring buffer.
  while (g_tail != g_head) {
    Ev e;
    noInterrupts();
    e = *(const Ev *)&g_buf[g_tail];
    g_tail = (uint16_t)((g_tail + 1) % kBufSize);
    interrupts();

    uint32_t held = e.us - g_lastUs;
    g_edges++;
    if (held < g_minPulse && held > 0) g_minPulse = held;

    // Only print the first 200 edges of a burst; a real UART frame would
    // otherwise flood the console faster than it can be read.
    if (g_edges <= 200) {
      Serial.printf("[%8lu us] %s -> %s   (held %lu us)\n",
                    (unsigned long)e.us,
                    g_lastLvl ? "HIGH" : "LOW ",
                    e.level   ? "HIGH" : "LOW ",
                    (unsigned long)held);
    } else if (g_edges == 201) {
      Serial.println("... further edges suppressed, see the 2 s summary ...");
    }

    g_lastUs  = e.us;
    g_lastLvl = e.level;
  }

  // Periodic summary so a quiet line is still reported.
  static uint32_t lastReport = 0;
  static uint32_t lastEdges  = 0;
  if (millis() - lastReport >= 2000) {
    lastReport = millis();
    uint32_t d = g_edges - lastEdges;
    lastEdges = g_edges;

    Serial.printf("[%s] level=%s  edges=%lu (+%lu)  ",
                  g_mode,
                  digitalRead(WATCH_PIN) ? "HIGH" : "LOW",
                  (unsigned long)g_edges, (unsigned long)d);
    if (g_minPulse != 0xFFFFFFFF) {
      Serial.printf("min pulse=%lu us", (unsigned long)g_minPulse);
      if (g_minPulse >= 7 && g_minPulse <= 11)        Serial.print("  -> ~115200");
      else if (g_minPulse >= 15 && g_minPulse <= 20)  Serial.print("  -> ~57600");
      else if (g_minPulse >= 23 && g_minPulse <= 30)  Serial.print("  -> ~38400");
      else if (g_minPulse >= 45 && g_minPulse <= 60)  Serial.print("  -> ~19200");
      else if (g_minPulse >= 95 && g_minPulse <= 115) Serial.print("  -> ~9600");
    } else {
      Serial.print("no edges yet");
    }
    if (g_dropped) Serial.printf("  [dropped %lu]", (unsigned long)g_dropped);
    Serial.println();
  }

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'f') setMode("floating", INPUT);
    else if (c == 'u') setMode("pull-up", INPUT_PULLUP);
    else if (c == 'd') setMode("pull-down", INPUT_PULLDOWN);
    else if (c == 'r') resetStats();
  }
}
