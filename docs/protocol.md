# CDBA Client/Server Protocol (Current v1)

This document specifies the wire protocol between `cdba` (client) and
`cdba-server` (server), as implemented in this repository.

It is intended to support:
- compatible third-party implementations
- protocol review and future evolution

## 1. Transport

- The protocol is a binary message stream.
- In the reference implementation, it runs over the server process
  `stdin`/`stdout` pair.
- When `-h <host>` is used, the stream is tunneled through `ssh`.
- Server `stderr` is out-of-band logs and not part of the protocol.

## 2. Frame Format

Each frame is:
- `type`: `uint8`
- `len`: `uint16`
- `payload`: `len` bytes

The C struct is:

```c
struct msg {
	uint8_t type;
	uint16_t len;
	uint8_t data[];
} __attribute__((packed));
```

Important notes:
- Header size is exactly 3 bytes.
- `len` is encoded in host byte order in the current implementation
  (no `htons`/`ntohs` conversion).
- In practice deployments are little-endian; a big-endian peer is not
  guaranteed interoperable.
- Maximum payload per frame is `65535` bytes.
- This is a stream protocol; receivers must handle partial reads.

## 3. Common Payload Types

- C string: UTF-8/byte string including trailing `NUL` (`strlen + 1`).
- Boolean: one byte (`0` or non-zero).
- Board names and EDL flash targets are expected as C strings.
- `key_press`:

```c
struct key_press {
	uint8_t key;   // DEVICE_KEY_*
	uint8_t state; // KEY_PRESS_*
} __attribute__((packed));
```

`DEVICE_KEY_*` values:
- `0`: fastboot key
- `1`: power key
- `2`: edl key

`KEY_PRESS_*` values:
- `0`: release
- `1`: press
- `2`: pulse (press then auto-release after 100 ms on server)

## 4. Message Registry

Direction:
- `C->S`: client to server
- `S->C`: server to client
- `Both`: both directions are valid

| ID | Name | Direction | Payload | Semantics |
|---:|---|---|---|---|
| 1 | `MSG_SELECT_BOARD` | Both | C->S: board C string. S->C: empty. | Select/open board on server. Server sends empty response frame as completion signal. |
| 2 | `MSG_CONSOLE` | Both | Raw bytes | Console stream data. |
| 3 | `MSG_HARDRESET` | Both | Empty | Currently ignored/no-op in both implementations. |
| 4 | `MSG_POWER_ON` | Both | C->S: optional 1-byte `power_on_mode`; S->C: empty | Power on selected board. If missing/invalid length, mode defaults to normal on server. |
| 5 | `MSG_POWER_OFF` | Both | Empty | Power off selected board; server replies with empty ack. |
| 6 | `MSG_FASTBOOT_PRESENT` | S->C | 1-byte bool | Fastboot USB presence notification (`1` present, `0` disconnected). |
| 7 | `MSG_FASTBOOT_DOWNLOAD` | Both | C->S: chunked binary data, zero-length frame terminates transfer. S->C: empty | Send boot image contents. Server replies empty when boot/download sequence completes. |
| 8 | `MSG_FASTBOOT_BOOT` | Both | Empty | Currently ignored/no-op in both implementations. |
| 9 | `MSG_STATUS_UPDATE` | Both | C->S: empty. S->C: raw status bytes (typically JSON lines). | Enable status source on server; server then streams updates. |
| 10 | `MSG_VBUS_ON` | C->S | Empty | Enable USB/VBUS path on selected board. |
| 11 | `MSG_VBUS_OFF` | C->S | Empty | Disable USB/VBUS path on selected board. |
| 12 | `MSG_FASTBOOT_REBOOT` | (reserved) | N/A | Declared but currently unsupported; receiving side treats it as unknown/fatal. |
| 13 | `MSG_SEND_BREAK` | C->S | Empty | Send console break on selected board. |
| 14 | `MSG_LIST_DEVICES` | Both | C->S: empty. S->C: one text item per frame, then empty frame terminator. | Enumerate accessible boards. Item format is server-defined printable text (no required `NUL`). |
| 15 | `MSG_BOARD_INFO` | Both | C->S: board C string. S->C: board description bytes or empty. | Query board description. |
| 16 | `MSG_FASTBOOT_CONTINUE` | Both | Empty | Request `fastboot continue`; server replies empty on completion call. |
| 17 | `MSG_KEY_PRESS` | C->S | `struct key_press` (2 bytes) | Assert/release/pulse logical board keys. Invalid payload length is ignored. |
| 18 | `MSG_EDL_PRESENT` | S->C | 1-byte bool | EDL USB presence notification (`1` present, `0` absent). |
| 19 | `MSG_EDL_DOWNLOAD` | C->S | Chunked binary data, zero-length frame terminates one file | Upload one EDL flash artifact into server temp storage. |
| 20 | `MSG_EDL_FLASH` | C->S | Target C string | Associate most recently uploaded EDL file with a flash target. |
| 21 | `MSG_EDL_RESET` | C->S | Empty | Execute `qdl` flash/reset sequence for queued EDL files. |

