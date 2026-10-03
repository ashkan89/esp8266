#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
// Bounded HTTP/1.x response decoder. Supports fixed-length, chunked and
// close-delimited bodies, and rejects oversized/truncated or non-200 responses.
class WeatherHttp {
 public:
  static constexpr size_t capacity = 2048;
  enum class Status { Receiving, Complete, Error };
  void reset() {
    state_ = State::StatusLine; status_ = Status::Receiving;
    lineSize_ = bodySize_ = headerBytes_ = chunkRemaining_ = 0;
    contentLength_ = -1; chunked_ = false; body_[0] = 0; error_ = "";
  }
  WeatherHttp() { reset(); }
  Status status() const { return status_; }
  const char* body() const { return body_; }
  const char* error() const { return error_; }
  bool feed(char byte) {
    if (status_ != Status::Receiving) return false;
    if (state_ == State::Body || state_ == State::ChunkData) {
      if (bodySize_ >= capacity) return fail("body too large");
      body_[bodySize_++] = byte; body_[bodySize_] = 0;
      if (state_ == State::ChunkData) {
        if (--chunkRemaining_ == 0) state_ = State::ChunkEnd;
      } else if (contentLength_ >= 0 && bodySize_ == size_t(contentLength_)) status_ = Status::Complete;
      return true;
    }
    if (++headerBytes_ > 4096) return fail("headers/chunk framing too large");
    if (byte == '\r') return true;
    if (byte != '\n') {
      if (lineSize_ + 1 >= sizeof(line_)) return fail("header line too large");
      line_[lineSize_++] = byte; return true;
    }
    line_[lineSize_] = 0;
    const bool result = line(); lineSize_ = 0; return result;
  }
  void closed() {
    if (status_ != Status::Receiving) return;
    if (state_ == State::Body && contentLength_ < 0 && bodySize_ > 0) status_ = Status::Complete;
    else fail("truncated HTTP response");
  }
 private:
  enum class State { StatusLine, Headers, Body, ChunkSize, ChunkData, ChunkEnd, Trailers };
  State state_;
  Status status_;
  char line_[256], body_[capacity + 1];
  size_t lineSize_, bodySize_, headerBytes_, chunkRemaining_;
  long contentLength_;
  bool chunked_;
  const char* error_;
  bool fail(const char* message) { error_ = message; status_ = Status::Error; return false; }
  static bool equal(const char* a, const char* b) {
    while (*a && *b) if (tolower(static_cast<unsigned char>(*a++)) != tolower(static_cast<unsigned char>(*b++))) return false;
    return *a == *b;
  }
  bool line() {
    if (state_ == State::StatusLine) {
      if (strncmp(line_, "HTTP/1.", 7) || strlen(line_) < 12 || line_[8] != ' ' || strncmp(line_ + 9, "200", 3) || (line_[12] && line_[12] != ' ')) return fail("HTTP status is not 200");
      state_ = State::Headers; return true;
    }
    if (state_ == State::Headers) {
      if (!*line_) {
        if (chunked_ && contentLength_ >= 0) return fail("ambiguous HTTP body framing");
        state_ = chunked_ ? State::ChunkSize : State::Body;
        if (contentLength_ == 0) return fail("empty body");
        return true;
      }
      char* colon = strchr(line_, ':'); if (!colon) return fail("invalid header");
      *colon++ = 0; while (*colon == ' ' || *colon == '\t') ++colon;
      if (equal(line_, "Content-Length")) {
        char* end; const long size = strtol(colon, &end, 10);
        if (!*colon || *end || size < 0 || size > long(capacity) || contentLength_ >= 0) return fail("invalid body length");
        contentLength_ = size;
      } else if (equal(line_, "Transfer-Encoding")) {
        if (!equal(colon, "chunked")) return fail("unsupported transfer encoding");
        chunked_ = true;
      }
      return true;
    }
    if (state_ == State::ChunkSize) {
      char* end; const unsigned long size = strtoul(line_, &end, 16);
      if (!*line_ || !isxdigit(static_cast<unsigned char>(*line_)) || (*end && *end != ';') || size > capacity - bodySize_) return fail("invalid chunk size");
      chunkRemaining_ = size;
      state_ = size ? State::ChunkData : State::Trailers; return true;
    }
    if (state_ == State::ChunkEnd) {
      if (*line_) return fail("invalid chunk ending");
      state_ = State::ChunkSize; return true;
    }
    if (state_ == State::Trailers) {
      if (!*line_) status_ = Status::Complete;
      return true;
    }
    return fail("invalid response state");
  }
};
