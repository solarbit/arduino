
# Solar Mining Protocol - First shot

## Messages
All messages have the same format. Each message has a 20-byte header, and an optional payload of up to 1452 bytes.

```c
+----------+------------+---------+---------+---------+------------------+
| MAGIC(4) | VERSION(4) | SYNC(4) | TYPE(4) | SIZE(4) | PAYLOAD(0..1452) |
+----------+------------+---------+---------+---------+------------------+
```

`MAGIC` is always the c_string "SMM", i.e.: ['S', 'M', 'M', 0]

`VERSION` is a [semver](https://www.semver.org) version number in 4 bytes, e.g.: [1, 0, 0, 'A']

`SYNC` is a 32 bit message number/identifier that is used to tie together request/response pairs.

`TYPE` is the message type or "command". 4 bytes of ASCII characters for readability. There are 10 message types: `HELO`, `NODE`, `POOL`, `MINE`, `DONE`, `WAIT`, `STAT`, `INFO`, `OKAY`, `WARN`

`SIZE` is a 32 bit little-endian number holding the size of the subsequent payload in bytes.

`PAYLOAD` content depends on the message type. The payload limit of 1452 bytes is chosen to reduce packet fragmentation to a minimum. i.e. MTUs will normally not fragment a packet of 1500 bytes, i.e.: 1452 bytes = 1500 bytes - (20 byte IP header + 8 UDP header + 20 byte SMM header).

## Handshake
```c
SMM                                    POOL
 |                                      |
 |--------------- HELO[] -------------->|
 |                                      |
 |<-------------- HELO[] ---------------|
 |                                      |
 |---------- NODE[ADDRESS(40)] -------->|
 |                                      |
 |<--- POOL[COINBASE_TEMPLATE(256)] ----|
 |                                      |
 |--------------- WAIT[] -------------->|
 |                                      |
```

## Operation
```c
<= MINE [BLOCK_HEIGHT(4), VERSION(4), PREV(32), TIME(4), BITS(4), PATH(32) * (1..N)]
<= STOP []
=> INFO [BLOCK_HEIGHT(4), NONCE(4), NONCE2(8)] // Previous attempt
=> DONE [BLOCK_HEIGHT(4), NONCE(4), NONCE2(8)]
<= OKAY []
```

## Status
```c
<= STAT []
=> INFO [BLOCK_HEIGHT(4), NONCE(4), NONCE2(8)] // Current attempt
=> DONE [BLOCK_HEIGHT(4), NONCE(4), NONCE2(8)]
=> WAIT []

=> WARN [STRING]
```
