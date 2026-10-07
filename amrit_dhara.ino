/*
  AMRIT DHARA - Intelligent Water Purification & Quality Monitoring
  ESP32 firmware:  ESP32 -> MQTT (TLS, port 8883) -> HiveMQ Cloud -> Flutter app

  Flow: purify -> monitor -> verify -> dispense.
  Dispensing is only allowed while the purified water passes every check,
  and the relay is switched off automatically if a check fails.

  Libraries (Library Manager):
    - PubSubClient            (Nick O'Leary)
    - LiquidCrystal_I2C       (Frank de Brabander)
  Board package: "esp32" by Espressif, version 2.0.x or newer.

  HiveMQ Cloud setup: console.hivemq.cloud -> create a (free) cluster, copy its host name,
  then Access Management -> create a username/password with "Publish and Subscribe".

  MQTT topics (TOPIC_BASE = amritdhara/unit01 by default)
    <base>/telemetry   device -> dashboard   JSON readings, every PUBLISH_MS
    <base>/status      device -> dashboard   "online" / "offline" (retained, last will)
    <base>/spec        device -> dashboard   JSON limits (retained)
    <base>/event       device -> dashboard   text events (dispense_started, ...)
    <base>/cmd         dashboard -> device   "dispense" or "stop"

  !! Items marked CHECK below are assumptions. Verify them against your
  !! hardware before trusting the numbers.
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =====================================================================
// 1. USER SETTINGS
// =====================================================================
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASS     = "YOUR_WIFI_PASSWORD";

// HiveMQ Cloud: host name is shown on your cluster's Overview page.
const char*    MQTT_HOST     = "YOUR-CLUSTER-ID.s1.eu.hivemq.cloud";
const uint16_t MQTT_PORT     = 8883;                  // MQTT over TLS
const char*    MQTT_USER     = "YOUR_HIVEMQ_USERNAME";
const char*    MQTT_PASSWORD = "YOUR_HIVEMQ_PASSWORD";

// true  = check the broker's certificate (recommended)
// false = encrypted but NOT verified; only for a quick test
const bool VERIFY_TLS = true;

// Root certificate of HiveMQ Cloud (Let's Encrypt "ISRG Root X1", valid until June 2035).
const char* ROOT_CA = R"EOF(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";

// Topic prefix. The Flutter app must use the same value.
#define TOPIC_BASE "amritdhara/unit01"

// =====================================================================
// 2. PINS (from your pin plan)
// =====================================================================
#define PIN_PH_RAW     32   // green pH module, raw water (ADC1)
#define PIN_TDS_IN     33   // TDS sensor #1, before filter (ADC1)
#define PIN_TDS_OUT    34   // TDS sensor #2, after filter (ADC1, input only)
#define PIN_TURBIDITY  35   // turbidity sensor (ADC1, input only)

#define RS485_RX       16   // from RS485-to-TTL module RO (must be 3.3 V!)
#define RS485_TX       17   // to RS485-to-TTL module DI
#define RS485_DE_RE     4   // DE and RE tied together

#define I2C_SDA        21
#define I2C_SCL        22

// ---- Added by me, NOT in your pin plan. CHECK / change to match your wiring ----
#define PIN_RELAY      25   // dispense valve / pump relay
#define RELAY_ACTIVE_LOW true
#define PIN_BUTTON     27   // optional push button to GND (start / stop)

// LCD I2C addresses (which screen shows what is my choice, swap freely)
LiquidCrystal_I2C lcdRaw   (0x27, 16, 2);   // raw water
LiquidCrystal_I2C lcdOut   (0x26, 16, 2);   // purified water
LiquidCrystal_I2C lcdMisc  (0x25, 16, 2);   // turbidity + temperature
LiquidCrystal_I2C lcdStatus(0x24, 16, 2);   // filter efficiency + dispense status

// =====================================================================
// 3. WATER-QUALITY LIMITS (purified water)
// =====================================================================
const float TDS_OUT_MAX         = 500.0f;   // ppm
const float PH_MIN              = 6.5f;
const float PH_MAX              = 8.5f;
const float TURB_MAX_NTU        = 5.0f;     // CHECK: assumes the turbidity sensor sits on purified water
const float FILTER_EFF_MIN_PCT  = 50.0f;    // (TDS_in - TDS_out) / TDS_in
const float EFF_MIN_INPUT_TDS   = 50.0f;    // below this raw TDS, efficiency is not meaningful
const uint32_t DISPENSE_MAX_MS  = 30000;    // auto-stop a single dispense after 30 s

// =====================================================================
// 4. SENSOR CALIBRATION
// =====================================================================
const float TDS_K = 1.0f;                   // multiply TDS to calibrate against a 342 ppm / 1413 uS/cm solution

// Green pH module: output goes through a divider so the ESP32 sees <= 3.3 V.
// CHECK: ratio = (R1 + R2) / R2 of YOUR divider. Use 1.0 if PO goes straight to the ESP32
// (only safe if PO never exceeds 3.3 V).
const float PH_DIVIDER_RATIO = 1.5f;
const float PH_NEUTRAL_V     = 2.50f;       // module voltage at pH 7.0 (calibrate with pH 6.86 buffer)
const float PH_V_PER_PH      = 0.18f;       // module volts per pH unit (calibrate with pH 4.01 buffer)
const float PH_OFFSET        = 0.0f;

// Turbidity module: same idea. CHECK your divider.
const float TURB_DIVIDER_RATIO = 1.5f;

// RS-485 (Modbus RTU) pH + temperature sensor ZTS-3001-TR-THPH-N01.
// CHECK: address, baud rate and register map against the sensor datasheet!
const uint8_t  MB_ADDR       = 0x01;
const uint32_t MB_BAUD       = 4800;
const uint16_t MB_START_REG  = 0x0000;
const uint8_t  MB_REG_COUNT  = 2;           // how many registers to read from MB_START_REG
const uint8_t  MB_PH_INDEX   = 0;           // which of those registers holds pH
const float    MB_PH_SCALE   = 0.01f;       // pH   = raw * scale
const uint8_t  MB_TEMP_INDEX = 1;           // which register holds temperature (signed)
const float    MB_TEMP_SCALE = 0.1f;        // degC = raw * scale

// =====================================================================
// 5. TIMING
// =====================================================================
const uint32_t SENSE_MS   = 1000;
const uint32_t PUBLISH_MS = 2000;

// =====================================================================
// Internals
// =====================================================================
#define TOPIC_TELEMETRY TOPIC_BASE "/telemetry"
#define TOPIC_STATUS    TOPIC_BASE "/status"
#define TOPIC_SPEC      TOPIC_BASE "/spec"
#define TOPIC_EVENT     TOPIC_BASE "/event"
#define TOPIC_CMD       TOPIC_BASE "/cmd"

const uint8_t F_SENSOR = 1, F_PH = 2, F_TDS = 4, F_TURB = 8, F_FILTER = 16;

struct Readings {
  float tdsIn, tdsOut, phRaw, phOut, tempC, turbNTU, effPct;
  bool  rs485Ok, effValid;
} rd = {0, 0, 0, NAN, NAN, 0, 0, false, false};

uint8_t  faults          = F_SENSOR;   // fail-safe: blocked until the first good reading
bool     haveData        = false;
bool     dispensing      = false;
uint32_t dispenseStartMs = 0;
bool     publishNow      = false;

WiFiClientSecure wifiClient;
PubSubClient mqtt(wifiClient);

// ---------------------------------------------------------------------
// Relay
// ---------------------------------------------------------------------
void setRelay(bool on) {
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? !on : on);
}

// ---------------------------------------------------------------------
// Analog helpers
// ---------------------------------------------------------------------
// Median-trimmed average of 25 samples, returns volts at the ESP32 pin.
float readVolts(int pin) {
  const int N = 25;
  int s[N];
  for (int i = 0; i < N; i++) {
    s[i] = analogReadMilliVolts(pin);
    delayMicroseconds(400);
  }
  for (int i = 1; i < N; i++) {                 // insertion sort
    int k = s[i], j = i - 1;
    while (j >= 0 && s[j] > k) { s[j + 1] = s[j]; j--; }
    s[j + 1] = k;
  }
  long sum = 0;
  for (int i = 5; i < N - 5; i++) sum += s[i];  // drop 5 lowest and 5 highest
  return sum / (float)(N - 10) / 1000.0f;
}

float tdsFromVolts(float v, float tempC) {
  float comp = 1.0f + 0.02f * (tempC - 25.0f);
  float vc = v / comp;
  float tds = (133.42f * vc * vc * vc - 255.86f * vc * vc + 857.39f * vc) * 0.5f * TDS_K;
  return tds < 0 ? 0 : tds;
}

float phFromVolts(float vAtPin) {
  float vModule = vAtPin * PH_DIVIDER_RATIO;
  return 7.0f + (PH_NEUTRAL_V - vModule) / PH_V_PER_PH + PH_OFFSET;
}

float turbidityFromVolts(float vAtPin) {
  float v = vAtPin * TURB_DIVIDER_RATIO;
  if (v < 2.5f) return 3000.0f;                 // very dirty / sensor unplugged
  float ntu = -1120.4f * v * v + 5742.3f * v - 4352.9f;
  if (ntu < 0) ntu = 0;
  if (ntu > 3000.0f) ntu = 3000.0f;
  return ntu;
}

// ---------------------------------------------------------------------
// Modbus RTU over RS-485 (function 0x03, read holding registers)
// ---------------------------------------------------------------------
uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
  }
  return crc;
}

bool modbusRead(uint8_t addr, uint16_t start, uint8_t count, uint16_t* out) {
  if (count == 0 || count > 12) return false;
  uint8_t req[8] = {addr, 0x03, (uint8_t)(start >> 8), (uint8_t)(start & 0xFF),
                    0x00, count, 0, 0};
  uint16_t c = crc16(req, 6);
  req[6] = c & 0xFF;
  req[7] = c >> 8;

  while (Serial2.available()) Serial2.read();   // flush stale bytes
  digitalWrite(RS485_DE_RE, HIGH);              // transmit
  delayMicroseconds(300);
  Serial2.write(req, sizeof(req));
  Serial2.flush();
  delayMicroseconds(300);
  digitalWrite(RS485_DE_RE, LOW);               // receive

  const size_t expected = 5 + (size_t)count * 2;
  uint8_t resp[32];
  size_t n = 0;
  uint32_t t0 = millis();
  while (n < expected && millis() - t0 < 300) {
    if (Serial2.available()) resp[n++] = Serial2.read();
  }
  if (n < expected) return false;
  if (resp[0] != addr || resp[1] != 0x03 || resp[2] != count * 2) return false;
  uint16_t crc = crc16(resp, expected - 2);
  if (resp[expected - 2] != (crc & 0xFF) || resp[expected - 1] != (crc >> 8)) return false;
  for (uint8_t i = 0; i < count; i++) out[i] = ((uint16_t)resp[3 + 2 * i] << 8) | resp[4 + 2 * i];
  return true;
}

// ---------------------------------------------------------------------
// Read + evaluate
// ---------------------------------------------------------------------
void readSensors() {
  uint16_t regs[12];
  rd.rs485Ok = modbusRead(MB_ADDR, MB_START_REG, MB_REG_COUNT, regs);
  if (rd.rs485Ok) {
    rd.phOut = regs[MB_PH_INDEX] * MB_PH_SCALE;
    rd.tempC = (int16_t)regs[MB_TEMP_INDEX] * MB_TEMP_SCALE;
  } else {
    rd.phOut = NAN;
    rd.tempC = NAN;
  }
  float t = rd.rs485Ok ? rd.tempC : 25.0f;      // temperature compensation for both TDS probes

  rd.tdsIn   = tdsFromVolts(readVolts(PIN_TDS_IN),  t);
  rd.tdsOut  = tdsFromVolts(readVolts(PIN_TDS_OUT), t);
  rd.phRaw   = phFromVolts(readVolts(PIN_PH_RAW));
  rd.turbNTU = turbidityFromVolts(readVolts(PIN_TURBIDITY));

  rd.effValid = rd.tdsIn >= EFF_MIN_INPUT_TDS;
  if (rd.effValid) {
    rd.effPct = (rd.tdsIn - rd.tdsOut) / rd.tdsIn * 100.0f;
    if (rd.effPct > 100.0f) rd.effPct = 100.0f;
  } else {
    rd.effPct = 0;
  }
}

void evaluate() {
  faults = 0;
  if (!rd.rs485Ok) faults |= F_SENSOR;                                   // fail-safe
  else if (rd.phOut < PH_MIN || rd.phOut > PH_MAX) faults |= F_PH;
  if (rd.tdsOut > TDS_OUT_MAX) faults |= F_TDS;
  if (rd.turbNTU > TURB_MAX_NTU) faults |= F_TURB;
  if (rd.effValid && rd.effPct < FILTER_EFF_MIN_PCT) faults |= F_FILTER;
}

// ---------------------------------------------------------------------
// LCDs
// ---------------------------------------------------------------------
void lcdLine(LiquidCrystal_I2C& lcd, uint8_t row, const char* text) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void updateLcds() {
  char l[32];

  snprintf(l, sizeof(l), "RAW TDS:%4.0fppm", rd.tdsIn);   lcdLine(lcdRaw, 0, l);
  snprintf(l, sizeof(l), "RAW pH :%5.2f", rd.phRaw);      lcdLine(lcdRaw, 1, l);

  snprintf(l, sizeof(l), "OUT TDS:%4.0fppm", rd.tdsOut);  lcdLine(lcdOut, 0, l);
  if (rd.rs485Ok) snprintf(l, sizeof(l), "OUT pH :%5.2f", rd.phOut);
  else            snprintf(l, sizeof(l), "OUT pH : ERR");
  lcdLine(lcdOut, 1, l);

  snprintf(l, sizeof(l), "TURB:%6.2f NTU", rd.turbNTU);   lcdLine(lcdMisc, 0, l);
  if (rd.rs485Ok) snprintf(l, sizeof(l), "TEMP:%6.1f C", rd.tempC);
  else            snprintf(l, sizeof(l), "TEMP:   ERR");
  lcdLine(lcdMisc, 1, l);

  if (rd.effValid) snprintf(l, sizeof(l), "Filter eff:%3.0f%%", rd.effPct);
  else             snprintf(l, sizeof(l), "Filter eff: n/a");
  lcdLine(lcdStatus, 0, l);

  if (dispensing) {
    snprintf(l, sizeof(l), "DISPENSING...");
  } else if (faults) {
    snprintf(l, sizeof(l), "BLOCK:");
    if (faults & F_SENSOR) strlcat(l, "SENS ", sizeof(l));
    if (faults & F_TDS)    strlcat(l, "TDS ",  sizeof(l));
    if (faults & F_PH)     strlcat(l, "PH ",   sizeof(l));
    if (faults & F_TURB)   strlcat(l, "TURB ", sizeof(l));
    if (faults & F_FILTER) strlcat(l, "FILT",  sizeof(l));
  } else {
    snprintf(l, sizeof(l), "READY: WATER OK");
  }
  lcdLine(lcdStatus, 1, l);
}

// ---------------------------------------------------------------------
// MQTT publishing
// ---------------------------------------------------------------------
void num(char* out, size_t n, float v, int dec) {
  if (isnan(v)) strlcpy(out, "null", n);
  else snprintf(out, n, "%.*f", dec, v);
}

void buildReasons(char* out, size_t n) {
  out[0] = 0;
  auto add = [&](uint8_t flag, const char* name) {
    if (faults & flag) {
      if (out[0]) strlcat(out, ",", n);
      strlcat(out, "\"", n);
      strlcat(out, name, n);
      strlcat(out, "\"", n);
    }
  };
  add(F_SENSOR, "sensor_fault");
  add(F_TDS,    "tds_high");
  add(F_PH,     "ph_range");
  add(F_TURB,   "turb_high");
  add(F_FILTER, "filter_low");
}

void publishEvent(const char* text) {
  Serial.printf("[event] %s\n", text);
  if (mqtt.connected()) mqtt.publish(TOPIC_EVENT, text);
  publishNow = true;
}

void publishSpec() {
  char json[128];
  snprintf(json, sizeof(json),
           "{\"tds_max\":%.0f,\"ph_min\":%.1f,\"ph_max\":%.1f,\"turb_max\":%.1f,\"eff_min\":%.0f}",
           TDS_OUT_MAX, PH_MIN, PH_MAX, TURB_MAX_NTU, FILTER_EFF_MIN_PCT);
  mqtt.publish(TOPIC_SPEC, json, true);
}

void publishTelemetry() {
  if (!mqtt.connected()) return;
  char a[14], b[14], c[14], d[14], e[14], f[14], g[14];
  num(a, sizeof(a), rd.tdsIn,   0);
  num(b, sizeof(b), rd.tdsOut,  0);
  num(c, sizeof(c), rd.phRaw,   2);
  num(d, sizeof(d), rd.phOut,   2);
  num(e, sizeof(e), rd.tempC,   1);
  num(f, sizeof(f), rd.turbNTU, 2);
  num(g, sizeof(g), rd.effValid ? rd.effPct : NAN, 0);
  char reasons[96];
  buildReasons(reasons, sizeof(reasons));

  char json[420];
  snprintf(json, sizeof(json),
           "{\"tds_in\":%s,\"tds_out\":%s,\"ph_raw\":%s,\"ph_out\":%s,\"temp\":%s,"
           "\"turb\":%s,\"eff\":%s,\"quality\":\"%s\",\"reasons\":[%s],"
           "\"dispensing\":%s,\"rssi\":%d,\"uptime\":%lu}",
           a, b, c, d, e, f, g,
           faults ? "BLOCKED" : "OK", reasons,
           dispensing ? "true" : "false",
           WiFi.RSSI(), (unsigned long)(millis() / 1000));
  mqtt.publish(TOPIC_TELEMETRY, json);
}

// ---------------------------------------------------------------------
// Dispense control
// ---------------------------------------------------------------------
void requestDispense() {
  if (dispensing) return;
  if (!haveData || faults) {
    publishEvent("dispense_blocked");
    return;
  }
  dispensing = true;
  dispenseStartMs = millis();
  setRelay(true);
  updateLcds();
  publishEvent("dispense_started");
}

void stopDispense(const char* why) {
  if (!dispensing) return;
  dispensing = false;
  setRelay(false);
  updateLcds();
  publishEvent(why);
}

void onMqttMessage(char* topic, byte* payload, unsigned int len) {
  char cmd[16];
  size_t n = len < sizeof(cmd) - 1 ? len : sizeof(cmd) - 1;
  memcpy(cmd, payload, n);
  cmd[n] = 0;
  for (char* p = cmd; *p; ++p) *p = tolower(*p);

  if (!strcmp(cmd, "dispense"))  requestDispense();
  else if (!strcmp(cmd, "stop")) stopDispense("dispense_stopped");
}

// ---------------------------------------------------------------------
// Connectivity (non-blocking: sensing and the safety interlock keep
// running even when Wi-Fi or the broker is down)
// ---------------------------------------------------------------------
void wifiEnsure() {
  static uint32_t last = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - last < 10000) return;
  last = millis();
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void mqttEnsure() {
  static uint32_t last = 0;
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (VERIFY_TLS && time(nullptr) < 1700000000) return;   // wait for NTP: certificate dates need the real time
  if (last && millis() - last < 5000) return;
  last = millis();

  char cid[32];
  snprintf(cid, sizeof(cid), "amritdhara-%08X", (uint32_t)ESP.getEfuseMac());
  bool ok = mqtt.connect(cid, MQTT_USER, MQTT_PASSWORD, TOPIC_STATUS, 1, true, "offline");

  if (ok) {
    Serial.println("[mqtt] connected");
    mqtt.publish(TOPIC_STATUS, "online", true);
    publishSpec();
    mqtt.subscribe(TOPIC_CMD);
    publishNow = true;
  } else {
    char tlsErr[100] = "";
    wifiClient.lastError(tlsErr, sizeof(tlsErr));
    Serial.printf("[mqtt] failed, rc=%d, tls: %s\n", mqtt.state(), tlsErr);
    // rc=4 or 5 = wrong username/password (HiveMQ Access Management)
  }
}

// ---------------------------------------------------------------------
// Push button (press = start, press again = stop)
// ---------------------------------------------------------------------
void handleButton() {
  static bool lastRaw = HIGH, stable = HIGH;
  static uint32_t t = 0;
  bool raw = digitalRead(PIN_BUTTON);
  if (raw != lastRaw) { t = millis(); lastRaw = raw; }
  if (millis() - t > 40 && raw != stable) {
    stable = raw;
    if (stable == LOW) {
      if (dispensing) stopDispense("dispense_stopped");
      else requestDispense();
    }
  }
}

// ---------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------
void setup() {
  // Relay OFF before anything else (write the level first, then make it an output)
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false);

  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);
  lcdRaw.init();    lcdRaw.backlight();
  lcdOut.init();    lcdOut.backlight();
  lcdMisc.init();   lcdMisc.backlight();
  lcdStatus.init(); lcdStatus.backlight();
  lcdLine(lcdStatus, 0, "AMRIT DHARA");
  lcdLine(lcdStatus, 1, "Starting...");

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_PH_RAW,    ADC_11db);
  analogSetPinAttenuation(PIN_TDS_IN,    ADC_11db);
  analogSetPinAttenuation(PIN_TDS_OUT,   ADC_11db);
  analogSetPinAttenuation(PIN_TURBIDITY, ADC_11db);

  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);
  Serial2.begin(MB_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);

  if (VERIFY_TLS) wifiClient.setCACert(ROOT_CA);
  else            wifiClient.setInsecure();
  configTime(0, 0, "pool.ntp.org", "time.google.com");   // real time is needed to validate the certificate

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
  t0 = millis();
  while (VERIFY_TLS && WiFi.status() == WL_CONNECTED && time(nullptr) < 1700000000 && millis() - t0 < 10000) delay(250);
  lcdLine(lcdStatus, 1, WiFi.status() == WL_CONNECTED ? "WiFi connected" : "WiFi offline");
  delay(600);
}

void loop() {
  static uint32_t lastSense = 0, lastPublish = 0;
  uint32_t now = millis();

  wifiEnsure();
  mqttEnsure();
  mqtt.loop();
  handleButton();

  if (!haveData || now - lastSense >= SENSE_MS) {
    lastSense = now;
    readSensors();
    evaluate();
    haveData = true;
    updateLcds();
    Serial.printf("TDS in %.0f out %.0f | pH raw %.2f out %.2f | T %.1f | turb %.2f | eff %.0f%% | faults 0x%02X\n",
                  rd.tdsIn, rd.tdsOut, rd.phRaw, rd.phOut, rd.tempC, rd.turbNTU, rd.effPct, faults);
  }

  // Safety interlock: never keep dispensing bad water, never dispense forever
  if (dispensing) {
    if (faults) stopDispense("dispense_auto_stopped_quality");
    else if (now - dispenseStartMs > DISPENSE_MAX_MS) stopDispense("dispense_auto_stopped_timeout");
  }

  if (publishNow || now - lastPublish >= PUBLISH_MS) {
    publishNow = false;
    lastPublish = now;
    publishTelemetry();
  }
}
