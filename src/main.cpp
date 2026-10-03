#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <stdarg.h>
#include <memory>
#include <WiFiClientSecureBearSSL.h>
#include "config.h"
#include "control.h"
#include "aht20.h"
#include "dashboard.h"
#include "mqtt_inbox.h"
#include "startup.h"
#include "weather_http.h"
#include "weather_request.h"
#include "weather_ca.h"
#include "oled_ui.h"

#ifndef CONTROLLER_SERIAL_DIAGNOSTICS
#define CONTROLLER_SERIAL_DIAGNOSTICS 0
#endif

namespace {
AsyncMqttClient mqtt;
ESP8266WebServer web(80);
Adafruit_SSD1306 oled(128, 32, &Wire, -1);
displayui::Navigation oledNavigation;
displayui::Timeout oledTimeout(cfg::oledTimeout);
int oledContrast = -1;
Aht20 sensor;
String deviceId, baseTopic, availabilityTopic;
uint16_t relays = 0;
uint32_t relayStarted[16] = {};
uint8_t brightness[4] = {}, savedBrightness[4] = {255, 255, 255, 255};
bool ledOn = false, oledOn = false, oledPresent = false;
bool configured = false, safetyLocked = false, updating = false;
startup::Services services = {};
bool networkServicesStarted = false;
uint8_t previousWifiStatus = 255;
char logs[24][128] = {};
unsigned logWrite = 0, logCount = 0;
uint32_t lastStatusLog = 0;
WiFiEventHandler wifiDisconnectHandler;
bool dirty = true, mqttWasConnected = false, haBirth = false;
uint32_t lastDisplay = 0, lastTelemetry = 0, lastWifi = 0;
uint32_t lastMqtt = 0, mqttAttempt = 0, offlineSince = 0, restartStarted = 0;
bool mqttConnecting = false, restartRequested = false;
uint32_t rejectedCommands = 0, sensorAvailabilityPublished = 2, weatherAvailabilityPublished = 2;
uint64_t uptimeMs = 0;
uint32_t previousMillis = 0;
int discoveryIndex = 0, stateIndex = 0;
bool announcing = false, ready = false;
uint16_t pendingPacket = 0;
uint32_t pendingSince = 0, lastPublish = 0;
constexpr int entityCount = 39;

void logEvent(const char* format, ...) {
  char text[100]; va_list args; va_start(args, format);
  vsnprintf(text, sizeof(text), format, args); va_end(args);
  snprintf(logs[logWrite], sizeof(logs[logWrite]), "[%lu] %s", static_cast<unsigned long>(millis()), text);
#if CONTROLLER_SERIAL_DIAGNOSTICS
  Serial.println(logs[logWrite]);
#endif
  logWrite = (logWrite + 1) % 24; if (logCount < 24) ++logCount;
}
bool pwmAvailable(unsigned channel) {
  return !CONTROLLER_SERIAL_DIAGNOSTICS || (cfg::pwmPins[channel] != 1 && cfg::pwmPins[channel] != 3);
}

MqttInbox inbox;

struct Weather {
  float temperature = NAN, humidity = NAN, cloud = NAN, precipitation = NAN;
  int code = -1, day = -1;
  char condition[40] = "unknown";
  uint32_t received = 0;
  bool valid = false;
} weather;
bool weatherFromMqtt = false;
char weatherSource[24] = "unavailable";
float weatherLatitude = cfg::weatherLatitude, weatherLongitude = cfg::weatherLongitude;
char region[64], location[64], timezoneName[64], clockTimezone[64];
bool locationFromHa = false, haWeatherChecked = false;
WeatherRequest weatherRefresh;
uint32_t weatherAttempt = 0, weatherRequestStarted = 0;
bool weatherAttempted = false;
char weatherError[96] = "Use Refresh weather or wait for MQTT feed";
std::unique_ptr<BearSSL::WiFiClientSecure> weatherClient;
std::unique_ptr<BearSSL::X509List> weatherTrust;
WeatherHttp weatherResponse;

String topic(const char* suffix) { return baseTopic + '/' + suffix; }
bool weatherHealthy(uint32_t now) {
  return weather.valid && !control::elapsed(now, weather.received, cfg::weatherExpiry);
}
solarphase::State solarState(time_t current) {
  static time_t minute = -1;
  static float latitude = NAN, longitude = NAN;
  static solarphase::State cached;
  if (minute != current/60 || latitude != weatherLatitude || longitude != weatherLongitude) {
    minute = current/60; latitude = weatherLatitude; longitude = weatherLongitude;
    cached = solarphase::calculate(current, latitude, longitude);
  }
  return cached;
}
void latchRelays() {
  const uint16_t bits = control::physicalBits(relays);
  SPI.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  digitalWrite(cfg::latch, LOW);
  // Far register (channels 9..16) first; nearest register (1..8) last.
  SPI.transfer(uint8_t(bits >> 8)); SPI.transfer(uint8_t(bits));
  digitalWrite(cfg::latch, HIGH);
  SPI.endTransaction();
}
void allOff(bool disable = false) {
  if (disable) digitalWrite(cfg::oe, HIGH);
  relays = 0; latchRelays();
  for (unsigned i = 0; i < 4; ++i) { brightness[i] = 0; if (pwmAvailable(i)) analogWrite(cfg::pwmPins[i], 0); }
  dirty = true;
}
void setRelay(unsigned channel, bool on) {
  const uint16_t before = relays;
  relays = control::setRelay(relays, channel, on, cfg::interlocks,
                            sizeof(cfg::interlocks) / sizeof(cfg::interlocks[0]));
  if ((relays & (1u << channel)) && !(before & (1u << channel))) relayStarted[channel] = millis();
  if (relays != before) { latchRelays(); dirty = true; }
}
void setOled(bool on) {
  if (on && !oledOn) oledNavigation.wake(millis());
  oledOn = on && oledPresent;
  if (oledPresent) oled.ssd1306_command(oledOn ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
  if (oledOn) oledTimeout.wake(millis());
  dirty = true;
  logEvent("OLED present=%d state=%s", oledPresent, oledOn ? "ON" : "OFF");
}
void restart() {
  allOff(true); safetyLocked = true;
  restartStarted = millis(); restartRequested = true;
  if (mqtt.connected()) mqtt.publish(topic("availability").c_str(), 1, true, "offline");
}
bool parseWeather(const char* payload, bool* unavailable = nullptr) {
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, payload)) return false;
  if (!doc["sample_time"].isNull()) {
    if (!doc["sample_time"].is<long>()) return false;
    const time_t sample = doc["sample_time"].as<long>(), now = time(nullptr);
    if (now > 1700000000 && (sample > now + 300 || now - sample >= long(cfg::weatherExpiry / 1000))) return false;
  }
  if (doc["available"].is<bool>() && !doc["available"].as<bool>()) {
    if (unavailable) *unavailable = true;
    // An unavailable HA provider must not erase a working direct fallback.
    if (weatherFromMqtt) { weather.valid = false; weatherAttempted = false; dirty = true; }
    return true;
  }
  const char* condition = doc["condition"] | "";
  if (!*condition || strlen(condition) >= sizeof(weather.condition) ||
      !strcmp(condition, "unknown") || !strcmp(condition, "unavailable")) return false;
  const char* fields[] = {"temperature", "humidity", "cloud_cover", "precipitation"};
  float values[4];
  for (unsigned i = 0; i < 4; ++i) {
    if (i >= 2 && doc[fields[i]].isNull()) { values[i] = NAN; continue; }
    if (!doc[fields[i]].is<float>()) return false;
    values[i] = doc[fields[i]].as<float>();
    if (!isfinite(values[i])) return false;
  }
  if (values[0] < -90 || values[0] > 70 || values[1] < 0 || values[1] > 100 ||
      values[2] < 0 || values[2] > 100 || values[3] < 0 || values[3] > 1000 ||
      !doc["code"].is<int>() || !doc["is_day"].is<int>()) return false;
  int code = doc["code"], day = doc["is_day"];
  if (code < 0 || code > 99 || day < 0 || day > 1) return false;
  weather.temperature = values[0]; weather.humidity = values[1];
  weather.cloud = values[2]; weather.precipitation = values[3];
  weather.code = code; weather.day = day;
  strlcpy(weather.condition, condition, sizeof(weather.condition));
  const char* source = doc["source"] | "home-assistant";
  strlcpy(weatherSource, !strcmp(source, "open-meteo") ? "open-meteo-ha" : "home-assistant", sizeof(weatherSource));
  weather.received = millis();
  const time_t now = time(nullptr);
  if (now > 1700000000 && doc["sample_time"].is<long>()) {
    const time_t sample = doc["sample_time"].as<long>();
    if (sample < now) weather.received -= uint32_t(now - sample) * 1000;
  }
  weather.valid = true; dirty = true; return true;
}
void closeWeather();
bool command(const char* suffix, const char* payload) {
  if (!strcmp(suffix, "oled_timeout/set")) {
    if (updating || restartRequested || !oledTimeout.select(payload, millis())) return false;
    dirty = true;
    logEvent("OLED timeout=%u minutes (0=OFF)", oledTimeout.minutes());
    return true;
  }
  if (!strcmp(suffix, "oled_page/set")) {
    if (updating || restartRequested || !oledNavigation.select(payload, millis())) return false;
    setOled(true); lastDisplay = millis() - 1000; return true;
  }
  if (!strcmp(suffix, "weather_refresh/set")) {
    if (strcmp(payload, "PRESS") || updating || restartRequested) return false;
    if (!weatherRefresh.pending()) {
      weatherRefresh.request(); haWeatherChecked = false;
      strlcpy(weatherError, "Weather refresh queued", sizeof(weatherError));
      logEvent("Weather refresh requested"); dirty = true;
    }
    return true;
  }
  if (!strcmp(suffix, "location/set")) {
    if (!cfg::haLocationOverride) return true;
    StaticJsonDocument<768> doc;
    if (deserializeJson(doc, payload) || !doc["latitude"].is<float>() || !doc["longitude"].is<float>() || !doc["utc_offset_minutes"].is<int>()) return false;
    const float lat = doc["latitude"], lon = doc["longitude"];
    const int offset = doc["utc_offset_minutes"];
    if (!isfinite(lat) || !isfinite(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180 || offset < -720 || offset > 840) return false;
    const char* fields[] = {"region", "location", "timezone"};
    for (auto field : fields) {
      if (!doc[field].is<const char*>() || !*doc[field].as<const char*>() || strlen(doc[field]) >= 64) return false;
    }
    const bool moved = weatherLatitude != lat || weatherLongitude != lon;
    weatherLatitude = lat; weatherLongitude = lon;
    strlcpy(region, doc["region"], sizeof(region)); strlcpy(location, doc["location"], sizeof(location));
    strlcpy(timezoneName, doc["timezone"], sizeof(timezoneName));
    char tz[64]; snprintf(tz, sizeof(tz), "HA%s%d:%02d", offset >= 0 ? "-" : "+", abs(offset) / 60, abs(offset) % 60);
    if (strcmp(clockTimezone, tz)) {
      strlcpy(clockTimezone, tz, sizeof(clockTimezone));
      configTime(clockTimezone, "pool.ntp.org", "time.cloudflare.com");
    }
    if (moved) {
      if (weatherClient) weatherClient->stop();
      weatherClient.reset(); weatherTrust.reset();
      weather.valid = false; weatherAttempted = false;
    }
    locationFromHa = true; dirty = true; return true;
  }
  if (!strcmp(suffix, "weather/set")) {
    bool unavailable = false;
    if (!parseWeather(payload, &unavailable)) return false;
    haWeatherChecked = true;
    if (unavailable) return true;
    closeWeather();
    weatherRefresh.finish();
    weatherFromMqtt = true; weatherError[0] = 0;
    logEvent("Weather received from MQTT feed"); return true;
  }
  if (!strcmp(suffix, "all_off/set") && !strcmp(payload, "PRESS")) { allOff(); return true; }
  if (!strcmp(suffix, "restart/set") && !strcmp(payload, "PRESS")) { restart(); return true; }
  if (safetyLocked || updating || restartRequested) return false;
  bool on = false;
  if (!strcmp(suffix, "oled/set") && control::switchCommand(payload, on)) { setOled(on); return true; }
  if (!strcmp(suffix, "led/set") && control::switchCommand(payload, on)) {
    ledOn = on; digitalWrite(cfg::led, !on); dirty = true; return true;
  }
  char number[4]; unsigned channel;
  const size_t size = strlen(suffix);
  if (!strncmp(suffix, "relay/", 6) && size >= 11 && size <= 12 && !strcmp(suffix + size - 4, "/set")) {
    const size_t n = size - 10;
    memcpy(number, suffix + 6, n); number[n] = 0;
    if (!control::channel(number, 16, channel) || !control::switchCommand(payload, on)) return false;
    setRelay(channel, on); return true;
  }
  if (!strncmp(suffix, "pwm/", 4) && size == 9 && !strcmp(suffix + 5, "/set")) {
    number[0] = suffix[4]; number[1] = 0;
    if (!control::channel(number, 4, channel) || !pwmAvailable(channel)) return false;
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, payload) || !doc.is<JsonObject>()) return false;
    bool hasState = doc.containsKey("state"), hasBrightness = doc.containsKey("brightness");
    if (!hasState && !hasBrightness) return false;
    if (hasState && (!doc["state"].is<const char*>() || !control::switchCommand(doc["state"], on))) return false;
    int value = savedBrightness[channel];
    if (hasBrightness) {
      if (!doc["brightness"].is<int>()) return false;
      value = doc["brightness"].as<int>();
      if (value < 0 || value > 255) return false;
    }
    if (!hasState) on = value > 0;
    if (hasBrightness && value > 0) savedBrightness[channel] = value;
    brightness[channel] = on ? value : 0;
    analogWrite(cfg::pwmPins[channel], brightness[channel]); dirty = true; return true;
  }
  return false;
}

