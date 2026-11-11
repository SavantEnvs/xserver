# Framing: a BIG-REQUESTS length above MAXINT >> 2 makes the request reader answer BadLength for ever

`input.bin` = flags 0x03 (byte-swapped client, BIG-REQUESTS enabled), then a request header with length 0 and
an extended length of 0x90520000 words. `ReadRequestFromClient` (`os/io.c`, `if (needed > MAXINT >> 2)`)
returns `-(BadLength)` without consuming anything, so `Dispatch()` (`dix/dispatch.c`: `read_result < 0` ->
`BadLength` -> `SendErrorToClient` -> `break`) asks the same client again, gets the same answer, and sends
another error each time: a CPU spin plus an unbounded output queue, started by any client that enabled
BIG-REQUESTS. The harness stops reading the stream at the first `-BadLength` so one input does not hang.
Fix: close the client (`return -1`) or consume the request, as the `needed > maxBigRequestSize << 2` branch
does with `ignoreBytes`.