`power_on_mode` values:
- `0`: normal
- `1`: fastboot
- `2`: edl

## 5. Protocol Flows

### 5.1 Boot (Fastboot image)

1. `C->S MSG_SELECT_BOARD(board)`
2. `S->C MSG_SELECT_BOARD()`
3. `C->S MSG_POWER_ON(mode=FASTBOOT)` (or other mode)
4. `S->C MSG_POWER_ON()`
5. `S->C MSG_FASTBOOT_PRESENT(1)` when fastboot is detected
6. `C->S MSG_FASTBOOT_DOWNLOAD(chunk...)`
7. `C->S MSG_FASTBOOT_DOWNLOAD(len=0)` transfer terminator
8. `S->C MSG_FASTBOOT_DOWNLOAD()` completion ack

Console (`MSG_CONSOLE`) may be exchanged asynchronously throughout.

### 5.2 Boot (fastboot continue)

Same setup through fastboot presence, then:
1. `C->S MSG_FASTBOOT_CONTINUE()`
2. `S->C MSG_FASTBOOT_CONTINUE()`

### 5.3 EDL flash flow

1. Client requests power-on in EDL mode.
2. On `S->C MSG_EDL_PRESENT(1)`, client sends for each file:
   - `MSG_EDL_DOWNLOAD(chunk...)`
   - `MSG_EDL_DOWNLOAD(len=0)` terminator for that file
   - `MSG_EDL_FLASH(target)`
3. Client sends `MSG_EDL_RESET()` to execute flash/reset.

### 5.4 List devices

1. `C->S MSG_LIST_DEVICES()`
2. Server sends one or more `S->C MSG_LIST_DEVICES(item)`
3. Server sends `S->C MSG_LIST_DEVICES(len=0)` terminator

### 5.5 Board info

1. `C->S MSG_BOARD_INFO(board)`
2. `S->C MSG_BOARD_INFO(description-or-empty)`

### 5.6 Status stream

1. `C->S MSG_STATUS_UPDATE()` to enable status source
2. Server emits `S->C MSG_STATUS_UPDATE(payload)` as data arrives

Status payload is opaque bytes at protocol level.

Current in-tree producers emit JSON lines like:

```json
{"ts":12.345, "battery":{"mv":8023, "ma":725}}
```

Common units currently emitted by helpers are `"mv"`, `"ma"`, and `"gpio"`.

## 6. Error Handling and Compatibility Characteristics

Current behavior to match for wire compatibility:
- Unknown message type is fatal on both sides.
- No explicit protocol version negotiation exists.
- No request IDs/correlation IDs; ordering and context are implicit.
- No integrity checks (CRC/MAC) at protocol layer.
- Large transfers are sender-chunked; completion uses zero-length sentinel.
- No structured error replies exist; failures are typically emitted on `stderr`
  and/or by connection teardown.
- Most board-control commands assume a successful prior `MSG_SELECT_BOARD`.

Implication:
- New message IDs cannot be sent to old peers safely without a separate
  capability-negotiation mechanism.

## 7. Guidance for Third-Party Implementers

- Parse stream incrementally; never assume one read equals one frame.
- Treat `len` as 16-bit and validate buffer bounds.
- Preserve zero-length sentinel semantics for:
  - `MSG_FASTBOOT_DOWNLOAD`
  - `MSG_EDL_DOWNLOAD`
  - `MSG_LIST_DEVICES` response terminator
- Treat `MSG_STATUS_UPDATE` payload as opaque bytes unless you explicitly
  opt into JSON-line parsing.
- Handle asynchronous server events (`CONSOLE`, `FASTBOOT_PRESENT`,
  `EDL_PRESENT`, `STATUS_UPDATE`) at any time.

## 8. Expansion Notes (Design, Not Yet Implemented)

To evolve this protocol safely, introduce an explicit capability handshake
before using new message IDs or changed semantics.

Recommended direction:
- Add a new negotiated protocol generation (v2) with:
  - fixed byte order (`little-endian` or `network-order`, explicitly defined)
  - version/capability exchange at session start
  - optional request correlation field for command/reply matching
- Keep v1 behavior as a compatibility mode.
- Reserve message ID ranges and maintain this registry in-repo.