void stateJson(JsonDocument& doc) {
  char key[5];
  for (unsigned i = 0; i < 16; ++i) {
    snprintf(key, sizeof(key), "r%02u", i + 1); doc[key] = (relays & (1u << i)) ? "ON" : "OFF";
  }
  doc["relay_mask"] = relays;
  doc["oled"] = oledOn ? "ON" : "OFF"; doc["led"] = ledOn ? "ON" : "OFF";
  doc["oled_present"] = oledPresent;
  doc["oled_mode"] = oledNavigation.mode();
  doc["oled_page"] = displayui::pageName(oledNavigation.page(millis()));
  doc["oled_timeout_minutes"] = oledTimeout.minutes();
  doc["oled_remaining_s"] = oledOn ? oledTimeout.remaining(millis()) : 0;
  if (sensor.healthy) { doc["temperature"] = sensor.temperature; doc["humidity"] = sensor.humidity; }
  else { doc["temperature"] = nullptr; doc["humidity"] = nullptr; }
  doc["sensor_healthy"] = sensor.healthy ? "ON" : "OFF";
  doc["weather_available"] = weatherHealthy(millis()) ? "ON" : "OFF";
  doc["weather_source"] = weatherHealthy(millis()) ? weatherSource : "unavailable";
  doc["region"] = region; doc["location"] = location; doc["timezone"] = timezoneName;
  doc["latitude"] = weatherLatitude; doc["longitude"] = weatherLongitude;
  doc["location_source"] = locationFromHa ? "home-assistant" : "configuration";
  char localTime[32] = "";
  const time_t clockNow = time(nullptr); struct tm local;
  if (clockNow > 1700000000) { localtime_r(&clockNow, &local); strftime(localTime, sizeof(localTime), "%Y-%m-%d %H:%M:%S", &local); }
  doc["local_time"] = localTime;
  doc["weather_error"] = weatherError;
  doc["weather_refresh_pending"] = weatherRefresh.pending();
  doc["weather_age_s"] = weather.valid ? uint32_t(millis() - weather.received) / 1000 : 0;
  doc["time_valid"] = time(nullptr) > 1700000000;
  doc["solar_phase"] = solarphase::name(solarState(time(nullptr)).phase);
  if (weatherHealthy(millis())) {
    doc["weather_temperature"] = weather.temperature; doc["weather_humidity"] = weather.humidity;
    if (isfinite(weather.cloud)) doc["weather_cloud_cover"] = weather.cloud;
    else doc["weather_cloud_cover"] = nullptr;
    if (isfinite(weather.precipitation)) doc["weather_precipitation"] = weather.precipitation;
    else doc["weather_precipitation"] = nullptr;
    doc["weather_code"] = weather.code; doc["weather_is_day"] = weather.day ? "ON" : "OFF";
    doc["weather_condition"] = weather.condition;
  }
  doc["uptime"] = uptimeMs / 1000; doc["rssi"] = WiFi.RSSI(); doc["heap"] = ESP.getFreeHeap();
  doc["reset"] = ESP.getResetReason(); doc["firmware"] = cfg::version;
  doc["locked"] = safetyLocked; doc["rejected_commands"] = rejectedCommands;
  doc["sensor_failures"] = sensor.failures;
  doc["ip"] = WiFi.localIP().toString();
  doc["wifi_status"] = int(WiFi.status()); doc["mqtt_connected"] = mqtt.connected();
  doc["mqtt_configured"] = services.mqtt; doc["web_enabled"] = services.web;
  doc["ota_enabled"] = services.ota; doc["serial_diagnostics"] = bool(CONTROLLER_SERIAL_DIAGNOSTICS);
  doc["discovery_index"] = discoveryIndex; doc["mqtt_ready"] = ready;
  JsonArray pwm = doc.createNestedArray("pwm"); for (auto b : brightness) pwm.add(b);
}

