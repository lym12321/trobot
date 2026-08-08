from __future__ import annotations

from .client import Transport
from .protocol import (
    ImuCommand,
    ImuState,
    ImuStatus,
    Service,
    decode_imu_state,
    decode_imu_status,
)


class ImuClient:
    def __init__(self, transport: Transport) -> None:
        self._transport = transport

    def status(self) -> ImuStatus:
        return decode_imu_status(
            self._transport.request(Service.IMU, ImuCommand.STATUS)
        )

    def state(self) -> ImuState:
        return decode_imu_state(
            self._transport.request(Service.IMU, ImuCommand.READ_STATE)
        )

    def start_calibration(self) -> None:
        self._transport.request(Service.IMU, ImuCommand.START_CALIBRATION)

    def cancel_calibration(self) -> None:
        self._transport.request(Service.IMU, ImuCommand.CANCEL_CALIBRATION)
