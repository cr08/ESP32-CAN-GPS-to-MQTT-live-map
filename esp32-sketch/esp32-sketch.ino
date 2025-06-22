#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "conf.h"
#include "driver/twai.h"

// ===== MQTT Setup =====
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ===== Shared GPS Data Structure =====
struct GPSData {
  float lat = 0.0;
  float lon = 0.0;
  float heading = 0.0;
  float speed = 0.0;
  float hdop = 0.0;
  float vdop = 0.0;
  float altitude = 0.0;
  int sats = 0;
  String fix = "Unknown";
  String actual = "false";

  bool nav1_received = false;
  bool nav2_received = false;
  bool nav3_received = false;

  void resetFlags() {
    nav1_received = nav2_received = nav3_received = false;
  }

  bool isComplete() const {
    return nav1_received && nav2_received && nav3_received;
  }
};

GPSData gpsData;

// ===== Extract MSB-First Bit Fields =====
uint64_t extractBits(const uint8_t* data, uint8_t startBit, uint8_t length) {
  if (length == 0 || length > 64 || (startBit + length) > 64) return 0;
  uint64_t value = 0;
  for (uint8_t i = 0; i < length; ++i) {
    uint8_t bitPos = startBit + i;
    uint8_t byteIndex = bitPos / 8;
    uint8_t bitIndex = 7 - (bitPos % 8);
    if (data[byteIndex] & (1 << bitIndex)) {
      value |= (1ULL << (length - 1 - i));
    }
  }
  return value;
}

// ===== GPS Decoder: Nav 1 (Lat/Lon) =====
void decodeGPSNav1(const uint8_t* data, size_t len) {
  uint64_t raw_lat_deg     = extractBits(data, 0, 8);
  uint64_t raw_lat_min     = extractBits(data, 8, 6);
  uint64_t raw_lat_min_dec = extractBits(data, 16, 14);
  uint64_t lat_hemi        = extractBits(data, 30, 2);  // 2=N, 1=S

  uint64_t raw_lon_deg     = extractBits(data, 32, 9);
  uint64_t raw_lon_min     = extractBits(data, 41, 6);
  uint64_t raw_lon_min_dec = extractBits(data, 48, 14);
  uint64_t lon_hemi        = extractBits(data, 14, 2);  // 2=W, 1=E

  float lat_deg = fabs(raw_lat_deg - 89.0f);
  float lat_min = raw_lat_min + raw_lat_min_dec * 0.0001f;
  gpsData.lat = lat_deg + lat_min / 60.0f;
  if (lat_hemi == 1) gpsData.lat *= -1.0f;

  float lon_deg = fabs(raw_lon_deg - 179.0f);
  float lon_min = raw_lon_min + raw_lon_min_dec * 0.0001f;
  gpsData.lon = lon_deg + lon_min / 60.0f;
  if (lon_hemi == 2) gpsData.lon *= -1.0f;

  gpsData.nav1_received = true;
}

// ===== GPS Decoder: Nav 2 (Actual/Infer) =====
void decodeGPSNav2(const uint8_t* data, size_t len) {
  uint64_t actual = extractBits(data, 33, 1);
  gpsData.actual = (actual == 0) ? "true" : "false";
  gpsData.nav2_received = true;
}

// ===== GPS Decoder: Nav 3 (Fix, Sat, Speed, etc) =====
void decodeGPSNav3(const uint8_t* data, size_t len) {
  gpsData.sats = extractBits(data, 0, 5);
  uint64_t fix = extractBits(data, 5, 3);
  uint64_t alt = extractBits(data, 8, 12);
  uint64_t heading = extractBits(data, 24, 16);
  uint64_t speed = extractBits(data, 40, 8);
  uint64_t hdop = extractBits(data, 48, 5);
  uint64_t vdop = extractBits(data, 56, 5);

  gpsData.altitude = alt * 10.0f - 20460.0f;
  gpsData.heading = heading * 0.01f;
  gpsData.speed = (speed <= 253) ? speed : 0.0f;
  gpsData.hdop = hdop * 0.2f;
  gpsData.vdop = vdop * 0.2f;

  switch (fix) {
    case 0: gpsData.fix = "No_Fix"; break;
    case 1: gpsData.fix = "2D"; break;
    case 2: gpsData.fix = "3D"; break;
    default: gpsData.fix = "Unknown"; break;
  }

  gpsData.nav3_received = true;
}

// ===== Publish Once All Fields Received =====
void tryPublish() {
  if (!gpsData.isComplete()) return;

  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"lat\":%.6f,\"lon\":%.6f,\"heading\":%.2f,\"speed\":%.2f,"
    "\"hdop\":%.1f,\"vdop\":%.1f,\"altitude\":%.1f,\"sats\":%d,"
    "\"fix\":\"%s\",\"actual\":\"%s\"}",
    gpsData.lat, gpsData.lon, gpsData.heading, gpsData.speed,
    gpsData.hdop, gpsData.vdop, gpsData.altitude, gpsData.sats,
    gpsData.fix.c_str(), gpsData.actual.c_str()
  );

  Serial.println("📤 Full MQTT Payload:");
  Serial.println(payload);
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC, payload);
  }

  gpsData.resetFlags();
}

// ===== Wi-Fi Setup =====
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("🔌 Connecting to Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\n📶 Connected. IP: ");
  Serial.println(WiFi.localIP());
}

// ===== MQTT Setup =====
void connectMQTT() {
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  while (!mqttClient.connected()) {
    Serial.print("🔁 Connecting to MQTT...");
    bool ok = (strlen(MQTT_USER) > 0)
                ? mqttClient.connect("ESP32_GPS", MQTT_USER, MQTT_PASS)
                : mqttClient.connect("ESP32_GPS");
    if (ok) Serial.println("✅ MQTT Connected.");
    else {
      Serial.print("❌ Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(". Retrying...");
      delay(2000);
    }
  }
}

// ===== CAN Setup =====
void setupCAN() {
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_4, GPIO_NUM_5, TWAI_MODE_NORMAL);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_driver_install(&g_config, &t_config, &f_config);
  twai_start();
  Serial.println("🚌 CAN Bus started (500Kbps)");
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  connectWiFi();
  connectMQTT();
  setupCAN();
}

// ===== Main Loop =====
void loop() {
  mqttClient.loop();

  twai_message_t frame;
  if (twai_receive(&frame, pdMS_TO_TICKS(10)) == ESP_OK) {
    if (frame.data_length_code == 8) {
      switch (frame.identifier) {
        case 0x0462: decodeGPSNav1(frame.data, 8); break;  // Lat/Lon
        case 0x0466: decodeGPSNav2(frame.data, 8); break;  // Actual/Infer
        case 0x0467: decodeGPSNav3(frame.data, 8); break;  // Heading, Sat, Alt
      }
      tryPublish();
    }
  }

  // Reconnect handling
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ Wi-Fi disconnected. Reconnecting...");
    connectWiFi();
  }
  if (!mqttClient.connected()) {
    connectMQTT();
  }
}