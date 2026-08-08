from __future__ import annotations

import dataclasses
import enum
import json
import struct
import zlib


FRAME_MAGIC = b"TDBG"
FRAME_VERSION = 1
MAX_RESPONSE_SIZE = 16 * 1024
FRAME_HEADER = struct.Struct("<4sBBBBHHIII")
FRAME_PREFIX = struct.Struct("<4sBBBBHHII")

INFO_PAYLOAD = struct.Struct("<IIIIIIII")
DEBUG_INFO_PAYLOAD = struct.Struct("<III")
CAN_STATUS_HEADER = struct.Struct("<IIIIII")
CAN_BUS_STATUS = struct.Struct("<IIIIIIII")
CAN_READ_HEADER = struct.Struct("<IIIIII")
CAN_FRAME_HEADER = struct.Struct("<BBHIIII")
IMU_STATUS_PAYLOAD = struct.Struct("<IIIIII3f")
IMU_STATE_PAYLOAD = struct.Struct("<II7f6f4f4fI")

RECORD_MAGIC = b"LOG1"
RECORD_VERSION = 1
RECORD_HEADER = struct.Struct("<4sBBHIIIIHBBI")
RECORD_MAX_SIZE = 256


class ProtocolError(ValueError):
    pass


class Service(enum.IntEnum):
    LOG = 1
    DEBUG = 2
    CAN = 3
    IMU = 4


class DebugCommand(enum.IntEnum):
    CONNECT = 1
    DISCONNECT = 2


class LogCommand(enum.IntEnum):
    INFO = 1
    BEGIN_EXPORT = 2
    READ = 3
    END_EXPORT = 4
    CLEAR = 5


class CanCommand(enum.IntEnum):
    STATUS = 1
    START = 2
    READ = 3
    STOP = 4


class ImuCommand(enum.IntEnum):
    STATUS = 1
    READ_STATE = 2
    START_CALIBRATION = 3
    CANCEL_CALIBRATION = 4


class Status(enum.IntEnum):
    OK = 0
    BAD_FRAME = 1
    UNSUPPORTED_VERSION = 2
    UNKNOWN_SERVICE = 3
    UNKNOWN_COMMAND = 4
    INVALID_ARGUMENT = 5
    BUSY = 6
    NO_SESSION = 7
    STORAGE_ERROR = 8
    NOT_READY = 9
    IO_ERROR = 10


class CalibrationState(enum.IntEnum):
    IDLE = 0
    RUNNING = 1
    SAVING = 2
    SUCCEEDED = 3
    FAILED = 4
    CANCELLED = 5


class CalibrationError(enum.IntEnum):
    NONE = 0
    IMU_READ = 1
    FLASH_WRITE = 2


class LogLevel(enum.IntEnum):
    ERROR = 1
    WARNING = 2
    INFO = 3


@dataclasses.dataclass(frozen=True)
class Frame:
    service: int
    command: int
    status: Status
    request_id: int
    payload: bytes


@dataclasses.dataclass(frozen=True)
class LogRecord:
    level: LogLevel
    sequence: int
    boot_id: int
    uptime_ms: int
    task_name: str
    message: str
    truncated: bool


@dataclasses.dataclass(frozen=True)
class LogInfo:
    storage_ready: bool
    export_active: bool
    total_size: int
    used_size: int
    snapshot_size: int
    boot_id: int
    next_sequence: int
    queued_records: int


@dataclasses.dataclass(frozen=True)
class DebugInfo:
    protocol_version: int
    hardware_failures: int
    service_mask: int


@dataclasses.dataclass(frozen=True)
class CanBusStatus:
    captured: int
    missed: int
    frequency_hz: float
    fifo_lost: int
    tx_errors: int
    rx_errors: int
    bus_off_count: int
    last_status: int


@dataclasses.dataclass(frozen=True)
class CanStatus:
    bitrate: int
    available_mask: int
    active_mask: int
    tracked_id_limit: int
    sample_elapsed_ms: int
    buses: tuple[CanBusStatus, ...]


@dataclasses.dataclass(frozen=True)
class CanFrame:
    bus: int
    can_id: int
    timestamp_ms: int
    data: bytes
    count: int
    frequency_hz: float


@dataclasses.dataclass(frozen=True)
class CanRead:
    sample_elapsed_ms: int
    missed: tuple[int, int, int]
    frames: tuple[CanFrame, ...]


@dataclasses.dataclass(frozen=True)
class ImuStatus:
    hardware_ready: bool
    ins_ready: bool
    calibration_valid: bool
    calibration_persisted: bool
    calibration_state: CalibrationState
    calibration_error: CalibrationError
    calibration_progress_ms: int
    calibration_duration_ms: int
    gyro_correction: tuple[float, float, float]


@dataclasses.dataclass(frozen=True)
class ImuState:
    timestamp_ms: int
    raw_accel: tuple[float, float, float]
    raw_gyro: tuple[float, float, float]
    temperature: float
    accel: tuple[float, float, float]
    gyro: tuple[float, float, float]
    quaternion: tuple[float, float, float, float]
    roll: float
    pitch: float
    yaw: float
    yaw_total: float
    converged: bool
    stationary: bool


