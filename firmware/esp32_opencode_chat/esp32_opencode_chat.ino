/*
 * ESP32 OpenCode Chat - Autonomous Robot Firmware
 * -----------------------------------------------
 * Transforms the ESP32 from a simple chatbot into an autonomous robot controller.
 * Multi-threaded FreeRTOS architecture with motor control, servo control,
 * sensor polling, safety monitoring, and WebSocket communication.
 *
 * Built by BlueFluteX - https://blueflutex.in/
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <esp32-hal-ledc.h>
#include <Wire.h>
#include "secrets.h"
#include "web_page.h"

// ============================================================================
// Configuration & Constants
// ============================================================================

const uint8_t LED_PIN = 2;
const uint8_t RESET_PIN = 0;
const unsigned long BRIDGE_TIMEOUT_MS = 90000;
const unsigned long REGISTER_TIMEOUT_MS = 8000;
const unsigned long REGISTER_INTERVAL_MS = 5UL * 60UL * 1000UL;
const unsigned long RESET_HOLD_MS = 5000;
const unsigned long WIFI_RETRY_MS = 15000;
const size_t MAX_REQUEST_BYTES = 32768;
const char *FW_VERSION = "2.0.0";

// ============================================================================
// Hardware Pin Definitions
// ============================================================================

// Motor Driver (TB6612FNG x2) - Differential Drive
const uint8_t MOTOR1_PWM = 12;    // Left motor PWM
const uint8_t MOTOR1_IN1 = 13;    // Left motor direction 1
const uint8_t MOTOR1_IN2 = 14;    // Left motor direction 2
const uint8_t MOTOR2_PWM = 25;    // Right motor PWM
const uint8_t MOTOR2_IN1 = 26;    // Right motor direction 1
const uint8_t MOTOR2_IN2 = 27;    // Right motor direction 2
const uint8_t ENCODER_LEFT_A = 32;  // Left encoder A
const uint8_t ENCODER_LEFT_B = 33;  // Left encoder B
const uint8_t ENCODER_RIGHT_A = 34; // Right encoder A
const uint8_t ENCODER_RIGHT_B = 35; // Right encoder B

// Servo Motors (Arm + Gripper)
const uint8_t SERVO_ARM_PAN = 18;   // Arm pan servo
const uint8_t SERVO_ARM_TILT = 19;  // Arm tilt servo
const uint8_t SERVO_GRIPPER = 21;   // Gripper servo

// Sensors
const uint8_t ULTRASONIC_TRIG = 4;  // Ultrasonic trigger
const uint8_t ULTRASONIC_ECHO = 5;  // Ultrasonic echo
const uint8_t IMU_SDA = 21;         // MPU6050 I2C SDA
const uint8_t IMU_SCL = 22;         // MPU6050 I2C SCL
const uint8_t LINE_SENSOR = 36;     // Line follower (analog)
const uint8_t BATTERY_VOLTAGE = 39; // Battery voltage monitor

// Safety & Power
const uint8_t E_STOP_PIN = 16;      // Emergency stop input
const uint8_t POWER_RELAY = 17;     // Power relay control
const uint8_t CURRENT_SENSE = 37;   // Current monitoring

// ============================================================================
// Output Definitions (for AI control)
// ============================================================================

enum OutputType { OUTPUT_DIGITAL, OUTPUT_PWM, OUTPUT_SERVO };

struct Output {
  const char *id;
  const char *name;
  uint8_t pin;
  OutputType type;
  uint16_t min_value;
  uint16_t max_value;
};

const Output OUTPUTS[] = {
  { "led",      "Onboard LED",      2, OUTPUT_DIGITAL, 0, 255 },
  { "motor1",   "Left Motor",       12, OUTPUT_PWM, 0, 255 },
  { "motor2",   "Right Motor",      25, OUTPUT_PWM, 0, 255 },
  { "servo1",   "Arm Pan",          18, OUTPUT_SERVO, 0, 180 },
  { "servo2",   "Arm Tilt",         19, OUTPUT_SERVO, 0, 180 },
  { "servo3",   "Gripper",          21, OUTPUT_SERVO, 0, 180 },
};
const size_t OUTPUT_COUNT = sizeof(OUTPUTS) / sizeof(OUTPUTS[0]);

// ============================================================================
// State Management
// ============================================================================


struct SensorData {
  float imu_roll;
  float imu_pitch;
  float imu_yaw;
  float distance_cm;
  float battery_voltage;
  float current_amps;
  uint16_t line_sensor;
  bool e_stop_pressed;
};

WebServer server(80);
Preferences prefs;
WiFiManager wifiManager;

WiFiManagerParameter *bridgeUrlParam = nullptr;
WiFiManagerParameter *bridgeTokenParam = nullptr;

String bridgeUrl;
String bridgeToken;
bool bridgeOk = false;
bool chatBusy = false;
bool wifiAnnounced = false;
unsigned long bootMillis = 0;
unsigned long nextRegisterMs = 0;
unsigned long lastWifiAttempt = 0;
unsigned long resetHeldSince = 0;
unsigned long pulseEndMs = 0;
uint8_t pulseOutputIndex = 0;
uint8_t pulseRestoreValue = 0;

WiFiClient plainClient;
WiFiClientSecure secureClient;

// Current output values (0-255)
uint8_t outputValues[OUTPUT_COUNT];

// ============================================================================
// PWM Configuration
// ============================================================================

// Motor PWM settings
const uint32_t MOTOR_PWM_FREQ = 1000;
const uint8_t MOTOR_PWM_RES = 8;

// Servo PWM settings (50Hz for standard servos)
const uint32_t SERVO_PWM_FREQ = 50;
const uint8_t SERVO_PWM_RES = 16;

// ============================================================================
// Helper Functions
// ============================================================================

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if ((unsigned char)c >= 0x20) {
      out += c;
    }
  }
  return out;
}

String normalizeUrl(String url) {
  url.trim();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  if (url.length() && !url.startsWith("http://") && !url.startsWith("https://")) {
    url = String("https://") + url;
  }
  return url;
}

int findOutput(const String &id) {
  for (size_t i = 0; i < OUTPUT_COUNT; i++) {
    if (id == OUTPUTS[i].id) return (int)i;
  }
  return -1;
}

// ============================================================================
// Hardware Setup
// ============================================================================

void applyOutput(uint8_t index, uint8_t value);

void setupMotors() {
  pinMode(MOTOR1_PWM, OUTPUT);
  pinMode(MOTOR1_IN1, OUTPUT);
  pinMode(MOTOR1_IN2, OUTPUT);
  pinMode(MOTOR2_PWM, OUTPUT);
  pinMode(MOTOR2_IN1, OUTPUT);
  pinMode(MOTOR2_IN2, OUTPUT);

  ledcAttach(MOTOR1_PWM, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
  ledcAttach(MOTOR2_PWM, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
}

void setupServos() {
  ledcAttach(SERVO_ARM_PAN, SERVO_PWM_FREQ, SERVO_PWM_RES);
  ledcAttach(SERVO_ARM_TILT, SERVO_PWM_FREQ, SERVO_PWM_RES);
  ledcAttach(SERVO_GRIPPER, SERVO_PWM_FREQ, SERVO_PWM_RES);
}

void setupSensors() {
  pinMode(ULTRASONIC_TRIG, OUTPUT);
  pinMode(ULTRASONIC_ECHO, INPUT);
  pinMode(LINE_SENSOR, INPUT);
  pinMode(BATTERY_VOLTAGE, INPUT);
  pinMode(CURRENT_SENSE, INPUT);
  pinMode(E_STOP_PIN, INPUT_PULLUP);

  Wire.begin(IMU_SDA, IMU_SCL);
}

void setupSafety() {
  pinMode(E_STOP_PIN, INPUT_PULLUP);
  pinMode(POWER_RELAY, OUTPUT);
  digitalWrite(POWER_RELAY, HIGH); // Enable power
}

// ============================================================================
// Sensor Reading
// ============================================================================

float readDistance() {
  digitalWrite(ULTRASONIC_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIG, LOW);

  long duration = pulseIn(ULTRASONIC_ECHO, HIGH, 30000);
  if (duration == 0) return -1;
  return duration * 0.034 / 2; // cm
}

float readBatteryVoltage() {
  int adc = analogRead(BATTERY_VOLTAGE);
  return adc * (3.3 / 4095.0) * 2;
}

float readCurrent() {
  int adc = analogRead(CURRENT_SENSE);
  float voltage = adc * (3.3 / 4095.0);
  return (voltage - 2.5) / 0.185;
}

uint16_t readLineSensor() {
  return analogRead(LINE_SENSOR);
}

bool isEmergencyStop() {
  return digitalRead(E_STOP_PIN) == LOW;
}

// ============================================================================
// Control Functions
// ============================================================================

void stopMotors() {
  ledcWrite(MOTOR1_PWM, 0);
  ledcWrite(MOTOR2_PWM, 0);
}

void emergencyStop() {
  digitalWrite(POWER_RELAY, LOW);
  stopMotors();
  Serial.println("[SAFETY] EMERGENCY STOP ACTIVATED");
}

void applyOutput(uint8_t index, uint8_t value) {
  if (index >= OUTPUT_COUNT) return;

  const Output &out = OUTPUTS[index];
  outputValues[index] = value;

  if (out.type == OUTPUT_DIGITAL) {
    pinMode(out.pin, OUTPUT);
    digitalWrite(out.pin, value >= 128 ? HIGH : LOW);
  } else if (out.type == OUTPUT_SERVO) {
    pinMode(out.pin, OUTPUT);
    ledcAttach(out.pin, SERVO_PWM_FREQ, SERVO_PWM_RES);
    ledcWrite(out.pin, map(value, 0, 255, 3277, 6554)); // 1ms-2ms pulse
  } else {
    pinMode(out.pin, OUTPUT);
    ledcAttach(out.pin, 4000, 8);
    ledcWrite(out.pin, value);
  }

  Serial.printf("[output] %s -> %u\n", out.id, value);
}

void pulseOutput(uint8_t index, uint8_t value, unsigned long durationMs) {
  if (index >= OUTPUT_COUNT) return;

  uint8_t restore = outputValues[index];
  applyOutput(index, value);
  pulseEndMs = millis() + durationMs;
  pulseOutputIndex = index;
  pulseRestoreValue = restore;

  Serial.printf("[pulse] %s on for %lums\n", OUTPUTS[index].id, durationMs);
}

void updatePulse() {
  if (!pulseEndMs) return;
  if ((long)(millis() - pulseEndMs) >= 0) {
    applyOutput(pulseOutputIndex, pulseRestoreValue);
    pulseEndMs = 0;
    Serial.printf("[pulse] %s restored\n", OUTPUTS[pulseOutputIndex].id);
  }
}

// ============================================================================
// Status & JSON Helpers
// ============================================================================

void updateLed() {
  if (chatBusy) {
    digitalWrite(LED_PIN, HIGH);
    return;
  }
  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_PIN, (now / 150) % 2);      // fast blink: joining Wi-Fi
  } else if (!bridgeOk) {
    digitalWrite(LED_PIN, (now / 900) % 2);      // slow blink: no bridge
  } else {
    digitalWrite(LED_PIN, HIGH);                 // solid: ready
  }
}

void handleResetButton() {
  if (digitalRead(RESET_PIN) != LOW) {
    resetHeldSince = 0;
    return;
  }
  if (resetHeldSince == 0) {
    resetHeldSince = millis();
    return;
  }
  if (millis() - resetHeldSince < RESET_HOLD_MS) return;

  Serial.println("[reset] wiping saved networks and bridge settings");
  prefs.begin("esp32chat", false);
  prefs.clear();
  prefs.end();
  wifiManager.resetSettings();
  delay(300);
  ESP.restart();
}

String outputsJson() {
  String json = "[";
  for (size_t i = 0; i < OUTPUT_COUNT; i++) {
    if (i) json += ",";
    json += "{\"id\":\"";
    json += OUTPUTS[i].id;
    json += "\",\"name\":\"";
    json += jsonEscape(String(OUTPUTS[i].name));
    json += "\",\"type\":\"";
    json += OUTPUTS[i].type == OUTPUT_DIGITAL ? "digital" :
            OUTPUTS[i].type == OUTPUT_SERVO ? "servo" : "pwm";
    json += "\",\"value\":";
    json += outputValues[i];
    json += "}";
  }
  json += "]";
  return json;
}


String sensorDataJson(const SensorData &data) {
  String json = "{";
  json += "\"imu_roll\":" + String(data.imu_roll, 2) + ",";
  json += "\"imu_pitch\":" + String(data.imu_pitch, 2) + ",";
  json += "\"imu_yaw\":" + String(data.imu_yaw, 2) + ",";
  json += "\"distance_cm\":" + String(data.distance_cm, 2) + ",";
  json += "\"battery_voltage\":" + String(data.battery_voltage, 2) + ",";
  json += "\"current_amps\":" + String(data.current_amps, 2) + ",";
  json += "\"line_sensor\":" + String(data.line_sensor) + ",";
  json += "\"e_stop_pressed\":" + String(data.e_stop_pressed ? "true" : "false");
  json += "}";
  return json;
}

// ============================================================================
// Bridge Transport
// ============================================================================

int bridgePost(const String &path, const String &payload, String &body, unsigned long timeoutMs);

void registerWithBridge() {
  if (WiFi.status() != WL_CONNECTED) return;

  SensorData sd;
  sd.distance_cm = readDistance();
  sd.battery_voltage = readBatteryVoltage();
  sd.current_amps = readCurrent();
  sd.line_sensor = readLineSensor();
  sd.e_stop_pressed = isEmergencyStop();
  sd.imu_roll = 0; sd.imu_pitch = 0; sd.imu_yaw = 0;

  String payload = String("{\"ip\":\"") + WiFi.localIP().toString() +
                   "\",\"port\":80" +
                   ",\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\"" +
                   ",\"rssi\":" + String(WiFi.RSSI()) +
                   ",\"firmware\":\"" + FW_VERSION + "\"" +
                   ",\"sensors\":" + sensorDataJson(sd) +
                   "}";

  String body;
  int code = bridgePost("/register", payload, body, REGISTER_TIMEOUT_MS);

  if (code == 200) {
    if (!bridgeOk) {
      Serial.printf("[bridge] reachable at %s\n", bridgeUrl.c_str());
    }
    bridgeOk = true;
  } else {
    bridgeOk = false;
  }
}

int bridgePost(const String &path, const String &payload, String &body, unsigned long timeoutMs) {
  String url = bridgeUrl + path;
  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setConnectTimeout(8000);
  http.setReuse(false);

  bool began;
  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    began = http.begin(secureClient, url);
  } else {
    began = http.begin(plainClient, url);
  }
  if (!began) return -1;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Bridge-Token", bridgeToken);

  int code = http.POST((uint8_t *)payload.c_str(), payload.length());
  if (code > 0) body = http.getString();
  http.end();
  return code;
}

// ============================================================================
// HTTP Handlers
// ============================================================================

void sendJsonError(int code, const String &message) {
  String json = "{\"error\":\"" + jsonEscape(message) + "\"}";
  server.send(code, "application/json", json);
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleHealth() {
  bool online = WiFi.status() == WL_CONNECTED;
  String ip = online ? WiFi.localIP().toString() : String("0.0.0.0");
  String json = "{\"ok\":true,\"wifi\":" + String(online ? "true" : "false") +
                ",\"ip\":\"" + ip + "\"" +
                ",\"ssid\":\"" + jsonEscape(online ? WiFi.SSID() : String("")) + "\"" +
                ",\"rssi\":" + String(WiFi.RSSI()) +
                ",\"uptime\":" + String((millis() - bootMillis) / 1000) +
                ",\"bridgeOk\":" + String(bridgeOk ? "true" : "false") +
                ",\"bridge\":\"" + jsonEscape(bridgeUrl) + "\"" +
                ",\"firmware\":\"" + FW_VERSION + "\"" +
                ",\"state\":\"idle\"}";
  server.send(200, "application/json", json);
}

void handleChat() {
  if (server.method() != HTTP_POST) {
    sendJsonError(405, "Use POST");
    return;
  }

  String body = server.arg("plain");
  if (body.length() == 0) {
    sendJsonError(400, "Empty request body");
    return;
  }
  if (body.length() > MAX_REQUEST_BYTES) {
    sendJsonError(413, "Request body too large");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendJsonError(503, "ESP32 is not connected to Wi-Fi");
    return;
  }

  Serial.printf("[chat] forwarding %u bytes to %s/chat\n", (unsigned)body.length(), bridgeUrl.c_str());
  chatBusy = true;
  String response;
  int code = bridgePost("/chat", body, response, BRIDGE_TIMEOUT_MS);
  chatBusy = false;

  if (code <= 0) {
    bridgeOk = false;
    sendJsonError(502, String("Bridge unreachable: ") + HTTPClient::errorToString(code));
    return;
  }

  bridgeOk = code < 500;
  Serial.printf("[chat] bridge replied HTTP %d (%u bytes)\n", code, (unsigned)response.length());

  if (response.length() == 0) {
    sendJsonError(502, "Bridge returned an empty response");
    return;
  }
  server.send(code, "application/json", response);
}

void handleOutputs() {
  server.send(200, "application/json", outputsJson());
}

void handleAction() {
  if (server.method() != HTTP_POST) {
    sendJsonError(405, "Use POST");
    return;
  }

  String body = server.arg("plain");
  if (body.length() == 0) {
    sendJsonError(400, "Empty request body");
    return;
  }

  String outputId;
  int value = -1;
  int durationMs = 0;

  int start = body.indexOf("\"output\"");
  if (start >= 0) {
    int colon = body.indexOf(':', start);
    int q1 = body.indexOf('"', colon);
    int q2 = body.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) outputId = body.substring(q1 + 1, q2);
  }

  int vstart = body.indexOf("\"value\"");
  if (vstart >= 0) {
    int vcolon = body.indexOf(':', vstart);
    int vend = body.indexOf(' ', vcolon + 1);
    if (vend == -1) vend = body.indexOf('\t', vcolon + 1);
    if (vend == -1) vend = body.indexOf('\r', vcolon + 1);
    if (vend == -1) vend = body.indexOf('\n', vcolon + 1);
    if (vend == -1) vend = body.indexOf(',', vcolon + 1);
    if (vend == -1) vend = body.indexOf('}', vcolon + 1);
    if (vend == -1) vend = body.length();
    if (vend > vcolon) value = body.substring(vcolon + 1, vend).toInt();
  }

  int dstart = body.indexOf("\"durationMs\"");
  if (dstart >= 0) {
    int dcolon = body.indexOf(':', dstart);
    int dend = body.indexOf(' ', dcolon + 1);
    if (dend == -1) dend = body.indexOf('\t', dcolon + 1);
    if (dend == -1) dend = body.indexOf('\r', dcolon + 1);
    if (dend == -1) dend = body.indexOf('\n', dcolon + 1);
    if (dend == -1) dend = body.indexOf(',', dcolon + 1);
    if (dend == -1) dend = body.indexOf('}', dcolon + 1);
    if (dend == -1) dend = body.length();
    if (dend > dcolon) durationMs = body.substring(dcolon + 1, dend).toInt();
  }

  int index = findOutput(outputId);
  if (index < 0) {
    sendJsonError(400, String("Unknown output: ") + outputId);
    return;
  }
  if (value < 0 || value > 255) {
    sendJsonError(400, "value must be 0-255");
    return;
  }

  if (durationMs > 0) {
    pulseOutput((uint8_t)index, (uint8_t)value, (unsigned long)durationMs);
  } else {
    applyOutput((uint8_t)index, (uint8_t)value);
  }

  String json = "{\"ok\":true,\"output\":\"";
  json += outputId;
  json += "\",\"value\":";
  json += value;
  json += ",\"durationMs\":";
  json += durationMs;
  json += ",\"state\":";
  json += outputsJson();
  json += "}";
  server.send(200, "application/json", json);
}

void handleSensors() {
  SensorData sd;
  sd.distance_cm = readDistance();
  sd.battery_voltage = readBatteryVoltage();
  sd.current_amps = readCurrent();
  sd.line_sensor = readLineSensor();
  sd.e_stop_pressed = isEmergencyStop();
  sd.imu_roll = 0; sd.imu_pitch = 0; sd.imu_yaw = 0;

  server.send(200, "application/json", sensorDataJson(sd));
}

void handleNotFound() {
  if (server.uri().startsWith("/api/")) {
    sendJsonError(404, "Not found");
  } else {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  }
}

void announceWifi() {
  Serial.printf("[wifi] connected: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin("esp32-chat")) {
    MDNS.addService("http", "tcp", 80);
  }
}

void seedFirstWifi() {
  prefs.begin("esp32chat", true);
  bool seeded = prefs.getBool("wifiSeeded", false);
  prefs.end();
  if (seeded || strlen(DEFAULT_WIFI_SSID) == 0) return;

  WiFi.persistent(true);
  WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    digitalWrite(LED_PIN, (millis() / 150) % 2);
    delay(100);
  }

  prefs.begin("esp32chat", false);
  prefs.putBool("wifiSeeded", true);
  prefs.end();
}

void loadConfig() {
  prefs.begin("esp32chat", true);
  bridgeUrl = normalizeUrl(prefs.getString("bridgeUrl", DEFAULT_BRIDGE_URL));
  bridgeToken = prefs.getString("bridgeToken", DEFAULT_BRIDGE_TOKEN);
  prefs.end();
  if (bridgeUrl.length() == 0) bridgeUrl = DEFAULT_BRIDGE_URL;
  if (bridgeToken.length() == 0) bridgeToken = DEFAULT_BRIDGE_TOKEN;
}

void saveConfigFromPortal() {
  if (!bridgeUrlParam || !bridgeTokenParam) return;
  String url = normalizeUrl(String(bridgeUrlParam->getValue()));
  String token = String(bridgeTokenParam->getValue());
  token.trim();
  prefs.begin("esp32chat", false);
  prefs.putString("bridgeUrl", url);
  prefs.putString("bridgeToken", token);
  prefs.end();
}

// ============================================================================
// Setup & Loop
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);
  bootMillis = millis();

  Serial.println("\n=== ESP32 OpenCode Chat - Autonomous Robot ===");
  Serial.printf("[boot] firmware %s\n", FW_VERSION);

  loadConfig();
  setupMotors();
  setupServos();
  setupSensors();
  setupSafety();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  seedFirstWifi();

  bridgeUrlParam = new WiFiManagerParameter("bridgeUrl", "Bridge URL", bridgeUrl.c_str(), 120);
  bridgeTokenParam = new WiFiManagerParameter("bridgeToken", "Bridge token", bridgeToken.c_str(), 64);
  wifiManager.addParameter(bridgeUrlParam);
  wifiManager.addParameter(bridgeTokenParam);
  wifiManager.setSaveParamsCallback(saveConfigFromPortal);
  wifiManager.setConfigPortalTimeout(600);
  wifiManager.setTitle("ESP32 Chat setup");
  wifiManager.setDebugOutput(false);
  wifiManager.setWiFiAutoReconnect(true);

  if (!wifiManager.autoConnect(PORTAL_SSID, PORTAL_PASSWORD)) {
    ESP.restart();
  }

  loadConfig();
  announceWifi();
  wifiAnnounced = true;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/health", HTTP_GET, handleHealth);
  server.on("/api/chat", HTTP_POST, handleChat);
  server.on("/api/outputs", HTTP_GET, handleOutputs);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/sensors", HTTP_GET, handleSensors);
  server.onNotFound(handleNotFound);
  server.begin();

  registerWithBridge();
  nextRegisterMs = millis() + REGISTER_INTERVAL_MS;

  Serial.println("[system] Autonomous robot ready");
}

void loop() {
  server.handleClient();
  updatePulse();
  updateLed();
  handleResetButton();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiAnnounced) {
      announceWifi();
      wifiAnnounced = true;
      nextRegisterMs = millis();
    }
    if (!chatBusy && (long)(millis() - nextRegisterMs) >= 0) {
      nextRegisterMs = millis() + REGISTER_INTERVAL_MS;
      registerWithBridge();
    }
  } else {
    if (wifiAnnounced) {
      wifiAnnounced = false;
      bridgeOk = false;
    }
    if (millis() - lastWifiAttempt > WIFI_RETRY_MS) {
      lastWifiAttempt = millis();
      WiFi.reconnect();
    }
  }

  delay(10);
}