// One QoS1 packet in flight keeps discovery bounded on an ESP8266.
bool publish(const String& target, const char* payload) {
  pendingPacket = mqtt.publish(target.c_str(), 1, true, payload);
  pendingSince = lastPublish = millis();
  return pendingPacket != 0;
}
bool publishJson(const String& target, JsonDocument& doc) {
  // ESP8266 has a small continuation stack. Keep the buffer out of the stack
  // used by JSON construction, Wi-Fi callbacks and the web server.
  static char buffer[2048]; const size_t length = measureJson(doc);
  if (doc.overflowed() || length >= sizeof(buffer)) {
    logEvent("JSON publish too large (%u bytes, overflow=%d)", unsigned(length), doc.overflowed());
    return false;
  }
  serializeJson(doc, buffer, sizeof(buffer)); return publish(target, buffer);
}

bool discovery(int index) {
  DynamicJsonDocument doc(1536);
  String component = "sensor", id, name, key;
  const char* unit = nullptr; const char* deviceClass = nullptr;
  bool measured = false, sensorAvailability = false, weatherAvailability = false;
  char numbered[32];
  if (index < 16) {
    snprintf(numbered, sizeof(numbered), "relay_%02d", index + 1); id = numbered;
    snprintf(numbered, sizeof(numbered), "Relay %02d", index + 1); name = numbered;
    snprintf(numbered, sizeof(numbered), "r%02d", index + 1); key = numbered;
    snprintf(numbered, sizeof(numbered), "relay/%02d/set", index + 1);
    component = "switch"; doc["command_topic"] = topic(numbered);
  } else if (index < 20) {
    id = "pwm_" + String(index - 15); name = "PWM GPIO " + String(index - 15);
    component = "light"; doc["schema"] = "json"; doc["brightness"] = true;
    doc["brightness_scale"] = 255;
    doc["command_topic"] = topic(("pwm/" + String(index - 15) + "/set").c_str());
    doc["state_topic"] = topic(("pwm/" + String(index - 15) + "/state").c_str());
  } else {
    switch (index) {
      case 20: id = key = "oled"; name = "OLED Screen"; component = "switch"; break;
      case 21: id = key = "led"; name = "Onboard LED GPIO16"; component = "switch"; break;
      case 22: id = "all_off"; name = "All outputs off"; component = "button"; break;
      case 23: id = "restart"; name = "Restart"; component = "button"; doc["device_class"] = "restart"; break;
      case 24: id = key = "temperature"; name = "Living Room Temperature"; unit = "°C"; deviceClass = "temperature"; measured = sensorAvailability = true; break;
      case 25: id = key = "humidity"; name = "Living Room Humidity"; unit = "%"; deviceClass = "humidity"; measured = sensorAvailability = true; break;
      case 26: id = key = "weather_temperature"; name = "Online Weather Temperature"; unit = "°C"; deviceClass = "temperature"; measured = weatherAvailability = true; break;
      case 27: id = key = "weather_humidity"; name = "Online Weather Humidity"; unit = "%"; deviceClass = "humidity"; measured = weatherAvailability = true; break;
      case 28: id = key = "weather_cloud_cover"; name = "Online Weather Cloud Cover"; unit = "%"; measured = weatherAvailability = true; break;
      case 29: id = key = "weather_precipitation"; name = "Online Weather Precipitation"; unit = "mm"; measured = weatherAvailability = true; break;
      case 30: id = key = "weather_code"; name = "Online Weather Code"; weatherAvailability = true; break;
      case 31: id = key = "weather_is_day"; name = "Online Weather Is Day"; component = "binary_sensor"; weatherAvailability = true; break;
      case 32: id = key = "weather_condition"; name = "Online Weather Condition"; weatherAvailability = true; break;
      case 33: id = key = "uptime"; name = "Uptime"; unit = "s"; deviceClass = "duration"; break;
      case 34: id = key = "rssi"; name = "WiFi RSSI"; unit = "dBm"; deviceClass = "signal_strength"; measured = true; break;
      case 35: id = key = "heap"; name = "Free heap"; unit = "B"; measured = true; break;
      case 36: id = key = "sensor_healthy"; name = "AHT20 healthy"; component = "binary_sensor"; break;
      case 37: id = key = "weather_available"; name = "Weather available"; component = "binary_sensor"; break;
      case 38: id = key = "reset"; name = "Reset reason"; break;
      default: return false;
    }
    if (component == "switch" || component == "button") doc["command_topic"] = topic((id + "/set").c_str());
    if (index >= 33) doc["entity_category"] = "diagnostic";
  }
  doc["name"] = name; doc["unique_id"] = deviceId + '_' + id;
  if (component != "button" && component != "light") {
    doc["state_topic"] = topic("state");
    doc["value_template"] = "{{ value_json." + key + " }}";
  }
  if (component == "switch" || component == "binary_sensor") {
    doc["payload_on"] = "ON"; doc["payload_off"] = "OFF";
  }
  if (component == "button") doc["payload_press"] = "PRESS";
  // Commands must not be replayed after a reconnect.
  if (component == "switch" || component == "light" || component == "button") {
    doc["retain"] = false; doc["qos"] = 1;
  }
  if (unit) doc["unit_of_measurement"] = unit;
  if (deviceClass) doc["device_class"] = deviceClass;
  if (measured) doc["state_class"] = "measurement";
  if (index == 24 || index == 26 || index == 29) doc["suggested_display_precision"] = 1;
  if (index == 25 || index == 27 || index == 28) doc["suggested_display_precision"] = 0;
  if (sensorAvailability || weatherAvailability) {
    JsonArray availability = doc.createNestedArray("availability");
    availability.createNestedObject()["topic"] = topic("availability");
    availability.createNestedObject()["topic"] = topic(sensorAvailability ? "sensor_availability" : "weather_availability");
    doc["availability_mode"] = "all";
  } else doc["availability_topic"] = topic("availability");
  JsonObject device = doc.createNestedObject("device");
  device["identifiers"] = deviceId; device["name"] = cfg::name;
  device["manufacturer"] = "Custom"; device["model"] = "ESP8266 16CH 74HC595";
  device["sw_version"] = cfg::version;
  device["configuration_url"] = "http://" + WiFi.localIP().toString();
  return publishJson("homeassistant/" + component + '/' + deviceId + '/' + id + "/config", doc);
}

