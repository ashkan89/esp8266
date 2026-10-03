#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
struct MqttMessage { char topic[128]; char payload[513]; };
class MqttInbox {
 public:
  // Returns false once per rejected message, or per invalid fragment. Callback
  // only assembles/queues data; GPIO and JSON processing happen in the main loop.
  bool receive(const char* topic, const char* data, bool retained,
               size_t len, size_t index, size_t total) {
    if (index == 0) {
      bytes_ = 0; total_ = total;
      valid_ = total <= 512 && strlen(topic) < sizeof(assembling_.topic) &&
               (!retained || !strcmp(topic, "homeassistant/status"));
      if (!valid_) return false;
      memcpy(assembling_.topic, topic, strlen(topic) + 1);
    }
    if (!valid_) return true;
    if (index != bytes_ || total != total_ || index > total || len > total - index ||
        strcmp(topic, assembling_.topic)) { valid_ = false; return false; }
    memcpy(assembling_.payload + index, data, len); bytes_ += len;
    if (bytes_ != total) return true;
    assembling_.payload[total] = 0; valid_ = false;
    if (strlen(assembling_.payload) != total) return false;
    const uint8_t next = (write_ + 1) % 8;
    if (next == read_) return false;
    messages_[write_] = assembling_; write_ = next; return true;
  }
  bool pop(MqttMessage& result) {
    if (read_ == write_) return false;
    result = messages_[read_]; read_ = (read_ + 1) % 8; return true;
  }
 private:
  MqttMessage messages_[8], assembling_;
  volatile uint8_t read_ = 0, write_ = 0;
  size_t bytes_ = 0, total_ = 0;
  bool valid_ = false;
};
