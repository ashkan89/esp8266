#pragma once
#include <stdint.h>
#include <string.h>

namespace displayui {
class Timeout {
 public:
  explicit Timeout(uint32_t duration = 0) : duration_(duration) {}
  bool select(const char* value, uint32_t now) {
    uint32_t minutes = 0;
    if (strcmp(value, "OFF")) {
      const size_t length = strlen(value);
      if (!length || length > 4) return false;
      for (const char* p = value; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        minutes = minutes * 10 + unsigned(*p - '0');
      }
      if (!minutes || minutes > 1440) return false;
    }
    duration_ = minutes * 60000; started_ = now; return true;
  }
  void wake(uint32_t now) { started_ = now; }
  bool expired(uint32_t now) const { return duration_ && uint32_t(now - started_) >= duration_; }
  uint32_t remaining(uint32_t now) const {
    if (!duration_ || expired(now)) return 0;
    return (duration_ - uint32_t(now - started_) + 999) / 1000;
  }
  unsigned minutes() const { return (duration_ + 59999) / 60000; }
 private:
  uint32_t duration_ = 0, started_ = 0;
};
enum Page : uint8_t { Clock, Room, Weather, Sky, Relays, Lights, Network, Count };
inline const char* pageName(unsigned page) {
  static const char* const names[] = {"clock", "room", "weather", "sky", "relays", "lights", "network"};
  return page < Count ? names[page] : "AUTO";
}
class Navigation {
 public:
  bool select(const char* name, uint32_t now) {
    if (!strcmp(name, "AUTO")) { pinned_ = Count; started_ = now; return true; }
    for (unsigned i = 0; i < Count; ++i) if (!strcmp(name, pageName(i))) {
      pinned_ = i; started_ = now; return true;
    }
    return false;
  }
  void wake(uint32_t now) { started_ = now; }
  const char* mode() const { return pageName(pinned_); }
  unsigned page(uint32_t now) const {
    if (pinned_ < Count) return pinned_;
    static const uint32_t durations[] = {8000, 6000, 8000, 5000, 5000, 5000, 5000};
    uint32_t phase = uint32_t(now - started_) % 42000;
    for (unsigned i = 0; i < Count; ++i) { if (phase < durations[i]) return i; phase -= durations[i]; }
    return Clock;
  }
 private:
  unsigned pinned_ = Count;
  uint32_t started_ = 0;
};
enum class Icon { Unknown, Sun, Moon, Partial, Cloud, Rain, Snow, Fog, Storm, Wind };
inline Icon weatherIcon(const char* condition, bool day) {
  if (strstr(condition, "lightning") || strstr(condition, "storm")) return Icon::Storm;
  if (strstr(condition, "snow") || strstr(condition, "hail")) return Icon::Snow;
  if (strstr(condition, "rain") || strstr(condition, "pour")) return Icon::Rain;
  if (strstr(condition, "fog")) return Icon::Fog;
  if (strstr(condition, "partly")) return Icon::Partial;
  if (strstr(condition, "cloud")) return Icon::Cloud;
  if (strstr(condition, "wind")) return Icon::Wind;
  if (!strcmp(condition, "sunny") || !strcmp(condition, "clear-night") || !strcmp(condition, "clear"))
    return day ? Icon::Sun : Icon::Moon;
  return Icon::Unknown;
}
inline const char* iconLabel(Icon icon) {
  static const char* const labels[] = {"Unknown", "Sunny", "Clear night", "Partly cloudy", "Cloudy", "Rain", "Snow / hail", "Fog", "Thunderstorm", "Windy"};
  return labels[unsigned(icon)];
}
} // namespace displayui