void mqttMessage(char* target, char* payload, AsyncMqttClientMessageProperties properties,
                 size_t len, size_t index, size_t total) {
  if (!inbox.receive(target, payload, properties.retain, len, index, total)) ++rejectedCommands;
}
void processInbox() {
  MqttMessage message;
  while (inbox.pop(message)) {
    if (!strcmp(message.topic, "homeassistant/status")) {
      if (!strcmp(message.payload, "online")) haBirth = true;
      continue;
    }
    const String prefix = baseTopic + '/';
    if (strncmp(message.topic, prefix.c_str(), prefix.length()) || !command(message.topic + prefix.length(), message.payload)) ++rejectedCommands;
  }
}
void mqttTick(uint32_t now) {
  if (!services.mqtt || WiFi.status() != WL_CONNECTED || updating || restartRequested) return;
  if (!mqtt.connected()) {
    if (mqttWasConnected) {
      mqttWasConnected = ready = false; offlineSince = now;
      pendingPacket = 0;
    }
    if (mqttConnecting && control::elapsed(now, mqttAttempt, 10000)) { mqtt.disconnect(true); mqttConnecting = false; }
    if (!mqttConnecting && control::elapsed(now, lastMqtt, 5000)) {
      lastMqtt = mqttAttempt = now; mqttConnecting = true;
      logEvent("MQTT connecting to configured broker port %u", unsigned(MQTT_PORT)); mqtt.connect();
    }
    return;
  }
  if (!mqttWasConnected) {
    logEvent("MQTT connected; publishing Home Assistant discovery");
    mqttWasConnected = true; mqttConnecting = false;
    mqtt.subscribe(topic("+/set").c_str(), 1);
    mqtt.subscribe(topic("relay/+/set").c_str(), 1);
    mqtt.subscribe(topic("pwm/+/set").c_str(), 1);
    mqtt.subscribe("homeassistant/status", 1);
    discoveryIndex = stateIndex = 0; announcing = true; ready = false;
    sensorAvailabilityPublished = weatherAvailabilityPublished = 2;
    dirty = true;
  }
  if (haBirth) { discoveryIndex = 0; announcing = true; haBirth = false; dirty = true; }
  if (pendingPacket) {
    if (control::elapsed(now, pendingSince, 10000)) { mqtt.disconnect(true); pendingPacket = 0; }
    return;
  }
  if (!control::elapsed(now, lastPublish, 50)) return;
  if (announcing) {
    if (discoveryIndex < entityCount) { if (discovery(discoveryIndex)) ++discoveryIndex; return; }
    announcing = false;
  }
  if (sensorAvailabilityPublished != unsigned(sensor.healthy)) {
    if (publish(topic("sensor_availability"), sensor.healthy ? "online" : "offline")) sensorAvailabilityPublished = sensor.healthy;
    return;
  }
  const bool weatherOk = weatherHealthy(now);
  if (weatherAvailabilityPublished != unsigned(weatherOk)) {
    if (publish(topic("weather_availability"), weatherOk ? "online" : "offline")) weatherAvailabilityPublished = weatherOk;
    return;
  }
  if (dirty && stateIndex == 0) { dirty = false; stateIndex = 1; }
  if (stateIndex) {
    DynamicJsonDocument doc(3072);
    String target = topic("state");
    if (stateIndex == 1) stateJson(doc);
    else {
      const unsigned i = stateIndex - 2;
      doc["state"] = brightness[i] ? "ON" : "OFF";
      doc["brightness"] = brightness[i];
      target = topic(("pwm/" + String(i + 1) + "/state").c_str());
    }
    if (publishJson(target, doc)) { ++stateIndex; if (stateIndex > 5) stateIndex = 0; }
    return;
  }
  if (!ready && publish(topic("availability"), "online")) {
    ready = true; logEvent("MQTT ready: %d discovery entities and states published", entityCount);
  }
}

