# USB Debug Protocol

TRobot enumerates WinUSB interface 2 while retaining CDC ACM on interfaces 0 and 1. The
Debug services are enabled only in debug mode. To enter debug mode, hold KEY low from
startup for 100 ms; normal application tasks are then skipped until the next reset. In
normal mode the WinUSB interface remains enumerated, but no debug callback is registered
and host requests receive no response.

The development USB identity is `0483:5741`, with WinUSB bulk OUT endpoint `0x05` and
bulk IN endpoint `0x84`.

PID `5740` is not used for the composite firmware because ST's Windows VCP INF claims
that exact hardware ID as a single CDC device before the composite parent driver loads.

Create the host environment and start the TUI with:

```powershell
cd tools
uv sync
uv run python trobot.py
```

The scriptable CLI remains available:

```powershell
uv run python trobot.py info
uv run python trobot.py log dump --format text
uv run python trobot.py can watch --buses 0x7
uv run python trobot.py imu watch
uv run python trobot.py imu calibrate
```

## Frame Format

Every request and response starts with a 24-byte little-endian header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `TDBG` |
| 4 | 1 | Version, currently 1 |
| 5 | 1 | Service; log 1, debug session 2, CAN 3, IMU 4 |
| 6 | 1 | Command |
| 7 | 1 | Flags; currently 0 |
| 8 | 2 | Status; requests use 0 |
| 10 | 2 | Reserved; currently 0 |
| 12 | 4 | Request ID |
| 16 | 4 | Payload size |
| 20 | 4 | IEEE CRC32 over bytes 0..19 followed by the payload |

Debug-session commands are `CONNECT=1` and `DISCONNECT=2`. `CONNECT` returns protocol
version, the `bsp_hw_e` failure mask, and a supported-service bit mask as three
`uint32_t` values. `UsbTransport` sends these commands
when the WinUSB interface is claimed and released. While disconnected, the board LED
breathes blue; an active session changes it to green. A physical USB disconnect clears
the state immediately, and five seconds without a valid request clears an abandoned
session. Disconnect also stops an active CAN capture.

Log commands are `INFO=1`, `BEGIN_EXPORT=2`, `READ=3`, `END_EXPORT=4`, and `CLEAR=5`.
Statuses are `OK=0`, `BAD_FRAME=1`, `UNSUPPORTED_VERSION=2`, `UNKNOWN_SERVICE=3`,
`UNKNOWN_COMMAND=4`, `INVALID_ARGUMENT=5`, `BUSY=6`, `NO_SESSION=7`, and
`STORAGE_ERROR=8`, `NOT_READY=9`, and `IO_ERROR=10`.

## CAN Service

All three CAN buses use the fixed nominal bitrate configured by CubeMX: 1 Mbit/s. The
service observes standard 11-bit data frames and does not transmit. Each aggregate
snapshot includes bus, ID, latest timestamp and payload, receive count, and average
frequency over the latest sampling window. It does not distinguish classic CAN from CAN
FD or expose BRS/ESI metadata.

Commands are `STATUS=1`, `START=2`, `READ=3`, and `STOP=4`.

- `STATUS` version 1 returns bitrate, available bus mask, active bus mask, the tracked-ID
  limit per bus, frequency window duration, then eight values for each bus: captured frames,
  frames whose ID could not be tracked, recent frequency in millihertz, FDCAN FIFO
  lost events, TX errors, RX errors, bus-off events, and last BSP status.
- `START` takes a `uint32_t` bus mask. Bits 0..2 select CAN1..CAN3. It clears the capture
  aggregation slots and counters before enabling capture.
- `READ` returns version, sample count, frequency window duration, three untracked-frame counters,
  then one snapshot per tracked bus and ID. Each snapshot is `bus:uint8`,
  `length:uint8`, a zero `uint16`, `id:uint32`, `timestamp_ms:uint32`,
  `count:uint32`, `frequency_millihz:uint32`, followed by the latest payload bytes.
- `STOP` disables capture. Disconnecting the host has the same effect.

The firmware tracks up to 16 standard IDs per bus and keeps only the latest payload and
receive count. It calculates each ID's frequency from frames received since the previous
`READ`; the TUI therefore displays a recent window of about 250 ms at its 4 Hz sampling
rate. Per-bus frequency uses the interval since the previous `STATUS`, normally about
one second in the TUI. A window with no new frames reports 0 Hz. Once a bus uses all 16
slots, frames with new IDs increment its untracked counter while existing IDs continue
to be counted.
Both frequency fields use `window_frame_count * 1000000 / window_ms` to encode
millihertz.
The FDCAN FIFO lost counter records hardware lost-flag events and is therefore a lower
bound when several messages are lost before one interrupt is serviced.

