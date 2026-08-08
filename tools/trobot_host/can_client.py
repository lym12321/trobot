from __future__ import annotations

import struct

from .client import Transport
from .protocol import (
    CanCommand,
    CanRead,
    CanStatus,
    Service,
    decode_can_read,
    decode_can_status,
)


class CanClient:
    def __init__(self, transport: Transport) -> None:
        self._transport = transport

    def status(self) -> CanStatus:
        return decode_can_status(
            self._transport.request(Service.CAN, CanCommand.STATUS)
        )

    def start(self, bus_mask: int) -> None:
        if bus_mask <= 0 or bus_mask & ~0x7:
            raise ValueError("CAN bus mask must select buses 1 through 3")
        self._transport.request(
            Service.CAN, CanCommand.START, struct.pack("<I", bus_mask)
        )

    def read(self) -> CanRead:
        return decode_can_read(
            self._transport.request(Service.CAN, CanCommand.READ)
        )

    def stop(self) -> None:
        self._transport.request(Service.CAN, CanCommand.STOP)