bool authorized() {
  if (web.authenticate(WEB_USER, WEB_PASSWORD)) return true;
  web.requestAuthentication(DIGEST_AUTH, "Controller"); return false;
}
void webSetup() {
  web.collectHeaders("X-Controller-Request");
  web.on("/", HTTP_GET, [] {
    if (!authorized()) return;
    web.sendHeader("Cache-Control", "no-store");
    web.sendHeader("X-Frame-Options", "DENY");
    web.send_P(200, "text/html", dashboard);
  });
  web.on("/api/state", HTTP_GET, [] {
    if (!authorized()) return;
    DynamicJsonDocument doc(3072); stateJson(doc);
    String result; serializeJson(doc, result);
    web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/json", result);
  });
  web.on("/api/logs", HTTP_GET, [] {
    if (!authorized()) return;
    String result;
    for (unsigned i = 0; i < logCount; ++i) {
      result += logs[(logWrite + 24 - logCount + i) % 24]; result += '\n';
    }
    web.sendHeader("Cache-Control", "no-store"); web.send(200, "text/plain", result);
  });
  web.on("/api/oled", HTTP_GET, [] {
    if (!authorized()) return;
    if (!oledPresent) { web.send(503, "text/plain", "OLED not detected"); return; }
    // PBM snapshot of the framebuffer already sent to the physical panel.
    // Stream 16 bytes per row rather than allocating another image in RAM.
    constexpr char header[] = "P4\n128 32\n";
    web.sendHeader("Cache-Control", "no-store");
    web.setContentLength(sizeof(header) - 1 + 512);
    web.send(200, "image/x-portable-bitmap", "");
    web.sendContent(header, sizeof(header) - 1);
    const uint8_t* pixels = oled.getBuffer();
    for (unsigned y = 0; y < 32; ++y) {
      uint8_t row[16] = {};
      for (unsigned x = 0; x < 128; ++x)
        if (!(pixels[x + (y/8)*128] & (1u << (y%8)))) row[x/8] |= 0x80u >> (x%8);
      web.sendContent(reinterpret_cast<const char*>(row), sizeof(row));
    }
  });
  web.on("/api/command", HTTP_POST, [] {
    if (!authorized()) return;
    // Custom header blocks browser cross-origin form attacks; no CORS is enabled.
    if (web.header("X-Controller-Request") != "1") { web.send(403); return; }
    const String body = web.arg("plain");
    if (body.length() > 256) { web.send(413); return; }
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, body) || !doc["topic"].is<const char*>() || !doc["payload"].is<const char*>()) {
      web.send(400, "application/json", "{\"error\":\"Invalid command\"}"); return;
    }
    if (!command(doc["topic"], doc["payload"])) {
      ++rejectedCommands; web.send(400, "application/json", "{\"error\":\"Rejected command or outputs locked\"}"); return;
    }
    web.send(200, "application/json", "{\"ok\":true}");
  });
  web.onNotFound([] { web.send(404); }); web.begin();
}
void displayTick(uint32_t now) {
  // Web/MQTT commands can restart the timer after loop() sampled its timestamp.
  // Read fresh time so that an earlier timestamp cannot look like a rollover.
  now = millis();
  if (oledOn && oledTimeout.expired(now)) setOled(false);
  if (!oledPresent || !oledOn || !control::elapsed(now, lastDisplay, 1000)) return;
  lastDisplay = now;
  const time_t current = time(nullptr);
  struct tm local = {}; localtime_r(&current, &local);
  char clockText[9] = "", dateText[16] = "", ip[16];
  strftime(clockText, sizeof(clockText), "%H:%M:%S", &local);
  strftime(dateText, sizeof(dateText), "%a %d %b", &local);
  const bool night = current > 1700000000 && (local.tm_hour >= 22 || local.tm_hour < 7);
  const int contrast = night ? 0x18 : 0x8f;
  if (contrast != oledContrast) {
    oled.ssd1306_command(SSD1306_SETCONTRAST); oled.ssd1306_command(contrast); oledContrast = contrast;
  }
  strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
  displayui::Snapshot s;
  s.wifi = WiFi.isConnected(); s.mqtt = mqtt.connected(); s.rssi = WiFi.RSSI();
  s.sensor = sensor.healthy; s.temperature = sensor.temperature; s.humidity = sensor.humidity;
  s.weather = weatherHealthy(now); s.weatherStale = weather.valid && !s.weather;
  s.outside = weather.temperature; s.outsideHumidity = weather.humidity;
  s.cloud = weather.cloud; s.rain = weather.precipitation; s.condition = weather.condition;
  const auto solar = solarState(current);
  s.phase = solar.phase;
  s.day = solar.phase != solarphase::Phase::Unknown ? solar.day : weather.day == 1;
  s.age = uint32_t(now - weather.received) / 1000;
  s.source = !strcmp(weatherSource, "home-assistant") ? "HA" : !strcmp(weatherSource, "open-meteo-ha") ? "HA/OM" : "OM";
  s.location = location; s.timezone = timezoneName; s.refreshing = weatherRefresh.pending();
  s.clock = current > 1700000000; s.time = clockText; s.date = dateText; s.ip = ip;
  s.relays = relays; memcpy(s.pwm, brightness, sizeof(s.pwm));
  s.uptime = uptimeMs / 1000; s.heap = ESP.getFreeHeap();
  s.locked = safetyLocked; s.updating = updating; s.configured = configured;
  oled.clearDisplay();
  displayui::render(oled, oledNavigation.page(now), s, strcmp(oledNavigation.mode(), "AUTO") != 0);
  oled.display();
}
void networkTick(uint32_t now) {
  const uint8_t status = WiFi.status();
  if (status != previousWifiStatus) {
    previousWifiStatus = status; logEvent("WiFi status=%u", unsigned(status));
    if (status == WL_CONNECTED) {
      logEvent("WiFi connected IP=%s RSSI=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }
  }
  if (status == WL_CONNECTED && !networkServicesStarted) {
    networkServicesStarted = true;
    if (services.web) { webSetup(); logEvent("Authenticated web dashboard and /api/logs started"); }
    if (services.ota) { ArduinoOTA.begin(); logEvent("Password-protected OTA started"); }
    configTime(clockTimezone, "pool.ntp.org", "time.cloudflare.com");
  }
  if (control::elapsed(now, lastStatusLog, 15000)) {
    lastStatusLog = now;
    logEvent("Health wifi=%u mqtt=%d sensor=%d heap=%u locked=%d", unsigned(status), mqtt.connected(), sensor.healthy, ESP.getFreeHeap(), safetyLocked);
  }
}

void closeWeather() {
  if (weatherClient) weatherClient->stop();
  weatherClient.reset(); weatherTrust.reset();
}
void weatherFailure(const char* error) {
  closeWeather(); weatherRefresh.finish();
  strlcpy(weatherError, error, sizeof(weatherError));
  logEvent("Weather fallback: %s", weatherError); dirty = true;
}
const char* weatherCondition(int code, int day) {
  if (code == 0) return day ? "sunny" : "clear-night";
  if (code <= 2) return "partlycloudy";
  if (code == 3) return "cloudy";
  if (code == 45 || code == 48) return "fog";
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return "rainy";
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return "snowy";
  if (code >= 95) return "lightning-rainy";
  return "exceptional";
}
void applyOnlineWeather() {
  DynamicJsonDocument doc(1536);
  if (deserializeJson(doc, weatherResponse.body())) { weatherFailure("Invalid Open-Meteo JSON"); return; }
  const JsonObject current = doc["current"].as<JsonObject>();
  if (current.isNull() || !current["weather_code"].is<int>() || !current["is_day"].is<int>() || !current["time"].is<long>()) {
    weatherFailure("Incomplete Open-Meteo response"); return;
  }
  const time_t sampleTime = current["time"].as<long>(), now = time(nullptr);
  if (sampleTime > now + 300 || now - sampleTime > 1800) { weatherFailure("Stale Open-Meteo response"); return; }
  StaticJsonDocument<512> normalized;
  normalized["sample_time"] = sampleTime;
  normalized["temperature"] = current["temperature_2m"];
  normalized["humidity"] = current["relative_humidity_2m"];
  normalized["cloud_cover"] = current["cloud_cover"];
  normalized["precipitation"] = current["precipitation"];
  normalized["code"] = current["weather_code"];
  normalized["is_day"] = current["is_day"];
  normalized["condition"] = weatherCondition(current["weather_code"], current["is_day"]);
  char payload[384]; serializeJson(normalized, payload, sizeof(payload));
  if (!parseWeather(payload)) { weatherFailure("Invalid Open-Meteo values"); return; }
  weatherFromMqtt = false; weatherError[0] = 0;
  strlcpy(weatherSource, "open-meteo", sizeof(weatherSource));
  logEvent("Weather fallback updated: %.1fC %.0f%% %s", weather.temperature, weather.humidity, weather.condition);
}
void weatherTick(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED || updating || restartRequested) return;
  if (weatherClient) {
    // Reading/HTTP framing is incremental; no blocking response/body read.
    unsigned budget = 256;
    while (budget-- && weatherClient->available() && weatherResponse.status() == WeatherHttp::Status::Receiving)
      weatherResponse.feed(char(weatherClient->read()));
    if (!weatherClient->connected() && !weatherClient->available()) weatherResponse.closed();
    if (weatherResponse.status() == WeatherHttp::Status::Error) { weatherFailure(weatherResponse.error()); return; }
    if (weatherResponse.status() == WeatherHttp::Status::Complete) {
      closeWeather(); // Release TLS heap before parsing JSON/publishing state.
      weatherRefresh.finish();
      applyOnlineWeather(); return;
    }
    if (control::elapsed(now, weatherRequestStarted, cfg::weatherResponseTimeoutMs)) weatherFailure("Weather response timeout");
    return;
  }
  // No DNS, TLS, polling or retries until an explicit refresh is requested.
  if (!weatherRefresh.pending() || !networkServicesStarted) return;
  if (!weatherRefresh.notified()) {
    if (mqtt.connected() && !mqtt.publish(topic("weather/request").c_str(), 1, false, "PRESS")) return;
    weatherRefresh.notify(now);
  }
  if (mqtt.connected() && !haWeatherChecked && weatherRefresh.waiting(now, cfg::haWeatherWaitMs)) {
    strlcpy(weatherError, "Waiting for Home Assistant weather", sizeof(weatherError)); return;
  }
  if (!cfg::weatherFallback) { weatherFailure("No MQTT weather response; direct fallback disabled"); return; }
  if (time(nullptr) < 1700000000) {
    strlcpy(weatherError, "Waiting for NTP time to verify HTTPS", sizeof(weatherError)); return;
  }
  if (weatherAttempted && !control::elapsed(now, weatherAttempt, cfg::weatherRetryMs)) return;
  // Certificate handshake yields to networking but is synchronous. Defer it
  // while a timed relay is active so auto-off deadlines remain responsive.
  for (unsigned i = 0; i < 16; ++i) if (cfg::relayAutoOff[i] && (relays & (1u << i))) return;
  if (ESP.getFreeHeap() < 22000 || ESP.getMaxFreeBlockSize() < 9000) return;
  weatherAttempted = true; weatherAttempt = now;
  weatherResponse.reset();
  weatherTrust.reset(new BearSSL::X509List(weatherRootCa));
  weatherClient.reset(new BearSSL::WiFiClientSecure);
  weatherClient->setTrustAnchors(weatherTrust.get());
  // The small, bounded endpoint response and current certificate chain fit
  // this RX limit. Oversized TLS records fail closed, never bypass validation.
  weatherClient->setBufferSizes(8192, 512);
  weatherClient->setTimeout(2500);
  logEvent("Fetching Open-Meteo over verified HTTPS");
  if (!weatherClient->connect("api.open-meteo.com", 443)) {
    char reason[80]; weatherClient->getLastSSLError(reason, sizeof(reason)); weatherFailure(reason); return;
  }
  weatherClient->setTimeout(1500);
  char request[512];
  snprintf(request, sizeof(request),
    "GET /v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,precipitation,weather_code,cloud_cover,is_day&timezone=UTC&timeformat=unixtime HTTP/1.0\r\nHost: api.open-meteo.com\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",
    weatherLatitude, weatherLongitude);
  if (weatherClient->write(reinterpret_cast<const uint8_t*>(request), strlen(request)) != strlen(request)) {
    weatherFailure("Weather request write failed"); return;
  }
  weatherRequestStarted = millis();
}
} // namespace

