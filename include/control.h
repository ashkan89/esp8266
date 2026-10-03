#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace control {
inline bool elapsed(uint32_t now, uint32_t since, uint32_t period) {
  return uint32_t(now - since) >= period;
}
inline uint16_t physicalBits(uint16_t logical) { return uint16_t(~logical); }
inline uint16_t setRelay(uint16_t mask, unsigned channel, bool on,
                         const uint16_t* groups, size_t count) {
  if (channel >= 16) return mask;
  const uint16_t bit = uint16_t(1u << channel);
  if (!on) return uint16_t(mask & ~bit);
  for (size_t i = 0; i < count; ++i)
    if (groups[i] & bit) mask &= uint16_t(~groups[i]);
  return mask | bit;
}
inline bool switchCommand(const char* value, bool& on) {
  if (!strcmp(value, "ON")) { on = true; return true; }
  if (!strcmp(value, "OFF")) { on = false; return true; }
  return false;
}
// Strict decimal channel parsing; prevents malformed topics selecting a relay.
inline bool channel(const char* text, unsigned max, unsigned& result) {
  if (!*text) return false;
  unsigned n = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9' || n > max) return false;
    n = n * 10 + unsigned(*p - '0');
  }
  if (!n || n > max) return false;
  result = n - 1; return true;
}
inline uint8_t crc8(const uint8_t* data, size_t size) {
  uint8_t crc = 0xff;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (unsigned j = 0; j < 8; ++j)
      crc = uint8_t((crc << 1) ^ ((crc & 0x80) ? 0x31 : 0));
  }
  return crc;
}
}

