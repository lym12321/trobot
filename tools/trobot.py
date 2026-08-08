from __future__ import annotations

import argparse
import dataclasses
import json
import pathlib
import sys
import time

from trobot_host.can_client import CanClient
from trobot_host.client import LogClient
from trobot_host.export import render_log, save_log
from trobot_host.imu_client import ImuClient
from trobot_host.protocol import CalibrationState, ProtocolError
from trobot_host.transport import (
    DEFAULT_PID,
    DEFAULT_VID,
    DeviceInfo,
    UsbTransport,
    discover_devices,
)


def parse_number(value: str) -> int:
    return int(value, 0)


def select_device(vid: int, pid: int, serial: str | None) -> DeviceInfo:
    devices = discover_devices(vid, pid)
    if serial is not None:
        devices = [device for device in devices if device.serial == serial]
    if not devices:
        raise RuntimeError(f"TRobot USB device {vid:04x}:{pid:04x} was not found")
    if len(devices) > 1:
        raise RuntimeError("multiple TRobot devices found; select one with --serial")
    return devices[0]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="TRobot debug console")
    parser.add_argument("--vid", type=parse_number, default=DEFAULT_VID)
    parser.add_argument("--pid", type=parse_number, default=DEFAULT_PID)
    parser.add_argument("--serial")
    parser.add_argument("--timeout", type=int, default=2000)
    commands = parser.add_subparsers(dest="service")
    commands.add_parser("tui", help="open the interactive debug console")
    commands.add_parser("info", help="show hardware failures and supported services")

    log = commands.add_parser("log", help="inspect persistent logs")
    log_commands = log.add_subparsers(dest="command", required=True)
    log_commands.add_parser("info")
    dump = log_commands.add_parser("dump")
    dump.add_argument("--format", choices=("text", "jsonl", "raw"), default="text")
    dump.add_argument("-o", "--output", type=pathlib.Path)
    clear = log_commands.add_parser("clear")
    clear.add_argument("--yes", action="store_true")

    can = commands.add_parser("can", help="inspect aggregated CAN traffic")
    can_commands = can.add_subparsers(dest="command", required=True)
    can_commands.add_parser("status")
    watch_can = can_commands.add_parser("watch")
    watch_can.add_argument(
        "--buses", type=parse_number, default=0x7,
        help="bus bit mask, bit 0 selects CAN1"
    )

    imu = commands.add_parser("imu", help="inspect and calibrate the IMU")
    imu_commands = imu.add_subparsers(dest="command", required=True)
    imu_commands.add_parser("status")
    imu_commands.add_parser("watch")
    imu_commands.add_parser("calibrate")
    imu_commands.add_parser("cancel")
    return parser


def print_json(value: object) -> None:
    print(json.dumps(dataclasses.asdict(value), indent=2, sort_keys=True))


def run_tui(args: argparse.Namespace) -> None:
    from trobot_host.tui import TRobotTui

    TRobotTui(
        discoverer=lambda: discover_devices(args.vid, args.pid),
        connector=lambda device: UsbTransport(device, args.timeout),
    ).run()


def run_log(args: argparse.Namespace, client: LogClient) -> None:
    if args.command == "info":
        print_json(client.info())
    elif args.command == "dump":
        data = client.download()
        if args.output is not None:
            save_log(data, args.format, args.output)
        else:
            rendered = render_log(data, args.format)
            if isinstance(rendered, bytes):
                sys.stdout.buffer.write(rendered)
            else:
                sys.stdout.write(rendered)
    elif args.command == "clear":
        if not args.yes:
            raise RuntimeError("log clear requires --yes")
        client.clear(timeout_ms=max(args.timeout, 60000))
        print("Logs cleared.")


def run_can(args: argparse.Namespace, client: CanClient) -> None:
    if args.command == "status":
        print_json(client.status())
        return

    client.start(args.buses)
    try:
        while True:
            for frame in client.read().frames:
                print(
                    f"{frame.timestamp_ms:10d} CAN{frame.bus + 1} "
                    f"{frame.can_id:03X} [{len(frame.data):2d}] "
                    f"count={frame.count} rate={frame.frequency_hz:.1f} Hz "
                    f"{frame.data.hex(' ')}"
                )
            time.sleep(0.25)
    finally:
        client.stop()


def run_imu(args: argparse.Namespace, client: ImuClient) -> None:
    if args.command == "status":
        print_json(client.status())
    elif args.command == "cancel":
        client.cancel_calibration()
        print("Calibration cancelled.")
    elif args.command == "watch":
        while True:
            state = client.state()
            print(
                f"roll={state.roll:+.5f} pitch={state.pitch:+.5f} "
                f"yaw={state.yaw:+.5f} total={state.yaw_total:+.5f} "
                f"temp={state.temperature:.2f} stationary={state.stationary}"
            )
            time.sleep(0.05)
    elif args.command == "calibrate":
        client.start_calibration()
        try:
            while True:
                status = client.status()
                print(
                    f"\r{status.calibration_state.name.lower():10s} "
                    f"{status.calibration_progress_ms:5d} / "
                    f"{status.calibration_duration_ms:5d} ms",
                    end="",
                    flush=True,
                )
                if status.calibration_state not in (
                    CalibrationState.RUNNING,
                    CalibrationState.SAVING,
                ):
                    print()
                    if status.calibration_state != CalibrationState.SUCCEEDED:
                        raise RuntimeError(
                            f"calibration ended in {status.calibration_state.name} "
                            f"({status.calibration_error.name})"
                        )
                    return
                time.sleep(0.2)
        except KeyboardInterrupt:
            client.cancel_calibration()
            print("\nCalibration cancelled.")


def run(args: argparse.Namespace) -> None:
    if args.service in (None, "tui"):
        run_tui(args)
        return

    device = select_device(args.vid, args.pid, args.serial)
    with UsbTransport(device, args.timeout) as transport:
        if args.service == "info":
            print_json(transport.debug_info)
        elif args.service == "log":
            run_log(args, LogClient(transport))
        elif args.service == "can":
            run_can(args, CanClient(transport))
        elif args.service == "imu":
            run_imu(args, ImuClient(transport))


def main() -> int:
    try:
        run(build_parser().parse_args())
    except KeyboardInterrupt:
        return 130
    except (OSError, ProtocolError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
