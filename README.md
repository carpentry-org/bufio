# bufio

Generic buffered I/O for Carp.

## Installation

```clojure
(load "git@github.com:carpentry-org/bufio@0.1.0")
```

## Usage

`BufReader` wraps any stream that implements the `stream-read` and
`stream-write` interfaces, providing buffered line-oriented and
size-delimited reading, and buffered writing.

### With TCP sockets

The [socket](https://github.com/carpentry-org/socket) library implements the
interfaces for `TcpStream`:

```clojure
(match (TcpStream.connect "example.com" 80)
  (Result.Success s)
    (let [br (TcpStream.buffered s)]
      (do
        (BufReader.write &br "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")
        (ignore (BufReader.flush &br))
        (match (BufReader.read-line &br)
          (Result.Success line) (println* &line)
          (Result.Error e) (IO.errorln &e))
        (BufReader.delete br)))
  (Result.Error e) (IO.errorln &e))
```

### Implementing for your own types

Define three interface functions for your stream type:

```clojure
; stream-read: read up to `len` bytes into buf, return bytes read
(definterface stream-read (Fn [&a &(Array Byte) Int] Int))

; stream-write: write bytes, return bytes written
(definterface stream-write (Fn [&a &(Array Byte)] Int))

; stream-close: close the stream
(definterface stream-close (Fn [a] ()))
```

Then provide a `buffered` function that creates a `BufReader` via the C-level
`BufReader_create_` with your type's adapter function pointers.

## API

- `BufReader.read-line` — read until `\n`
- `BufReader.read-until` — read until a delimiter byte
- `BufReader.read-n` — read exactly N bytes
- `BufReader.read-append` — append read data to a byte buffer
- `BufReader.write` — buffer string data
- `BufReader.write-bytes` — buffer binary data
- `BufReader.flush` — send buffered writes
- `BufReader.clear-read` / `BufReader.clear-write` — discard buffers
- `BufReader.delete` — close underlying stream and free buffers

## Testing

```
carp -x test/bufio.carp
```

<hr/>

Have fun!
