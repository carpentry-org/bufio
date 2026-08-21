#ifndef CARP_BUFIO_H
#define CARP_BUFIO_H

#include <limits.h>
#include <string.h>

#define BUFIO_DEFAULT_CAP 8192
#define BUFIO_MAX_CAP ((size_t)INT_MAX)

/* Values written to the `status` out-parameter of the read operations. */
#define BUFIO_OK 0
#define BUFIO_EOF 1
#define BUFIO_ERR (-1)

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

/* Capacity holding `need` bytes, doubling `have` first; 0 if `need` is
   past BUFIO_MAX_CAP. */
static size_t bufio_next_cap(size_t have, size_t need) {
  if (need > BUFIO_MAX_CAP) return 0;
  if (need <= have) return have;
  size_t next = have > BUFIO_MAX_CAP / 2 ? BUFIO_MAX_CAP : have * 2;
  return next < need ? need : next;
}

/* Make room for `extra` bytes past `used`. Returns 0, or -1 if the target is
   past BUFIO_MAX_CAP or the allocation fails; *buf is untouched on failure. */
static int bufio_reserve(char** buf, int* cap, size_t used, size_t extra) {
  if (extra > BUFIO_MAX_CAP - used) return -1;
  size_t next = bufio_next_cap((size_t)*cap, used + extra);
  if (next <= (size_t)*cap) return 0;
  char* grown = CARP_REALLOC(*buf, next);
  if (!grown) return -1;
  *buf = grown;
  *cap = (int)next;
  return 0;
}

/* Same for a Carp Array, whose length and capacity are size_t. */
static int bufio_reserve_array(Array* buf, size_t extra) {
  if (buf->len > BUFIO_MAX_CAP || extra > BUFIO_MAX_CAP - buf->len) return -1;
  size_t next = bufio_next_cap(buf->capacity, buf->len + extra);
  if (next <= buf->capacity) return 0;
  void* grown = CARP_REALLOC(buf->data, next);
  if (!grown) return -1;
  buf->data = grown;
  buf->capacity = next;
  return 0;
}

static String bufio_empty_string(void) {
  String s = CARP_MALLOC(1);
  s[0] = '\0';
  return s;
}

/* Returns the byte count, 0 at end of stream, or negative on failure; nothing
   already buffered is consumed. */
static int bufreader_fill(BufReader* br) {
  /* compact */
  if (br->rbuf_pos > 0) {
    int remaining = br->rbuf_len - br->rbuf_pos;
    if (remaining > 0) memmove(br->rbuf, br->rbuf + br->rbuf_pos, remaining);
    br->rbuf_len = remaining;
    br->rbuf_pos = 0;
  }
  if (bufio_reserve(&br->rbuf, &br->rbuf_cap, (size_t)br->rbuf_len, 1) != 0)
    return -1;
  int space = br->rbuf_cap - br->rbuf_len;
  int n = br->read_fn(br->inner, br->rbuf + br->rbuf_len, space);
  if (n > 0) br->rbuf_len += n;
  return n;
}

static int bufreader_available(BufReader* br) {
  return br->rbuf_len - br->rbuf_pos;
}

/* --- Read operations --- */

String BufReader_read_MINUS_until_(BufReader* br, char delim, int* status) {
  while (1) {
    for (int i = br->rbuf_pos; i < br->rbuf_len; i++) {
      if (br->rbuf[i] == delim) {
        int len = i - br->rbuf_pos + 1;
        String s = CARP_MALLOC(len + 1);
        memcpy(s, br->rbuf + br->rbuf_pos, len);
        s[len] = '\0';
        br->rbuf_pos += len;
        *status = BUFIO_OK;
        return s;
      }
    }
    int r = bufreader_fill(br);
    if (r < 0) {
      *status = BUFIO_ERR;
      return bufio_empty_string();
    }
    if (r == 0) {
      int avail = bufreader_available(br);
      *status = BUFIO_EOF;
      if (avail > 0) {
        String s = CARP_MALLOC(avail + 1);
        memcpy(s, br->rbuf + br->rbuf_pos, avail);
        s[avail] = '\0';
        br->rbuf_pos += avail;
        return s;
      }
      return bufio_empty_string();
    }
  }
}

String BufReader_read_MINUS_line_(BufReader* br, int* status) {
  return BufReader_read_MINUS_until_(br, '\n', status);
}

Array BufReader_read_MINUS_n_(BufReader* br, int n, int* status) {
  Array result;
  result.data = CARP_MALLOC(n);
  result.capacity = n;
  result.len = 0;
  *status = BUFIO_OK;

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
      if (r <= 0) {
        *status = r < 0 ? BUFIO_ERR : BUFIO_EOF;
        break;
      }
    }
  }
  return result;
}

/* Single unbuffered-style read into caller buffer, but uses internal buffer */
int BufReader_read_MINUS_append_(BufReader* br, Array* buf) {
  /* If we have buffered data, drain that first */
  int avail = bufreader_available(br);
  if (avail > 0) {
    if (bufio_reserve_array(buf, (size_t)avail) != 0) return -1;
    memcpy((char*)buf->data + buf->len, br->rbuf + br->rbuf_pos, avail);
    buf->len += avail;
    br->rbuf_pos += avail;
    return avail;
  }
  /* Buffer empty — read directly into caller's buffer */
  int space = 4096;
  if (bufio_reserve_array(buf, (size_t)space) != 0) return -1;
  int n = br->read_fn(br->inner, (char*)buf->data + buf->len, space);
  if (n > 0) buf->len += n;
  return n;
}

/* --- Write operations --- */

int BufReader_write_(BufReader* br, String* data) {
  size_t len = strlen(*data);
  if (bufio_reserve(&br->wbuf, &br->wbuf_cap, (size_t)br->wbuf_len, len) != 0)
    return -1;
  memcpy(br->wbuf + br->wbuf_len, *data, len);
  br->wbuf_len += (int)len;
  return (int)len;
}

int BufReader_write_MINUS_bytes_(BufReader* br, Array* data) {
  size_t len = data->len;
  if (bufio_reserve(&br->wbuf, &br->wbuf_cap, (size_t)br->wbuf_len, len) != 0)
    return -1;
  memcpy(br->wbuf + br->wbuf_len, data->data, len);
  br->wbuf_len += (int)len;
  return (int)len;
}

int BufReader_flush_(BufReader* br) {
  if (br->wbuf_len == 0) return 0;
  int total = 0;
  while (total < br->wbuf_len) {
    int n = br->write_fn(br->inner, br->wbuf + total, br->wbuf_len - total);
    if (n <= 0) break;
    total += n;
  }
  int remaining = total < br->wbuf_len ? br->wbuf_len - total : 0;
  if (total > 0 && remaining > 0) memmove(br->wbuf, br->wbuf + total, remaining);
  br->wbuf_len = remaining;
  return remaining == 0 ? 0 : -1;
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
