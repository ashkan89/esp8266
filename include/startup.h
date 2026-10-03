#pragma once
#include <stddef.h>
#include <string.h>
namespace startup {
inline bool value(const char* text, size_t minimum = 1) {
  return text && strlen(text) >= minimum && strcmp(text, "CHANGE_ME");
}
struct Services { bool wifi, mqtt, ota, web; };
inline Services services(const char* ssid, const char* wifiPassword,
                         const char* mqttHost, const char* mqttUser, const char* mqttPassword,
                         const char* otaPassword, const char* webUser, const char* webPassword) {
  const bool wifi = value(ssid) && value(wifiPassword, 8);
  const bool brokerAuth = (value(mqttUser) && value(mqttPassword)) ||
                          (mqttUser && mqttPassword && !*mqttUser && !*mqttPassword);
  return {wifi, wifi && value(mqttHost) && brokerAuth,
          wifi && value(otaPassword, 8), wifi && value(webUser) && value(webPassword, 8)};
}
}
