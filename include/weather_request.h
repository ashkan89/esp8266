#pragma once
#include <stdint.h>

// Idle at boot. Repeated refresh commands coalesce into one request.
class WeatherRequest {
 public:
  bool pending() const { return pending_; }
  bool notified() const { return notified_; }
  void request() { if (!pending_) { pending_ = true; notified_ = false; } }
  void notify(uint32_t now) { notified_ = true; sentAt_ = now; }
  bool waiting(uint32_t now, uint32_t timeout) const {
    return pending_ && notified_ && uint32_t(now - sentAt_) < timeout;
  }
  void finish() { pending_ = notified_ = false; }
 private:
  bool pending_ = false, notified_ = false;
  uint32_t sentAt_ = 0;
};
