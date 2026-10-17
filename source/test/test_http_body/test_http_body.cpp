#include <cstring>
#include <string>
#include <unity.h>

// HTTP/1.0 response parser (src/net/http_body.cpp): status, headers, body
// bytes to a sink, however recv() splits the stream.
#include "../../src/net/http_body.cpp"

struct Collected {
  std::string body;
  uint32_t calls = 0;
  bool refuse = false;
};

static bool collect(void *ctx, const uint8_t *data, uint32_t length) {
  Collected *c = (Collected *)ctx;
  c->calls++;
  if (c->refuse) {
    return false;
  }
  c->body.append((const char *)data, length);
  return true;
}

static bool feedString(HttpBody &h, const std::string &s) {
  return h.feed((const uint8_t *)s.data(), s.size());
}

static const std::string OK_RESPONSE =
  "HTTP/1.0 200 OK\r\nServer: x\r\ncontent-length: 10\r\n\r\n0123456789";

void setUp(void) {}
void tearDown(void) {}

static void test_body_split_everywhere(void) {
  for (size_t cut = 0; cut <= OK_RESPONSE.size(); cut++) {
    Collected c;
    HttpBody h(collect, &c);
    TEST_ASSERT_TRUE(feedString(h, OK_RESPONSE.substr(0, cut)));
    TEST_ASSERT_TRUE(feedString(h, OK_RESPONSE.substr(cut)));
    TEST_ASSERT_EQUAL_STRING("0123456789", c.body.c_str());
    TEST_ASSERT_TRUE(h.complete());
    TEST_ASSERT_EQUAL_UINT32(10, h.contentLength());
  }
}

static void test_byte_by_byte(void) {
  Collected c;
  HttpBody h(collect, &c);
  for (char ch : OK_RESPONSE) {
    TEST_ASSERT_TRUE(h.feed((const uint8_t *)&ch, 1));
  }
  TEST_ASSERT_EQUAL_STRING("0123456789", c.body.c_str());
  TEST_ASSERT_TRUE(h.complete());
}

static void test_404_rejected(void) {
  Collected c;
  HttpBody h(collect, &c);
  TEST_ASSERT_FALSE(feedString(h, "HTTP/1.0 404 File not found\r\nContent-Length: 3\r\n\r\nabc"));
  TEST_ASSERT_EQUAL_UINT32(0, c.calls);
  TEST_ASSERT_FALSE(h.complete());
}

static void test_short_body_incomplete(void) {
  Collected c;
  HttpBody h(collect, &c);
  TEST_ASSERT_TRUE(feedString(h, "HTTP/1.0 200 OK\r\nContent-Length: 10\r\n\r\n012345"));
  TEST_ASSERT_FALSE(h.complete());
}

static void test_missing_length_incomplete(void) {
  Collected c;
  HttpBody h(collect, &c);
  TEST_ASSERT_TRUE(feedString(h, "HTTP/1.0 200 OK\r\n\r\n0123456789"));
  TEST_ASSERT_FALSE(h.complete());
}

static void test_overlong_header_rejected(void) {
  Collected c;
  HttpBody h(collect, &c);
  const std::string longLine = "HTTP/1.0 200 OK\r\nX: " + std::string(600, 'a') + "\r\n\r\n";
  TEST_ASSERT_FALSE(feedString(h, longLine));
}

static void test_sink_refusal_propagates(void) {
  Collected c;
  c.refuse = true;
  HttpBody h(collect, &c);
  TEST_ASSERT_FALSE(feedString(h, OK_RESPONSE));
}

static void test_body_beyond_length_rejected(void) {
  Collected c;
  HttpBody h(collect, &c);
  TEST_ASSERT_FALSE(feedString(h, OK_RESPONSE + "X"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_body_split_everywhere);
  RUN_TEST(test_byte_by_byte);
  RUN_TEST(test_404_rejected);
  RUN_TEST(test_short_body_incomplete);
  RUN_TEST(test_missing_length_incomplete);
  RUN_TEST(test_overlong_header_rejected);
  RUN_TEST(test_sink_refusal_propagates);
  RUN_TEST(test_body_beyond_length_rejected);
  return UNITY_END();
}
