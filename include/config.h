#pragma once
#include <Arduino.h>
#include "secrets.h"
namespace cfg {
constexpr char name[] = "Living Room Controller";
constexpr char version[] = "1.1.3";
constexpr char timezone[] = "IRST-3:30";
// POSIX timezone for the ESP clock; IANA name for display/browser formatting.
constexpr char timezoneName[] = "Asia/Tehran";
constexpr char region[] = "Iran", location[] = "Tehran";
constexpr bool haLocationOverride = true;
constexpr uint32_t haWeatherWaitMs = 5000; // Nonblocking wait after explicit refresh.
constexpr uint8_t sda = 4, scl = 5, clock = 14, data = 13, latch = 0, oe = 2;
constexpr uint8_t led = 16;
constexpr uint8_t pwmPins[] = {12, 1, 15, 3};
constexpr uint32_t pwmFrequency = 1000;
constexpr uint32_t sensorPeriod = 10000, telemetryPeriod = 10000;
constexpr uint32_t oledTimeout = 0; // Boot default in ms; dashboard/MQTT can override until reboot.
static_assert(oledTimeout <= 86400000, "OLED timeout must not exceed 24 hours");
constexpr uint32_t weatherExpiry = 1800000;
constexpr bool weatherFallback = false; // Direct HTTPS only after explicit refresh.
constexpr float weatherLatitude = 35.6892f, weatherLongitude = 51.3890f;
constexpr uint32_t weatherRetryMs = 60000; // Minimum interval between direct requests.
constexpr uint32_t weatherResponseTimeoutMs = 6000;
// 0 = keep commanded outputs during outages. Set a timeout to turn everything off.
constexpr uint32_t offlineOffMs = 0;
// Per-channel automatic off time (ms), 0 = unlimited. No relay state restored at boot.
constexpr uint32_t relayAutoOff[16] = {};
// Each group bitmask allows only one active relay. Example 0x0003 interlocks 1 and 2.
// This does not provide break-before-make deadtime; do not use for motor reversal.
constexpr uint16_t interlocks[] = {0};
static_assert(clock == 14 && data == 13, "ESP8266 hardware SPI requires GPIO14/GPIO13");
static_assert(pwmFrequency >= 100 && pwmFrequency <= 40000, "Unsupported PWM frequency");
}
