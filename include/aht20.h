#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "control.h"
// Nonblocking AHT20 conversion, CRC validation and recovery on every failed cycle.
class Aht20 {
 public:
  float temperature = NAN, humidity = NAN;
  bool healthy = false;
  uint32_t failures = 0;
  void begin(uint32_t now) { started_ = now; phase_ = 0; }
  bool tick(uint32_t now) {
    if (!control::elapsed(now, started_, phase_ == 0 ? 50 : phase_ == 1 ? 20 : phase_ == 2 ? 90 : 10000)) return false;
    if (phase_ == 4) { phase_ = 0; started_ = now; return false; }
    if (phase_ == 0) {
      const uint8_t init[] = {0xbe, 0x08, 0x00};
      if (!send(init, 3)) return fail(now);
      phase_ = 1; started_ = now; return false;
    }
    if (phase_ == 1 || phase_ == 3) {
      const uint8_t trigger[] = {0xac, 0x33, 0x00};
      if (!send(trigger, 3)) return fail(now);
      phase_ = 2; started_ = now; return false;
    }
    uint8_t bytes[7];
    if (Wire.requestFrom(uint8_t(0x38), uint8_t(7)) != 7) return fail(now);
    for (auto& b : bytes) b = Wire.read();
    if ((bytes[0] & 0x80) || !(bytes[0] & 0x08) || control::crc8(bytes, 6) != bytes[6]) return fail(now);
    const uint32_t rawH = (uint32_t(bytes[1]) << 12) | (uint32_t(bytes[2]) << 4) | (bytes[3] >> 4);
    const uint32_t rawT = (uint32_t(bytes[3] & 15) << 16) | (uint32_t(bytes[4]) << 8) | bytes[5];
    humidity = rawH * (100.0f / 1048576.0f);
    temperature = rawT * (200.0f / 1048576.0f) - 50.0f;
    healthy = true; phase_ = 3; started_ = now; return true;
  }
 private:
  uint8_t phase_ = 0;
  uint32_t started_ = 0;
  bool send(const uint8_t* data, size_t size) {
    Wire.beginTransmission(0x38); Wire.write(data, size); return Wire.endTransmission() == 0;
  }
  bool fail(uint32_t now) {
    healthy = false; temperature = humidity = NAN; ++failures;
    phase_ = 4; started_ = now;
    return true;
  }
};
