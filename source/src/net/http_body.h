#ifndef HTTP_BODY_H
#define HTTP_BODY_H

#include <cstdint>

// HTTP/1.0 response parser for bundle downloads: checks status 200, reads
// Content-Length, hands body bytes to a sink as recv() delivers them.
class HttpBody {
public:
  typedef bool (*Sink)(void *ctx, const uint8_t *data, uint32_t length);

  HttpBody(Sink sink, void *ctx) : sink(sink), ctx(ctx) { }

  // False: malformed status or headers, header line > LINE_BYTES, status
  // != 200, body longer than Content-Length, or the sink refused.
  bool feed(const uint8_t *data, uint32_t length);

  // Headers done, Content-Length present and that many body bytes seen.
  bool complete() const;
  uint32_t contentLength() const { return length; }

private:
  enum State { STATUS, HEADERS, BODY, FAILED };
  static constexpr uint32_t LINE_BYTES = 512;

  bool headerByte(uint8_t c);
  bool lineDone();
  bool bodyBytes(const uint8_t *data, uint32_t n);

  Sink sink;
  void *ctx;
  State state = STATUS;
  char line[LINE_BYTES];
  uint32_t lineLen = 0;
  bool hasLength = false;
  uint32_t length = 0;
  uint32_t received = 0;
};

#endif
