from __future__ import annotations

import struct
from collections.abc import Callable
from typing import Protocol

from .protocol import (
    FRAME_HEADER,
    MAX_RESPONSE_SIZE,
    LogCommand,
    LogInfo,
    Service,
    decode_info,
)


READ_CHUNK_SIZE = MAX_RESPONSE_SIZE - FRAME_HEADER.size - 8 - 4
ProgressCallback = Callable[[int, int], None]


class Transport(Protocol):
    def request(
        self,
        service: int,
        command: int,
        payload: bytes = b"",
        timeout_ms: int | None = None,
    ) -> bytes: ...


class LogClient:
    def __init__(self, transport: Transport) -> None:
        self._transport = transport

    def info(self) -> LogInfo:
        return decode_info(self._transport.request(Service.LOG, LogCommand.INFO))

    def download(self, progress: ProgressCallback | None = None) -> bytes:
        begin = self._transport.request(Service.LOG, LogCommand.BEGIN_EXPORT)
        if len(begin) != 8:
            raise RuntimeError("BEGIN_EXPORT response has an invalid size")
        session, snapshot_size = struct.unpack("<II", begin)
        try:
            output = bytearray()
            if progress is not None:
                progress(0, snapshot_size)
            offset = 0
            while offset < snapshot_size:
                requested = min(READ_CHUNK_SIZE, snapshot_size - offset)
                payload = self._transport.request(
                    Service.LOG,
                    LogCommand.READ,
                    struct.pack("<III", session, offset, requested),
                )
                if len(payload) < 8:
                    raise RuntimeError("READ response has an invalid size")
                response_session, response_offset = struct.unpack_from("<II", payload)
                chunk = payload[8:]
                if (
                    response_session != session
                    or response_offset != offset
                    or len(chunk) != requested
                ):
                    raise RuntimeError("READ response does not match the requested range")
                output.extend(chunk)
                offset += len(chunk)
                if progress is not None:
                    progress(offset, snapshot_size)
        except BaseException as error:
            try:
                self._transport.request(
                    Service.LOG,
                    LogCommand.END_EXPORT,
                    struct.pack("<I", session),
                )
            except Exception as cleanup_error:
                error.add_note(f"END_EXPORT cleanup failed: {cleanup_error}")
            raise
        else:
            self._transport.request(
                Service.LOG,
                LogCommand.END_EXPORT,
                struct.pack("<I", session),
            )
        return bytes(output)

    def clear(self, timeout_ms: int = 60000) -> None:
        self._transport.request(
            Service.LOG,
            LogCommand.CLEAR,
            b"CLR!",
            timeout_ms=timeout_ms,
        )
