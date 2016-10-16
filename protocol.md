
# The Simple Solar Mining Protocol (SSMP)
This protocol is provisional and will, no doubt, evolve during prototyping and review.

## Messages
All messages have the same format. Each message has a 20-byte header, and an optional payload of up to 1452 bytes.

```c
+----------+------------+---------+---------+---------+------------------+
| MAGIC(4) | VERSION(4) | SYNC(4) | TYPE(4) | SIZE(4) | PAYLOAD(0..1452) |
+----------+------------+---------+---------+---------+------------------+
```

`MAGIC` is always the c_string "SMM", i.e.: ['S', 'M', 'M', 0]

`VERSION` is a [semver](https://www.semver.org) version number in 4 bytes, e.g.: [1, 0, 0, 'A']

`SYNC` is a 32 bit message number/identifier that may be used to tie together request/response pairs.

`TYPE` is the message type or "command". 4 bytes of ASCII characters for readability. There are 12 message types: `PING`, `HELO`, `SYNC`, `NODE`, `POOL`, `WAIT`, `MINE`, `LAST`, `DONE`, `STAT`, `INFO`, `NACK`

`SIZE` is a 32 bit little-endian number holding the size of the subsequent payload in bytes.

`PAYLOAD` is always encrypted using XXTEA with PKCS#7 padding. The plaintext content depends on the message type. There is a payload limit of 1452 bytes is chosen to reduce packet fragmentation to a minimum. i.e. MTUs will normally not fragment a packet of 1500 bytes, i.e.: 1452 bytes = 1500 bytes - (20 byte IP header + 8 UDP header + 20 byte SMM header).

## Handshake
```c
SMM                                  POOL
 |                                    |
 |<------------- PING[] --------------|?
 |                                    |
 |-------------- HELO[] ------------->|
 |                                    |
 |<------------- SYNC[] --------------|
 |                                    |
 |----------- NODE[ADDRESS] --------->|
 |                                    |
 |<---------- POOL[TEMPLATE] ---------|
 |                                    |
 |-------------- WAIT[] ------------->|?
 |                                    |
```

### Payloads
```c
MINER ADDRESS:
+-----------------+
| ADDRESS(32..40) |
+-----------------+

COINBASE TEMPLATE:
+-----------+----------------+-----------+
| HEIGHT(4) | SCRIPT(1..120) | NONCE2(4) |
+-----------+----------------+-----------+
```

## Operation

```c
SMM                                    POOL
 |                                      |
 |<--------- MINE[INSTRUCTION] ---------|
 |                                      |
 |------------ LAST[RESULT] ----------->|
 |                                      |
 |------------ DONE[RESULT] ----------->|?
 |                                      |
 |<-------------- WAIT[] ---------------|?
 |                                      |
```

### Payloads
```c
MINING INSTRUCTION:
+-----------+------------+----------+---------+---------+-----------+----------------+
| HEIGHT(4) | VERSION(4) | PREV(32) | TIME(4) | BITS(4) | LENGTH(1) | PATH(32)[1..N] |
+-----------+------------+----------+---------+---------+-----------+----------------+

MINING RESULT:
+-----------+---------+----------+-----------+
| HEIGHT(4) | BITS(4) | NONCE(4) | NONCE2(4) |
+-----------+---------+----------+-----------+
```

## Status


Bit|Mnemonic|Description
---|--------|-----------
0x01|PAUSED|Module is in a paused state
0x02|TETHERED|Module is tethered to another computer
0x04|VALID|Module is in a valid mining state
0x08|READY|Module is properly configured
0x10|BTC|Module is expecting to mine bitcoin
0x20|RESERVED|Leave this flag unset
0x40|HARDWARE|Module is using a hardware hash module
0x80|SOLAR|Module is powered by the Sun


```c
SMM                                   POOL
 |                                     |
 |<------------- STAT[] ---------------|
 |                                     |
 |------------ INFO[REPORT] ---------->|
 |                                     |
```

```c
MODULE REPORT:
+----------+-----------+-----------+----------+-----------+----------+-------------+-------------+
| FLAGS(1) | STATUS(1) | HEIGHT(4) | NONCE(4) | NONCE2(4) | HASH(32) | HASHTIME(8) | HASHRATE(8) |
+----------+-----------+-----------+----------+-----------+----------+-------------+-------------+
```

## Error

```c
NACK ERROR:
+---------+---------+---------+
| TYPE(4) | SYNC(4) | CODE(8) |
+---------+---------+---------+
```

```c
SMM                                   POOL
 |                                     |
 |----------- NACK[ERROR?] ----------->|
 |                                     |

SMM                                   POOL
 |                                     |
 |<---------- NACK[ERROR?] ------------|
 |                                     |
```