void setup() {
  strlcpy(region, cfg::region, sizeof(region)); strlcpy(location, cfg::location, sizeof(location));
  strlcpy(timezoneName, cfg::timezoneName, sizeof(timezoneName));
  strlcpy(clockTimezone, cfg::timezone, sizeof(clockTimezone));
  // GPIO2/OE is active-low. An external pull-up is required before firmware runs.
  digitalWrite(cfg::oe, HIGH); pinMode(cfg::oe, OUTPUT);
  digitalWrite(cfg::latch, HIGH); pinMode(cfg::latch, OUTPUT);
  SPI.begin(); allOff(true);
#if CONTROLLER_SERIAL_DIAGNOSTICS
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  Serial.setDebugOutput(false);
  pinMode(3, INPUT); // USB adapter TX drives this pin; do not contend with it.
#endif
  analogWriteRange(255); analogWriteFreq(cfg::pwmFrequency);
  for (unsigned i = 0; i < 4; ++i) {
    if (!pwmAvailable(i)) continue;
    const auto pin = cfg::pwmPins[i]; digitalWrite(pin, LOW); pinMode(pin, OUTPUT); analogWrite(pin, 0);
  }
  digitalWrite(cfg::led, HIGH); pinMode(cfg::led, OUTPUT);
  deviceId = "living_ctrl_" + String(ESP.getChipId(), HEX); baseTopic = "home/" + deviceId;
  availabilityTopic = topic("availability");
  logEvent("Boot firmware=%s device=%s reset=%s", cfg::version, deviceId.c_str(), ESP.getResetReason().c_str());
  logEvent("Serial diagnostics=%d (diagnostic mode reserves GPIO1/GPIO3)", CONTROLLER_SERIAL_DIAGNOSTICS);
  Wire.begin(cfg::sda, cfg::scl); Wire.setClock(400000); Wire.setClockStretchLimit(1500);
#if CONTROLLER_SERIAL_DIAGNOSTICS
  unsigned i2cCount = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) { ++i2cCount; logEvent("I2C device address=0x%02X", unsigned(address)); }
    yield();
  }
  logEvent("I2C scan: %u devices on SDA=%u SCL=%u", i2cCount, unsigned(cfg::sda), unsigned(cfg::scl));
