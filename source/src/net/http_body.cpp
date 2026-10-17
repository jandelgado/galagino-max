#include <cstring>
#include <cctype>
#include <cstdlib>
#include "http_body.h"

static constexpr int HTTP_OK = 200;
static const char STATUS_PREFIX[] = "HTTP/1.";
static const char LENGTH_HEADER[] = "content-length:";

static bool startsWithNoCase(const char *s, const char *prefix) {
  for (; *prefix; s++, prefix++) {
    if (tolower((unsigned char)*s) != *prefix) {
      return false;
    }
  }
  return true;
}

bool HttpBody::feed(const uint8_t *data, uint32_t n) {
  uint32_t i = 0;
  while (i < n && state != BODY) {
    if (state == FAILED || !headerByte(data[i])) {
      state = FAILED;
      return false;
    }
    i++;
  }
  if (state == BODY && i < n && !bodyBytes(data + i, n - i)) {
    state = FAILED;
    return false;
  }
  return state != FAILED;
}

bool HttpBody::complete() const {
  return state == BODY && hasLength && bodyBytesSeen == length;
}

bool HttpBody::headerByte(uint8_t c) {
  if (c == '\n') {
    return lineDone();
  }
  if (lineLen == LINE_BYTES - 1) {
    return false;
  }
  line[lineLen++] = (char)c;
  return true;
}

// one status or header line, CR stripped
bool HttpBody::lineDone() {
  if (lineLen && line[lineLen - 1] == '\r') {
    lineLen--;
  }
  line[lineLen] = 0;
  lineLen = 0;

  if (state == STATUS) {
    // "HTTP/1.x 200 OK"
    const uint32_t prefix = sizeof(STATUS_PREFIX) - 1;
    if (strncmp(line, STATUS_PREFIX, prefix) || strlen(line) < prefix + 5 || line[prefix + 1] != ' ') {
      return false;
    }
    statusCode = atoi(line + prefix + 2);
    if (statusCode != HTTP_OK) {
      return false;
    }
    state = HEADERS;
    return true;
  }

  if (!line[0]) {
    state = BODY;
    return true;
  }
  if (startsWithNoCase(line, LENGTH_HEADER)) {
    length = strtoul(line + sizeof(LENGTH_HEADER) - 1, nullptr, 10);
    hasLength = true;
  }
  return true;
}

bool HttpBody::bodyBytes(const uint8_t *data, uint32_t n) {
  if (hasLength && n > length - bodyBytesSeen) {
    return false;
  }
  bodyBytesSeen += n;
  return sink(ctx, data, n);
}
