#pragma once
#include <Adafruit_GFX.h>
#include "oled_model.h"
#include "oled_icons.h"
#include "solar_phase.h"
#include <Fonts/FreeSansBold9pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// All coordinates target the actual 128x32 panel. No dynamic allocation.
namespace displayui {
struct Snapshot {
  bool wifi = false, mqtt = false, sensor = false, weather = false, weatherStale = false;
  bool refreshing = false, clock = false, day = true, locked = false, updating = false, configured = true;
  float temperature = NAN, humidity = NAN, outside = NAN, outsideHumidity = NAN, cloud = NAN, rain = NAN;
  solarphase::Phase phase = solarphase::Phase::Unknown;
  uint32_t age = 0, uptime = 0, heap = 0;
  uint16_t relays = 0;
  uint8_t pwm[4] = {};
  int rssi = 0;
  const char* condition = "unknown";
  const char* location = "", *source = "", *timezone = "", *ip = "";
  const char* time = "", *date = "";
};
inline void text(Adafruit_GFX& d, int x, int y, const char* value, unsigned limit = 21) {
  d.setFont(nullptr); d.setTextSize(1); d.setTextColor(1); d.setCursor(x, y);
  // The panel's ASCII font cannot render UTF-8; replace complete codepoints,
  // cap every label and use an ellipsis marker instead of wrapping off-screen.
  unsigned count = 0;
  const unsigned capacity = unsigned((128 - x) / 6);
  if (limit > capacity) limit = capacity;
  while (*value && count < limit) {
    unsigned char c = static_cast<unsigned char>(*value++);
    if (c >= 128) { while ((*value & 0xc0) == 0x80) ++value; c = '?'; }
    if (count == limit - 1 && *value) c = '~';
    d.write(c >= 32 && c <= 126 ? c : ' '); ++count;
  }
}
inline void hero(Adafruit_GFX& d, int x, const char* value, unsigned maxWidth = 128) {
  d.setFont(&FreeSansBold9pt7b); d.setTextSize(1); d.setTextColor(1);
  int16_t bx, by; uint16_t w, h;
  d.getTextBounds(value, 0, 0, &bx, &by, &w, &h);
  // Top at y=10; compact fallback also handles outlier temperatures/IP text.
  if (maxWidth > unsigned(128 - x)) maxWidth = 128 - x;
  if (w > maxWidth || h > 15) { text(d, x, 13, value, maxWidth/6); return; }
  d.setCursor(x - bx, 10 - by); d.print(value); d.setFont(nullptr);
}
inline const uint8_t* iconBitmap(Icon kind, bool day, bool small) {
  using namespace displayicons;
  switch (kind) {
    case Icon::Sun: return small ? sun12 : sun22;
    case Icon::Moon: return small ? moon12 : moon22;
    case Icon::Partial: return day ? (small ? cloud_sun12 : cloud_sun22) : (small ? cloud_moon12 : cloud_moon22);
    case Icon::Cloud: return small ? cloud12 : cloud22;
    case Icon::Rain: return small ? cloud_rain12 : cloud_rain22;
    case Icon::Snow: return small ? cloud_snow12 : cloud_snow22;
    case Icon::Fog: return small ? cloud_fog12 : cloud_fog22;
    case Icon::Storm: return small ? cloud_lightning12 : cloud_lightning22;
    case Icon::Wind: return small ? wind12 : wind22;
    default: return small ? circle_question_mark12 : circle_question_mark22;
  }
}
inline void icon(Adafruit_GFX& d, Icon kind, bool day, int x = 4, int y = 10, bool small = false) {
  const unsigned size = small ? 12 : 22;
  d.drawBitmap(x, y, iconBitmap(kind, day, small), size, size, 1);
}
inline void phaseIcon(Adafruit_GFX& d, solarphase::Phase phase, int x, int y) {
  using namespace displayicons;
  const uint8_t* bitmap = circle_question_mark12;
  switch (phase) {
    case solarphase::Phase::Day: bitmap = sun12; break;
    case solarphase::Phase::Night: bitmap = moon12; break;
    case solarphase::Phase::Sunrise: bitmap = sunrise12; break;
    case solarphase::Phase::Sunset: bitmap = sunset12; break;
    default: break;
  }
  d.drawBitmap(x, y, bitmap, 12, 12, 1);
}
inline void render(Adafruit_GFX& d, unsigned page, const Snapshot& s, bool pinned) {
  d.setTextWrap(false);
  static const char* const titles[] = {"HOME", "INDOOR", "OUTDOOR", "SKY / RAIN", "RELAYS", "DIMMERS", "NETWORK"};
  text(d, 0, 0, titles[page < Count ? page : unsigned(Clock)], 10);
  // Seven steps identify the carousel; underline marks a pinned page.
  for (unsigned i = 0; i < Count; ++i) {
    if (i == page) d.fillRect(65 + i*4, 2, 3, 3, 1);
    else d.drawPixel(66+i*4,3,1);
  }
  if (pinned) d.drawFastHLine(65 + page*4,6,3,1);
  text(d, 97, 0, s.wifi ? "W" : "-", 1); text(d, 109, 0, s.mqtt ? "M" : "-", 1);
  if (s.wifi) for (int i = 0; i < 3; ++i) if (s.rssi >= -85 + i*10) d.fillRect(117+i*4,5-i*2,2,2+i*2,1);
  d.drawFastHLine(0,8,128,1);
  if (s.updating || s.locked || !s.configured) {
    text(d, 0, 12, s.updating ? "Updating firmware" : s.locked ? "OUTPUTS LOCKED" : "SETUP REQUIRED");
    text(d, 0, 24, s.updating ? "Please wait..." : s.locked ? "Check device logs" : "Configure WiFi"); return;
  }
  char a[32], b[32];
  switch (page) {
    case Clock:
      if (s.clock) {
        phaseIcon(d,s.phase,0,11);
        icon(d,s.weather ? weatherIcon(s.condition,s.day) : Icon::Unknown,s.day,15,11,true);
        hero(d,33,s.time,95);
        text(d,0,24,s.date,10); text(d,62,24,s.timezone,11);
      }
      else { text(d,0,12,"Clock syncing..."); text(d,0,24,s.wifi ? "Waiting for NTP" : "Waiting for WiFi"); }
      break;
    case Room:
      if (s.sensor) {
        snprintf(a,sizeof(a),"%.1f C",s.temperature); hero(d,0,a,80);
        snprintf(a,sizeof(a),"RH %.0f%%",s.humidity); text(d,84,13,a,7);
        text(d,0,24,"AHT20 / live indoors");
      } else { text(d,0,12,"AHT20 unavailable"); text(d,0,24,"Check power / wiring"); }
      break;
    case Weather:
      if (s.weather) {
        const auto kind = weatherIcon(s.condition,s.day); icon(d,kind,s.day);
        snprintf(a,sizeof(a),"%.1f C",s.outside); hero(d,34,a,66);
        snprintf(a,sizeof(a),"%.0f%%",s.outsideHumidity); text(d,104,14,a,4);
        text(d,34,24,iconLabel(kind),15);
      } else { icon(d,Icon::Unknown,true); text(d,34,12,s.weatherStale ? "Weather expired" : "No weather data",15);
        text(d,34,24,s.refreshing ? "Refreshing..." : "Refresh via web",15); }
      break;
    case Sky:
      if (s.weather) {
        if (isfinite(s.cloud)) snprintf(a,sizeof(a),"Cloud %.0f%%",s.cloud); else snprintf(a,sizeof(a),"Cloud --");
        if (isfinite(s.rain)) snprintf(b,sizeof(b),"%.1fmm",s.rain); else snprintf(b,sizeof(b),"--mm");
        text(d,0,12,a,12); text(d,80,12,b,8);
        snprintf(a,sizeof(a),"%s %lum %s",s.source,static_cast<unsigned long>(s.age/60),s.day ? "day" : "night");
        text(d,0,24,s.refreshing ? "Refreshing weather..." : a);
      } else { text(d,0,12,s.weatherStale ? "Weather expired" : "Waiting for weather"); text(d,0,24,"Refresh on dashboard"); }
      break;
    case Relays: {
      // Numbered two-bank map: filled = commanded ON; outline = OFF.
      for (unsigned i=0;i<16;++i) {
        const int x = (i%8)*16, y = 10+(i/8)*11;
        const bool on = s.relays & (1u<<i);
        if (on) d.fillRect(x,y,15,10,1); else d.drawRect(x,y,15,10,1);
        snprintf(a,sizeof(a),"%u",i+1); d.setFont(nullptr); d.setTextSize(1); d.setTextColor(on?0:1);
        d.setCursor(x+(i<9?5:2),y+1); d.print(a);
      }
      d.setTextColor(1); break;
    }
    case Lights:
      for (unsigned i=0;i<4;++i) {
        const int x=(i%2)*64, y=11+(i/2)*12;
        snprintf(a,sizeof(a),"%u %3u%%",i+1,(unsigned(s.pwm[i])*100+127)/255); text(d,x,y,a,6);
        d.drawRect(x+39,y,23,7,1); d.fillRect(x+40,y+1,(unsigned(s.pwm[i])*21+127)/255,5,1);
      }
      break;
    case Network:
      text(d,0,12,s.wifi ? s.ip : "WiFi reconnecting");
      snprintf(a,sizeof(a),"%s %luh %luk",s.mqtt ? "MQTT OK" : "MQTT --",static_cast<unsigned long>(s.uptime/3600),static_cast<unsigned long>(s.heap/1024));
      text(d,0,24,a); break;
    default: break;
  }
}
} // namespace displayui
