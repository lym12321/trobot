from __future__ import annotations

import dataclasses
import threading
from typing import Any

from .protocol import (
    DebugInfo,
    DebugCommand,
    MAX_RESPONSE_SIZE,
    ProtocolError,
    Service,
    Status,
    decode_debug_info,
    decode_frame,
    encode_frame,
)


DEFAULT_VID = 0x0483
DEFAULT_PID = 0x5741
WINUSB_INTERFACE = 2
WINUSB_OUT_EP = 0x05
WINUSB_IN_EP = 0x84


@dataclasses.dataclass(frozen=True)
class DeviceInfo:
    vid: int
    pid: int
    serial: str
    product: str

    @property
    def label(self) -> str:
        identity = self.serial or "no serial"
        return f"{self.product or 'TRobot'} [{identity}]"


def _usb_modules() -> tuple[Any, Any, Any]:
    try:
        import libusb_package
        import usb.core
        import usb.util
    except ImportError as error:
        raise RuntimeError(
            "USB dependencies are missing; run uv sync in tools"
        ) from error
    return libusb_package, usb.core, usb.util


def _matching_devices(vid: int, pid: int) -> tuple[Any, Any, list[Any]]:
    libusb_package, usb_core, usb_util = _usb_modules()
    backend = libusb_package.get_libusb1_backend()
    devices = list(
        usb_core.find(find_all=True, idVendor=vid, idProduct=pid, backend=backend)
        or []
    )
    return usb_core, usb_util, devices


def _string(usb_util: Any, device: Any, index: int) -> str:
    if not index:
        return ""
    try:
        return usb_util.get_string(device, index) or ""
    except Exception:
        return ""


def discover_devices(
    vid: int = DEFAULT_VID,
    pid: int = DEFAULT_PID,
) -> list[DeviceInfo]:
    _, usb_util, devices = _matching_devices(vid, pid)
    return [
        DeviceInfo(
            vid=vid,
            pid=pid,
            serial=_string(usb_util, device, device.iSerialNumber),
            product=_string(usb_util, device, device.iProduct),
        )
        for device in devices
    ]


class UsbTransport:
    def __init__(self, device_info: DeviceInfo, timeout_ms: int = 2000) -> None:
        usb_core, usb_util, devices = _matching_devices(device_info.vid, device_info.pid)
        devices = [
            device
            for device in devices
            if _string(usb_util, device, device.iSerialNumber) == device_info.serial
        ]
        if not devices:
            raise RuntimeError(
                f"TRobot USB device {device_info.vid:04x}:{device_info.pid:04x} "
                f"[{device_info.serial}] was not found"
            )
        if len(devices) > 1:
            raise RuntimeError("multiple matching TRobot devices were found")

        self._usb_util = usb_util
        self._device = devices[0]
        self._timeout_ms = timeout_ms
        self._request_id = 0
        self._lock = threading.Lock()
        self._closed = False
        self.debug_info: DebugInfo
        try:
            configuration = self._device.get_active_configuration()
        except usb_core.USBError:
            self._device.set_configuration()
            configuration = self._device.get_active_configuration()
        configuration[(WINUSB_INTERFACE, 0)]
        usb_util.claim_interface(self._device, WINUSB_INTERFACE)
        try:
            with self._lock:
                self.debug_info = decode_debug_info(
                    self._request_locked(Service.DEBUG, DebugCommand.CONNECT)
                )
        except Exception:
            try:
                usb_util.release_interface(self._device, WINUSB_INTERFACE)
            except Exception:
                pass
            usb_util.dispose_resources(self._device)
            self._closed = True
            raise

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            try:
                self._request_locked(Service.DEBUG, DebugCommand.DISCONNECT)
            except Exception:
                pass
            try:
                self._usb_util.release_interface(self._device, WINUSB_INTERFACE)
            finally:
                try:
                    self._usb_util.dispose_resources(self._device)
                finally:
                    self._closed = True

    def request(
        self,
        service: int,
        command: int,
        payload: bytes = b"",
        timeout_ms: int | None = None,
    ) -> bytes:
        with self._lock:
            if self._closed:
                raise RuntimeError("USB device is closed")
            return self._request_locked(service, command, payload, timeout_ms)

    def ping(self) -> DebugInfo:
        return decode_debug_info(
            self.request(Service.DEBUG, DebugCommand.CONNECT)
        )

    def _request_locked(
        self,
        service: int,
        command: int,
        payload: bytes = b"",
        timeout_ms: int | None = None,
    ) -> bytes:
        self._request_id = (self._request_id + 1) & 0xFFFFFFFF
        request = encode_frame(service, command, self._request_id, payload)
        if len(request) > 64:
            raise ProtocolError("request exceeds the device's 64-byte command limit")

        timeout = self._timeout_ms if timeout_ms is None else timeout_ms
        written = self._device.write(WINUSB_OUT_EP, request, timeout=timeout)
        if written != len(request):
            raise RuntimeError(f"short USB write: {written} of {len(request)} bytes")
        response = decode_frame(
            bytes(self._device.read(WINUSB_IN_EP, MAX_RESPONSE_SIZE, timeout=timeout))
        )
        if (
            response.service != service
            or response.command != command
            or response.request_id != self._request_id
        ):
            raise ProtocolError("response does not match the request")
        if response.status != Status.OK:
            raise RuntimeError(
                f"device returned {response.status.name} for "
                f"service {service} command {command}"
            )
        return response.payload

    def __enter__(self) -> UsbTransport:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