#endif
  for (uint8_t address : {uint8_t(0x3c), uint8_t(0x3d)}) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) continue;
    oledPresent = oled.begin(SSD1306_SWITCHCAPVCC, address, false, false);
    logEvent("OLED address=0x%02X initialized=%d", unsigned(address), oledPresent);
    if (oledPresent) break;
  }
  if (oledPresent) {
    oled.clearDisplay(); oled.setTextColor(WHITE); oled.setTextSize(1); oled.setCursor(0, 0);
    oled.printf("Living Controller\nFirmware %s\nStarting...", cfg::version);
    oled.display(); setOled(true);
  }
  logEvent("OLED detected=%d; AHT20 measurement scheduled", oledPresent);
  sensor.begin(millis());
  services = startup::services(WIFI_SSID, WIFI_PASSWORD, MQTT_HOST, MQTT_USER, MQTT_PASSWORD,
                              OTA_PASSWORD, WEB_USER, WEB_PASSWORD);
  configured = services.wifi;
  logEvent("Configuration WiFi=%d MQTT=%d OTA=%d Web=%d", services.wifi, services.mqtt, services.ota, services.web);
  if (!configured) { safetyLocked = true; logEvent("WiFi configuration invalid; relay outputs locked OFF"); return; }
  // No flash writes on switching and no restoration of energized outputs.
  digitalWrite(cfg::oe, LOW);
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& event) {
    logEvent("WiFi disconnect reason=%u", unsigned(event.reason));
  });
  WiFi.hostname(deviceId.c_str()); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifi = millis(); offlineSince = millis();
  mqtt.setServer(MQTT_HOST, MQTT_PORT); mqtt.setClientId(deviceId.c_str());
  if (*MQTT_USER) mqtt.setCredentials(MQTT_USER, MQTT_PASSWORD);
  mqtt.setCleanSession(true);
  mqtt.setKeepAlive(15); mqtt.setWill(availabilityTopic.c_str(), 1, true, "offline");
  mqtt.onMessage(mqttMessage);
  mqtt.onPublish([](uint16_t packet) { if (packet == pendingPacket) pendingPacket = 0; });
  mqtt.onDisconnect([](AsyncMqttClientDisconnectReason reason) {
    mqttConnecting = false; logEvent("MQTT disconnected reason=%u", unsigned(reason));
  });
  ArduinoOTA.setHostname(deviceId.c_str()); ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([] {
    logEvent("OTA accepted; outputs disabled");
    closeWeather();
    updating = safetyLocked = true; allOff(true); setOled(false);
    if (mqtt.connected()) mqtt.publish(topic("availability").c_str(), 1, true, "offline");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    logEvent("OTA error=%u", unsigned(error));
    // Authentication/connection errors occur before onStart; they must not lock
    // a running controller. Only an interrupted accepted update stays locked.
    if (updating) { updating = false; safetyLocked = true; dirty = true; }
  });
  logEvent("Startup complete; network and weather handled in main loop");
}