def encode_frame(
    service: int,
    command: int,
    request_id: int,
    payload: bytes = b"",
    status: Status = Status.OK,
) -> bytes:
    prefix = FRAME_PREFIX.pack(
        FRAME_MAGIC,
        FRAME_VERSION,
        service,
        command,
        0,
        int(status),
        0,
        request_id,
        len(payload),
    )
    checksum = zlib.crc32(prefix + payload) & 0xFFFFFFFF
    return prefix + struct.pack("<I", checksum) + payload


def decode_frame(data: bytes) -> Frame:
    if len(data) < FRAME_HEADER.size:
        raise ProtocolError("frame is shorter than its header")

    (
        magic,
        version,
        service,
        command,
        flags,
        status,
        reserved,
        request_id,
        payload_size,
        checksum,
    ) = FRAME_HEADER.unpack_from(data)
    if magic != FRAME_MAGIC:
        raise ProtocolError("invalid frame magic")
    if version != FRAME_VERSION:
        raise ProtocolError(f"unsupported frame version {version}")
    if flags != 0 or reserved != 0:
        raise ProtocolError("frame reserved fields are not zero")
    if len(data) != FRAME_HEADER.size + payload_size:
        raise ProtocolError("frame payload length does not match header")

    expected = zlib.crc32(data[: FRAME_HEADER.size - 4] + data[FRAME_HEADER.size :])
    if checksum != expected & 0xFFFFFFFF:
        raise ProtocolError("frame CRC32 mismatch")

    try:
        parsed_status = Status(status)
    except ValueError as error:
        raise ProtocolError(str(error)) from error
    return Frame(service, command, parsed_status, request_id, data[FRAME_HEADER.size :])


def decode_info(payload: bytes) -> LogInfo:
    if len(payload) != INFO_PAYLOAD.size:
        raise ProtocolError("INFO payload has an invalid size")

    (
        version,
        flags,
        total_size,
        used_size,
        snapshot_size,
        boot_id,
        next_sequence,
        queued_records,
    ) = INFO_PAYLOAD.unpack(payload)
    if version != 1:
        raise ProtocolError(f"unsupported INFO version {version}")

    return LogInfo(
        storage_ready=bool(flags & 1),
        export_active=bool(flags & 2),
        total_size=total_size,
        used_size=used_size,
        snapshot_size=snapshot_size,
        boot_id=boot_id,
        next_sequence=next_sequence,
        queued_records=queued_records,
    )


def decode_debug_info(payload: bytes) -> DebugInfo:
    if len(payload) != DEBUG_INFO_PAYLOAD.size:
        raise ProtocolError("CONNECT payload has an invalid size")
    version, hardware_failures, service_mask = DEBUG_INFO_PAYLOAD.unpack(payload)
    if version != 1:
        raise ProtocolError(f"unsupported debug protocol version {version}")
    return DebugInfo(version, hardware_failures, service_mask)


def decode_can_status(payload: bytes) -> CanStatus:
    expected = CAN_STATUS_HEADER.size + 3 * CAN_BUS_STATUS.size
    if len(payload) != expected:
        raise ProtocolError("CAN STATUS payload has an invalid size")
    version, bitrate, available, active, tracked_id_limit, sample_elapsed_ms = (
        CAN_STATUS_HEADER.unpack_from(payload)
    )
    if version != 1:
        raise ProtocolError(f"unsupported CAN STATUS version {version}")

    buses = []
    for i in range(3):
        values = CAN_BUS_STATUS.unpack_from(
            payload, CAN_STATUS_HEADER.size + i * CAN_BUS_STATUS.size
        )
        buses.append(CanBusStatus(
            values[0], values[1], values[2] / 1000.0, *values[3:]
        ))
    return CanStatus(
        bitrate, available, active, tracked_id_limit, sample_elapsed_ms, tuple(buses)
    )


def decode_can_read(payload: bytes) -> CanRead:
    if len(payload) < CAN_READ_HEADER.size:
        raise ProtocolError("CAN READ payload is shorter than its header")
    version, count, sample_elapsed_ms, *missed = CAN_READ_HEADER.unpack_from(
        payload
    )
    if version != 1:
        raise ProtocolError(f"unsupported CAN READ version {version}")

    frames: list[CanFrame] = []
    offset = CAN_READ_HEADER.size
    for _ in range(count):
        if len(payload) - offset < CAN_FRAME_HEADER.size:
            raise ProtocolError("CAN frame header is truncated")
        bus, size, reserved, can_id, timestamp, frame_count, frequency_millihz = (
            CAN_FRAME_HEADER.unpack_from(payload, offset)
        )
        offset += CAN_FRAME_HEADER.size
        if bus >= 3 or size > 64 or reserved != 0 or can_id > 0x7FF:
            raise ProtocolError("CAN frame metadata is invalid")
        if frame_count == 0:
            raise ProtocolError("CAN sample count is zero")
        if len(payload) - offset < size:
            raise ProtocolError("CAN frame data is truncated")
        frames.append(CanFrame(
            bus,
            can_id,
            timestamp,
            payload[offset : offset + size],
            frame_count,
            frequency_millihz / 1000.0,
        ))
        offset += size
    if offset != len(payload):
        raise ProtocolError("CAN READ payload has trailing data")
    return CanRead(sample_elapsed_ms, tuple(missed), tuple(frames))


