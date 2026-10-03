#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
struct FakeWire {
  unsigned sends = 0;
  bool failSend = false;
  uint8_t address = 0;
  size_t cursor = 0;
  std::vector<uint8_t> packet;
  void beginTransmission(uint8_t a) { address = a; }
  size_t write(const uint8_t*, size_t n) { return n; }
  uint8_t endTransmission() { ++sends; return failSend ? 1 : 0; }
  uint8_t requestFrom(uint8_t a, uint8_t) { address = a; cursor = 0; return uint8_t(packet.size()); }
  int read() { return packet.at(cursor++); }
};
extern FakeWire Wire;