void loop() {
  const uint32_t now = millis(); uptimeMs += uint32_t(now - previousMillis); previousMillis = now;
  if (sensor.tick(now)) { dirty = true; logEvent("AHT20 healthy=%d failures=%lu", sensor.healthy, static_cast<unsigned long>(sensor.failures)); }
  if (!configured) { displayTick(now); delay(10); return; }
  networkTick(now);
  if (networkServicesStarted && services.ota) ArduinoOTA.handle();
  if (networkServicesStarted && services.web) web.handleClient();
  if (restartRequested) { if (control::elapsed(now, restartStarted, 250)) ESP.restart(); delay(1); return; }
  if (updating) { delay(1); return; }
  processInbox();
  for (unsigned i = 0; i < 16; ++i)
    if (cfg::relayAutoOff[i] && (relays & (1u << i)) && control::elapsed(now, relayStarted[i], cfg::relayAutoOff[i])) setRelay(i, false);
  if (WiFi.status() != WL_CONNECTED) {
    if (mqttWasConnected) { mqttWasConnected = ready = false; offlineSince = now; pendingPacket = 0; }
    if (control::elapsed(now, lastWifi, 30000)) { lastWifi = now; WiFi.begin(WIFI_SSID, WIFI_PASSWORD); }
  }
  if (cfg::offlineOffMs && !mqtt.connected() && control::elapsed(now, offlineSince, cfg::offlineOffMs)) allOff();
  if (control::elapsed(now, lastTelemetry, cfg::telemetryPeriod)) { lastTelemetry = now; dirty = true; }
  mqttTick(now); displayTick(now); weatherTick(now); delay(1);
}