def decode_imu_status(payload: bytes) -> ImuStatus:
    if len(payload) != IMU_STATUS_PAYLOAD.size:
        raise ProtocolError("IMU STATUS payload has an invalid size")
    version, flags, state, error, progress, duration, *correction = (
        IMU_STATUS_PAYLOAD.unpack(payload)
    )
    if version != 1:
        raise ProtocolError(f"unsupported IMU STATUS version {version}")
    try:
        calibration_state = CalibrationState(state)
        calibration_error = CalibrationError(error)
    except ValueError as exception:
        raise ProtocolError(str(exception)) from exception
    return ImuStatus(
        hardware_ready=bool(flags & 1),
        ins_ready=bool(flags & 2),
        calibration_valid=bool(flags & 4),
        calibration_persisted=bool(flags & 8),
        calibration_state=calibration_state,
        calibration_error=calibration_error,
        calibration_progress_ms=progress,
        calibration_duration_ms=duration,
        gyro_correction=tuple(correction),
    )


def decode_imu_state(payload: bytes) -> ImuState:
    if len(payload) != IMU_STATE_PAYLOAD.size:
        raise ProtocolError("IMU READ_STATE payload has an invalid size")
    values = IMU_STATE_PAYLOAD.unpack(payload)
    if values[0] != 1:
        raise ProtocolError(f"unsupported IMU state version {values[0]}")
    return ImuState(
        timestamp_ms=values[1],
        raw_accel=values[2:5],
        raw_gyro=values[5:8],
        temperature=values[8],
        accel=values[9:12],
        gyro=values[12:15],
        quaternion=values[15:19],
        roll=values[19],
        pitch=values[20],
        yaw=values[21],
        yaw_total=values[22],
        converged=bool(values[23] & 1),
        stationary=bool(values[23] & 2),
    )


def parse_records(data: bytes) -> list[LogRecord]:
    records: list[LogRecord] = []
    offset = 0

    while offset + RECORD_HEADER.size <= len(data):
        if data[offset : offset + len(RECORD_MAGIC)] != RECORD_MAGIC:
            offset += 4
            continue

        (
            _,
            version,
            level,
            flags,
            total_size,
            sequence,
            boot_id,
            uptime_ms,
            payload_size,
            task_name_size,
            _,
            checksum,
        ) = RECORD_HEADER.unpack_from(data, offset)

        valid_size = (
            total_size >= RECORD_HEADER.size
            and total_size <= RECORD_MAX_SIZE
            and total_size % 4 == 0
            and payload_size <= total_size - RECORD_HEADER.size
            and task_name_size <= payload_size
            and offset + total_size <= len(data)
        )
        if version != RECORD_VERSION or not valid_size:
            offset += 4
            continue

        payload_start = offset + RECORD_HEADER.size
        payload = data[payload_start : payload_start + payload_size]
        expected = zlib.crc32(data[offset : offset + RECORD_HEADER.size - 4] + payload)
        if checksum != expected & 0xFFFFFFFF:
            offset += 4
            continue

        try:
            parsed_level = LogLevel(level)
        except ValueError:
            offset += total_size
            continue

        records.append(
            LogRecord(
                level=parsed_level,
                sequence=sequence,
                boot_id=boot_id,
                uptime_ms=uptime_ms,
                task_name=payload[:task_name_size].decode("utf-8", errors="replace"),
                message=payload[task_name_size:].decode("utf-8", errors="replace"),
                truncated=bool(flags & 1),
            )
        )
        offset += total_size

    return records


def format_records(records: list[LogRecord], output_format: str) -> str:
    lines: list[str] = []
    for record in records:
        level = record.level.name.lower()
        if output_format == "text":
            lines.append(
                f"[boot={record.boot_id} seq={record.sequence} "
                f"uptime_ms={record.uptime_ms} level={level} "
                f"task={record.task_name}] {record.message}"
            )
        elif output_format == "jsonl":
            lines.append(
                json.dumps(
                    {
                        "boot_id": record.boot_id,
                        "level": level,
                        "message": record.message,
                        "sequence": record.sequence,
                        "task_name": record.task_name,
                        "truncated": record.truncated,
                        "uptime_ms": record.uptime_ms,
                    },
                    ensure_ascii=False,
                    separators=(",", ":"),
                    sort_keys=True,
                )
            )
        else:
            raise ValueError(f"unsupported output format {output_format}")
    return "".join(f"{line}\n" for line in lines)
