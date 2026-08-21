#ifndef MOCK_STREAM_H
#define MOCK_STREAM_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
  char* data;
  int data_len;
  int read_pos;
  char* output;
  int output_len;
  int output_cap;
  int closed;
  int chunk_size;
  int write_max;
  int write_budget;
  int write_fail_code;
  int read_budget;
  int read_fail_code;
} MockStream;

static MockStream* g_mock = NULL;

static int mock_stream_read(void* inner, char* buf, int len) {
  MockStream* ms = (MockStream*)inner;
  if (ms->read_budget >= 0) {
    if (ms->read_budget == 0) return ms->read_fail_code;
    if (len > ms->read_budget) len = ms->read_budget;
  }
  int avail = ms->data_len - ms->read_pos;
  if (avail <= 0) return 0;
  int to_read = len < avail ? len : avail;
  if (ms->chunk_size > 0 && to_read > ms->chunk_size)
    to_read = ms->chunk_size;
  memcpy(buf, ms->data + ms->read_pos, to_read);
  ms->read_pos += to_read;
  if (ms->read_budget >= 0) ms->read_budget -= to_read;
  return to_read;
}

static int mock_stream_write(void* inner, const char* buf, int len) {
  MockStream* ms = (MockStream*)inner;
  if (ms->write_max > 0 && len > ms->write_max) len = ms->write_max;
  if (ms->write_budget >= 0) {
    if (ms->write_budget == 0) return ms->write_fail_code;
    if (len > ms->write_budget) len = ms->write_budget;
    ms->write_budget -= len;
  }
  if (ms->output_len + len > ms->output_cap) {
    ms->output_cap = (ms->output_len + len) * 2;
    ms->output = (char*)realloc(ms->output, ms->output_cap);
  }
  memcpy(ms->output + ms->output_len, buf, len);
  ms->output_len += len;
  return len;
}

static void mock_stream_close(void* inner) {
  MockStream* ms = (MockStream*)inner;
  ms->closed = 1;
}

static BufReader mock_bufreader_create(String* data, int chunk_size) {
  MockStream* ms = (MockStream*)CARP_MALLOC(sizeof(MockStream));
  ms->data_len = (int)strlen(*data);
  ms->data = (char*)CARP_MALLOC(ms->data_len + 1);
  memcpy(ms->data, *data, ms->data_len + 1);
  ms->read_pos = 0;
  ms->output = (char*)CARP_MALLOC(1024);
  ms->output_len = 0;
  ms->output_cap = 1024;
  ms->closed = 0;
  ms->chunk_size = chunk_size;
  ms->write_max = 0;
  ms->write_budget = -1;
  ms->write_fail_code = -1;
  ms->read_budget = -1;
  ms->read_fail_code = -1;
  g_mock = ms;
  return BufReader_create_(
    (void*)ms, mock_stream_read, mock_stream_write, mock_stream_close);
}

/* max_per_write 0 = unlimited; budget < 0 = unlimited, else bytes accepted
   before every further write returns fail_code */
static void mock_set_write_limits(int max_per_write, int budget,
                                  int fail_code) {
  if (!g_mock) return;
  g_mock->write_max = max_per_write;
  g_mock->write_budget = budget;
  g_mock->write_fail_code = fail_code;
}

/* budget < 0 = unlimited, else bytes handed out before every further read
   returns fail_code */
static void mock_set_read_limits(int budget, int fail_code) {
  if (!g_mock) return;
  g_mock->read_budget = budget;
  g_mock->read_fail_code = fail_code;
}

static int mock_buffered_write_len(BufReader* br) { return br->wbuf_len; }

/* Long-typed view of bufio_next_cap, which takes and returns size_t; -1 for an
   argument that not every size_t can represent. */
static int64_t bufio_next_cap_long(int64_t have, int64_t need) {
  if (have < 0 || need < 0 || have > 0xFFFFFFFFLL || need > 0xFFFFFFFFLL)
    return -1;
  return (int64_t)bufio_next_cap((size_t)have, (size_t)need);
}

static String mock_get_output(void) {
  if (!g_mock || g_mock->output_len == 0) {
    String s = CARP_MALLOC(1);
    s[0] = '\0';
    return s;
  }
  String s = CARP_MALLOC(g_mock->output_len + 1);
  memcpy(s, g_mock->output, g_mock->output_len);
  s[g_mock->output_len] = '\0';
  return s;
}

static bool mock_is_closed(void) {
  return g_mock ? g_mock->closed : 0;
}

static void mock_cleanup(void) {
  if (g_mock) {
    CARP_FREE(g_mock->data);
    CARP_FREE(g_mock->output);
    CARP_FREE(g_mock);
    g_mock = NULL;
  }
}

static String mock_bytes_to_string(Array* arr) {
  String s = CARP_MALLOC(arr->len + 1);
  if (arr->len > 0) memcpy(s, arr->data, arr->len);
  s[arr->len] = '\0';
  return s;
}

#endif
