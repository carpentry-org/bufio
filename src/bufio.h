#ifndef CARP_BUFIO_H
#define CARP_BUFIO_H

#include <string.h>

#define BUFIO_DEFAULT_CAP 8192

/* Function pointer types for stream operations */
typedef int (*bufio_read_fn)(void* inner, char* buf, int len);
typedef int (*bufio_write_fn)(void* inner, const char* buf, int len);
typedef void (*bufio_close_fn)(void* inner);

typedef struct {
  void* inner;
  bufio_read_fn read_fn;
  bufio_write_fn write_fn;
  bufio_close_fn close_fn;
  char* rbuf;
  int rbuf_len;
  int rbuf_pos;
  int rbuf_cap;
  char* wbuf;
  int wbuf_len;
  int wbuf_cap;
} BufReader;

/* --- Construction / destruction --- */

BufReader BufReader_create_(void* inner, bufio_read_fn rfn,
                            bufio_write_fn wfn, bufio_close_fn cfn) {
  BufReader br;
  br.inner = inner;
  br.read_fn = rfn;
  br.write_fn = wfn;
  br.close_fn = cfn;
  br.rbuf = CARP_MALLOC(BUFIO_DEFAULT_CAP);
  br.rbuf_len = 0;
  br.rbuf_pos = 0;
  br.rbuf_cap = BUFIO_DEFAULT_CAP;
  br.wbuf = CARP_MALLOC(BUFIO_DEFAULT_CAP);
  br.wbuf_len = 0;
  br.wbuf_cap = BUFIO_DEFAULT_CAP;
  return br;
}

void BufReader_delete(BufReader br) {
  if (br.close_fn && br.inner) br.close_fn(br.inner);
  if (br.rbuf) CARP_FREE(br.rbuf);
  if (br.wbuf) CARP_FREE(br.wbuf);
}

/* --- Internal helpers --- */

static int bufreader_fill(BufReader* br) {
  /* compact */
  if (br->rbuf_pos > 0) {
    int remaining = br->rbuf_len - br->rbuf_pos;
    if (remaining > 0) memmove(br->rbuf, br->rbuf + br->rbuf_pos, remaining);
    br->rbuf_len = remaining;
    br->rbuf_pos = 0;
  }
  /* grow if full */
  if (br->rbuf_len >= br->rbuf_cap) {
    br->rbuf_cap *= 2;
    br->rbuf = CARP_REALLOC(br->rbuf, br->rbuf_cap);
  }
  int space = br->rbuf_cap - br->rbuf_len;
  int n = br->read_fn(br->inner, br->rbuf + br->rbuf_len, space);
  if (n > 0) br->rbuf_len += n;
  return n;
}

static int bufreader_available(BufReader* br) {
  return br->rbuf_len - br->rbuf_pos;
}

/* --- Read operations --- */

String BufReader_read_MINUS_until_(BufReader* br, char delim) {
  while (1) {
    for (int i = br->rbuf_pos; i < br->rbuf_len; i++) {
      if (br->rbuf[i] == delim) {
        int len = i - br->rbuf_pos + 1;
        String s = CARP_MALLOC(len + 1);
        memcpy(s, br->rbuf + br->rbuf_pos, len);
        s[len] = '\0';
        br->rbuf_pos += len;
        return s;
      }
    }
    int r = bufreader_fill(br);
    if (r <= 0) {
      int avail = bufreader_available(br);
      if (avail > 0) {
        String s = CARP_MALLOC(avail + 1);
        memcpy(s, br->rbuf + br->rbuf_pos, avail);
        s[avail] = '\0';
        br->rbuf_pos += avail;
        return s;
      }
      /* Return empty string to signal EOF/error */
      String s = CARP_MALLOC(1);
      s[0] = '\0';
      return s;
    }
  }
}

String BufReader_read_MINUS_line_(BufReader* br) {
  return BufReader_read_MINUS_until_(br, '\n');
}