## IMU Service

Commands are `STATUS=1`, `READ_STATE=2`, `START_CALIBRATION=3`, and
`CANCEL_CALIBRATION=4`.

`STATUS` returns version, flags, calibration state and error, progress and duration in
milliseconds, and the three saved gyro corrections. Flag bits mean hardware ready,
INS ready, calibration valid, and calibration persisted. Calibration states are idle,
running, saving, succeeded, failed, and cancelled. Errors distinguish IMU reads from
Flash writes.

`READ_STATE` returns version and timestamp followed by raw accelerometer, raw gyroscope,
temperature, transformed/corrected accelerometer and gyroscope, quaternion, roll,
pitch, yaw, accumulated yaw, and converged/stationary flags. All sensor and attitude
values are little-endian IEEE-754 floats. The host polls this command at 20 Hz.

`START_CALIBRATION` begins a non-blocking 30-second stationary gyro calibration. Motion
resets progress to zero. The existing correction remains active while sampling. After
30 uninterrupted seconds the candidate is written to Flash; only a successful write
replaces the active correction, and the EKF attitude/yaw state is then reinitialized.
`CANCEL_CALIBRATION` cancels a running sample phase.

## Log Service

`INFO` returns eight `uint32_t` values: payload version 1, flags, total size, used size,
snapshot size, boot ID, next sequence, and queued records. Flag bit 0 means storage is
ready; bit 1 means an export is active.

`BEGIN_EXPORT` has no request payload and returns `session_id, snapshot_size`. `READ`
takes `session_id, offset, size`, where offset and size are 4-byte aligned and size is at
most 16348. Its response is `session_id, offset, data`. The complete response is four
bytes below the 16-KiB host receive buffer, so a full-size USB FS transfer ends with a
short packet. `END_EXPORT` takes a session ID.
`CLEAR` takes the confirmation bytes `CLR!`.

Only one export can be active. Its storage view is frozen, new application records are
not accepted, and the session expires after five seconds without a `READ`.

## Record Format

The EasyFlash log stream contains 4-byte-aligned records of at most 256 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `LOG1` |
| 4 | 1 | Version, currently 1 |
| 5 | 1 | Level: error 1, warning 2, info 3 |
| 6 | 2 | Flags; bit 0 means the message was truncated |
| 8 | 4 | Total aligned record size |
| 12 | 4 | Sequence |
| 16 | 4 | Boot ID |
| 20 | 4 | Uptime in milliseconds |
| 24 | 2 | Payload size |
| 26 | 1 | Task-name size |
| 27 | 1 | Reserved |
| 28 | 4 | IEEE CRC32 over bytes 0..27 followed by the payload |

The payload is the UTF-8 task name followed by the UTF-8 message, without terminators.
Padding bytes are `0xFF` and are not covered by the CRC. Host parsing resynchronizes at
4-byte boundaries after interrupted or corrupt records.

Application code writes Flash records through `logger::info`, `logger::warn`, and
`logger::error`. Flash and UART have independent level filters. Call
`logger::init_flash(level)` to enable Flash logging and `logger::init(port, level)` to
enable UART logging; omit either initializer to leave that output disabled. Normal mode
uses Flash level `WARNING`. Debug mode passes `NONE`, which initializes the storage while
disabling new Flash records and does not advance the persisted boot ID. The application
debug module owns the WinUSB protocol and uses the logger Flash maintenance interface
for snapshot operations.

## Retention

The physical EasyFlash log region is 1 MiB. Each of its 256 4-KiB sectors uses a
12-byte header, leaving 1045504 bytes for records. For reference, a 68-byte record once
per second retains about 15375 records, or 4.27 hours. Maximum-size 256-byte records
retain 4084 records.

EasyFlash rotates whole 4-KiB sectors. When writing needs the next sector, that sector
is erased and the logical start advances if it contained the oldest data. A record that
crossed the erased sector boundary can therefore leave an incomplete suffix at the new
start; the host parser skips incomplete or CRC-invalid data and resynchronizes on `LOG1`
at a 4-byte boundary. During an active export the snapshot remains fixed, so new records
are not accepted instead of rotating storage.

`CLEAR` erases the records and resets the persisted boot ID to 0. Records written before
the next reboot use boot ID 0; the next boot starts again at 1. Record sequence also
restarts at 0.

Full-area erases use 4-KiB sector erase for the unaligned edges and 64-KiB block erase
for the aligned middle of the log partition.
