# EDID: the extension count in byte 126 is trusted

`input.bin` is 243 bytes but byte 126 announces 18 extension blocks (2432 bytes needed). The parser and
its block iterators index the extension blocks by that count, so they read past the end of the buffer.
Impact: out-of-bounds read from a monitor-supplied EDID. Fix: reject `size < 128 * (1 + data[126])`
(the fuzz harness does, returning -1, so the deeper bugs below stay reachable).
