#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "control.h"
#include "aht20.h"
#include "mqtt_inbox.h"
#include "startup.h"
#include "weather_http.h"
#include "weather_request.h"
#include "oled_model.h"
#include "solar_phase.h"
FakeWire Wire;
void sensorPacket(uint8_t status = 0x08) {
  // 50% RH, 25 Celsius, calibrated and not busy.
  Wire.packet = {status, 0x80, 0, 0x06, 0, 0, 0};
  Wire.packet[6] = control::crc8(Wire.packet.data(), 6);
}
int main() {
  using solarphase::Phase;
  // 2026-03-20 around the equinox: UTC solar noon/midnight at Greenwich.
  assert(solarphase::calculate(1774008000, 0, 0).phase == Phase::Day);
  assert(solarphase::calculate(1773964800, 0, 0).phase == Phase::Night);
  assert(solarphase::calculate(1773986400, 0, 0).phase == Phase::Sunrise);
  assert(solarphase::calculate(1774029600, 0, 0).phase == Phase::Sunset);
  assert(solarphase::calculate(1774008000, 0, 180).phase == Phase::Night);
  assert(solarphase::calculate(1773964800, 0, 180).phase == Phase::Day);
  assert(solarphase::calculate(1782000000, 78, 0).phase == Phase::Day); // Polar summer midnight.
  assert(solarphase::calculate(1797811200, 78, 0).phase == Phase::Night);
  assert(solarphase::calculate(1782000000, -78, 0).phase == Phase::Night);
  assert(solarphase::calculate(0, 35.6892f, 51.3890f).phase == Phase::Unknown);
  assert(solarphase::calculate(1774008000, NAN, 0).phase == Phase::Unknown);
  assert(solarphase::calculate(1774008000, 91, 0).phase == Phase::Unknown);
  assert(solarphase::calculate(1774008000, 0, 181).phase == Phase::Unknown);
  displayui::Timeout timeout;
  assert(!timeout.expired(100000) && timeout.remaining(100000) == 0);
  assert(timeout.select("1", 100));
  assert(timeout.minutes() == 1 && timeout.remaining(100) == 60);
  assert(!timeout.expired(60099) && timeout.remaining(60099) == 1);
  assert(timeout.expired(60100) && timeout.remaining(60100) == 0);
  timeout.wake(70000); assert(!timeout.expired(70000) && timeout.remaining(70000) == 60);
  for (const char* invalid : {"", "0", "-1", "1.5", " 1", "1m", "1441", "99999", "off"}) {
    assert(!timeout.select(invalid, 80000));
    assert(timeout.minutes() == 1 && timeout.remaining(80000) == 50);
  }
  assert(timeout.select("1440", 0)); assert(timeout.expired(86400000));
  assert(timeout.select("1", 0xfffffff0));
  assert(!timeout.expired(0x00000010) && timeout.remaining(0x00000010) == 60);
  assert(timeout.expired(0x0000ea50));
  assert(timeout.select("OFF", 0)); assert(!timeout.expired(0xffffffff));
  displayui::Navigation pages;
  assert(pages.page(0) == displayui::Clock);
  assert(pages.page(7999) == displayui::Clock);
  assert(pages.page(8000) == displayui::Room);
  assert(pages.page(14000) == displayui::Weather);
  assert(pages.page(22000) == displayui::Sky);
  assert(pages.page(27000) == displayui::Relays);
  assert(pages.page(32000) == displayui::Lights);
  assert(pages.page(37000) == displayui::Network);
  assert(pages.page(42000) == displayui::Clock);
  assert(pages.select("weather", 70000));
  assert(pages.page(90000) == displayui::Weather);
  assert(!pages.select("WEATHER", 90000)); // Invalid command must preserve the mode.
  assert(!strcmp(pages.mode(), "weather"));
  assert(pages.select("AUTO", 0xfffff000));
  assert(pages.page(0x00001000) == displayui::Room); // Wrap-safe carousel.
  pages.wake(100); assert(pages.page(100) == displayui::Clock);
  using displayui::Icon;
  assert(displayui::weatherIcon("unknown", true) == Icon::Unknown);
  assert(displayui::weatherIcon("sunny", true) == Icon::Sun);
  assert(displayui::weatherIcon("clear-night", false) == Icon::Moon);
  assert(displayui::weatherIcon("partlycloudy", false) == Icon::Partial);
  assert(displayui::weatherIcon("cloudy", true) == Icon::Cloud);
  assert(displayui::weatherIcon("lightning-rainy", true) == Icon::Storm);
  assert(displayui::weatherIcon("snowy-rainy", true) == Icon::Snow);
  assert(displayui::weatherIcon("pouring", true) == Icon::Rain);
  assert(displayui::weatherIcon("fog", true) == Icon::Fog);
  assert(displayui::weatherIcon("windy-variant", true) == Icon::Wind);
  WeatherRequest refresh;
  assert(!refresh.pending() && !refresh.waiting(75000, 5000));
  refresh.request(); assert(refresh.pending() && !refresh.notified());
  refresh.notify(0xfffffff0);
  refresh.request(); // Repeated commands must not extend the response deadline.
  assert(refresh.notified() && refresh.waiting(0x10, 5000));
  assert(!refresh.waiting(0x1378, 5000));
  refresh.finish(); assert(!refresh.pending() && !refresh.notified());
  assert(!refresh.waiting(0x10, 5000));
  refresh.request(); assert(refresh.pending() && !refresh.notified());
  refresh.finish();
  const auto valid = startup::services("network", "123456789", "192.168.1.10", "user", "123456789", "123456789", "admin", "123456789");
  assert(valid.wifi && valid.mqtt && valid.ota && valid.web);
  const auto incomplete = startup::services("network", "123456789", "CHANGE_ME", "user", "CHANGE_ME", "CHANGE_ME", "admin", "CHANGE_ME");
  assert(incomplete.wifi && !incomplete.mqtt && !incomplete.ota && !incomplete.web);
  const auto local = startup::services("network", "123456789", "host", "", "", "123456789", "admin", "123456789");
  assert(local.wifi && local.mqtt && local.ota && local.web);
  const auto invalid = startup::services("CHANGE_ME", "CHANGE_ME", "host", "user", "password", "password", "admin", "password");
  assert(!invalid.wifi && !invalid.mqtt && !invalid.ota && !invalid.web);
  for (uint32_t mask = 0; mask < 65536; ++mask)
    assert(uint16_t(control::physicalBits(uint16_t(mask)) ^ uint16_t(mask)) == 0xffff);
  const uint16_t groups[] = {0x0003, 0x000c};
  uint16_t state = control::setRelay(0, 0, true, groups, 2);
  assert(state == 1);
  state = control::setRelay(state, 2, true, groups, 2); assert(state == 5);
  state = control::setRelay(state, 1, true, groups, 2); assert(state == 6);
  state = control::setRelay(state, 3, true, groups, 2); assert(state == 10);
  assert(control::setRelay(state, 16, true, groups, 2) == state);
  assert(control::setRelay(state, 1, false, groups, 2) == 8);
  for (unsigned i = 0; i < 16; ++i)
    assert(control::setRelay(0, i, true, nullptr, 0) == (1u << i));
  unsigned channel = 99;
  assert(control::channel("01", 16, channel) && channel == 0);
  assert(control::channel("16", 16, channel) && channel == 15);
  for (const char* invalid : {"", "0", "17", "-1", "1x", "1/set", "4294967297"})
    assert(!control::channel(invalid, 16, channel));
  bool on;
  assert(control::switchCommand("ON", on) && on);
  assert(control::switchCommand("OFF", on) && !on);
  for (const char* invalid : {"on", "OFF ", "1", "ONx", ""}) assert(!control::switchCommand(invalid, on));
  assert(!control::elapsed(0x10, 0xfffffff0, 33));
  assert(control::elapsed(0x10, 0xfffffff0, 32));
  const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
  assert(control::crc8(check, 9) == 0xf7);
  Aht20 aht; aht.begin(0); sensorPacket();
  assert(!aht.tick(49) && Wire.sends == 0);
  assert(!aht.tick(50) && Wire.sends == 1);
  assert(!aht.tick(69) && Wire.sends == 1);
  assert(!aht.tick(70) && Wire.sends == 2);
  assert(!aht.tick(159));
  assert(aht.tick(160) && aht.healthy);
  assert(fabs(aht.temperature - 25) < 0.01 && fabs(aht.humidity - 50) < 0.01);
  assert(!aht.tick(10159)); assert(!aht.tick(10160));
  Wire.packet[6] ^= 1;
  assert(aht.tick(10250) && !aht.healthy && isnan(aht.temperature) && aht.failures == 1);
  const auto sends = Wire.sends;
  assert(!aht.tick(10251) && Wire.sends == sends);
  assert(!aht.tick(20249) && Wire.sends == sends);
  assert(!aht.tick(20250) && Wire.sends == sends);
  assert(!aht.tick(20300) && Wire.sends == sends + 1);
  sensorPacket(0x88); aht.tick(20320);
  assert(aht.tick(20410) && !aht.healthy && aht.failures == 2);
  // Failed bus writes, incomplete reads, missing calibration and wrap-around recovery.
  for (unsigned failure = 0; failure < 3; ++failure) {
    Aht20 test; test.begin(0xfffffff0);
    Wire.failSend = failure == 0;
    sensorPacket(failure == 2 ? 0 : 0x08);
    if (failure == 1) Wire.packet.pop_back();
    if (failure == 0) assert(test.tick(34));
    else { test.tick(34); test.tick(54); assert(test.tick(144)); }
    assert(test.failures == 1 && !test.healthy);
  }
  MqttInbox inbox; MqttMessage message;
  assert(inbox.receive("relay/01/set", "O", false, 1, 0, 2));
  assert(!inbox.pop(message));
  assert(inbox.receive("relay/01/set", "N", false, 1, 1, 2));
  assert(inbox.pop(message) && !strcmp(message.payload, "ON"));
  assert(!inbox.receive("relay/01/set", "ON", true, 2, 0, 2));
  assert(!inbox.pop(message));
  assert(inbox.receive("homeassistant/status", "online", true, 6, 0, 6));
  assert(inbox.pop(message));
  assert(!inbox.receive("relay/01/set", "ON", false, 2, 0, 513));
  assert(!inbox.receive("relay/01/set", "ON", false, 2, 0, 1));
  const char embeddedNull[] = {'O', 'N', 0, 'x'};
  assert(!inbox.receive("relay/01/set", embeddedNull, false, 4, 0, 4));
  assert(inbox.receive("relay/01/set", "O", false, 1, 0, 2));
  assert(!inbox.receive("relay/02/set", "N", false, 1, 1, 2));
  assert(!inbox.pop(message));
  assert(inbox.receive("relay/01/set", "O", false, 1, 0, 2));
  assert(!inbox.receive("relay/01/set", "N", false, 1, 1, 3));
  assert(inbox.receive("relay/01/set", "O", false, 1, 0, 3));
  assert(!inbox.receive("relay/01/set", "N", false, 1, 2, 3));
  for (unsigned i = 0; i < 7; ++i) assert(inbox.receive("relay/01/set", "ON", false, 2, 0, 2));
  assert(!inbox.receive("relay/01/set", "ON", false, 2, 0, 2));
  for (unsigned i = 0; i < 7; ++i) assert(inbox.pop(message));
  assert(!inbox.pop(message));
  const auto feed = [](WeatherHttp& parser, const char* text) {
    for (const char* p = text; *p && parser.status() == WeatherHttp::Status::Receiving; ++p) parser.feed(*p);
  };
  WeatherHttp http;
  feed(http, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}");
  assert(http.status() == WeatherHttp::Status::Complete && !strcmp(http.body(), "{}"));
  http.reset(); feed(http, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\n{\r\n1;ignored=1\r\n}\r\n0\r\n\r\n");
  assert(http.status() == WeatherHttp::Status::Complete && !strcmp(http.body(), "{}"));
  http.reset(); feed(http, "HTTP/1.0 200 OK\r\n\r\n{}"); http.closed();
  assert(http.status() == WeatherHttp::Status::Complete);
  for (const char* bad : {"HTTP/1.1 503 Failed\r\n", "HTTP/1.1 2000 bad\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n", "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\ninvalid\r\n"}) {
    http.reset(); feed(http, bad); assert(http.status() == WeatherHttp::Status::Error);
  }
  http.reset(); feed(http, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n{}"); http.closed();
  assert(http.status() == WeatherHttp::Status::Error);
  http.reset(); feed(http, "HTTP/1.0 200 OK\r\n\r\n");
  for (unsigned i = 0; i < 2049; ++i) http.feed('x');
  assert(http.status() == WeatherHttp::Status::Error);
  puts("PASS: startup, relays, timing, AHT20, MQTT framing, bounded HTTP fixed/chunked/close-delimited weather responses");
}