Array BufReader_read_MINUS_n_(BufReader* br, int n) {
  Array result;
  result.data = CARP_MALLOC(n);
  result.capacity = n;
  result.len = 0;

  while (result.len < n) {
    int avail = bufreader_available(br);
    if (avail > 0) {
      int want = n - result.len;
      int take = avail < want ? avail : want;
      memcpy((char*)result.data + result.len, br->rbuf + br->rbuf_pos, take);
      result.len += take;
      br->rbuf_pos += take;
    } else {
      int r = bufreader_fill(br);
      if (r <= 0) break;
    }
  }
  return result;
}

/* Single unbuffered-style read into caller buffer, but uses internal buffer */
int BufReader_read_MINUS_append_(BufReader* br, Array* buf) {
  /* If we have buffered data, drain that first */
  int avail = bufreader_available(br);
  if (avail > 0) {
    if ((int)(buf->capacity - buf->len) < avail) {
      int new_cap = (buf->len + avail) * 2;
      buf->data = CARP_REALLOC(buf->data, new_cap);
      buf->capacity = new_cap;
    }
    memcpy((char*)buf->data + buf->len, br->rbuf + br->rbuf_pos, avail);
    buf->len += avail;
    br->rbuf_pos += avail;
    return avail;
  }
  /* Buffer empty — read directly into caller's buffer */
  int space = 4096;
  if ((int)(buf->capacity - buf->len) < space) {
    int new_cap = (buf->len + space) * 2;
    buf->data = CARP_REALLOC(buf->data, new_cap);
    buf->capacity = new_cap;
  }
  int n = br->read_fn(br->inner, (char*)buf->data + buf->len, space);
  if (n > 0) buf->len += n;
  return n;
}

/* --- Write operations --- */

int BufReader_write_(BufReader* br, String* data) {
  int len = strlen(*data);
  if (br->wbuf_len + len > br->wbuf_cap) {
    br->wbuf_cap = (br->wbuf_len + len) * 2;
    br->wbuf = CARP_REALLOC(br->wbuf, br->wbuf_cap);
  }
  memcpy(br->wbuf + br->wbuf_len, *data, len);
  br->wbuf_len += len;
  return len;
}

int BufReader_write_MINUS_bytes_(BufReader* br, Array* data) {
  if (br->wbuf_len + (int)data->len > br->wbuf_cap) {
    br->wbuf_cap = (br->wbuf_len + data->len) * 2;
    br->wbuf = CARP_REALLOC(br->wbuf, br->wbuf_cap);
  }
  memcpy(br->wbuf + br->wbuf_len, data->data, data->len);
  br->wbuf_len += data->len;
  return data->len;
}

int BufReader_flush_(BufReader* br) {
  if (br->wbuf_len == 0) return 0;
  int total = 0;
  while (total < br->wbuf_len) {
    int n = br->write_fn(br->inner, br->wbuf + total, br->wbuf_len - total);
    if (n <= 0) return -1;
    total += n;
  }
  br->wbuf_len = 0;
  return 0;
}

void BufReader_clear_MINUS_read(BufReader* br) {
  br->rbuf_pos = 0;
  br->rbuf_len = 0;
}

void BufReader_clear_MINUS_write(BufReader* br) {
  br->wbuf_len = 0;
}

BufReader BufReader_copy(BufReader* br) {
  BufReader c;
  c.inner = br->inner;
  c.read_fn = br->read_fn;
  c.write_fn = br->write_fn;
  c.close_fn = br->close_fn;
  c.rbuf_cap = br->rbuf_cap;
  c.rbuf_len = br->rbuf_len;
  c.rbuf_pos = br->rbuf_pos;
  c.rbuf = CARP_MALLOC(c.rbuf_cap);
  memcpy(c.rbuf, br->rbuf, c.rbuf_len);
  c.wbuf_cap = br->wbuf_cap;
  c.wbuf_len = br->wbuf_len;
  c.wbuf = CARP_MALLOC(c.wbuf_cap);
  memcpy(c.wbuf, br->wbuf, c.wbuf_len);
  return c;
}

#endif
