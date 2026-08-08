import pathlib
import struct
import sys
import threading
import unittest
from unittest.mock import patch


TOOLS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

from trobot_host.client import LogClient  # noqa: E402
from trobot_host.protocol import (  # noqa: E402
    CalibrationState,
    CanBusStatus,
    CanFrame,
    CanRead,
    CanStatus,
    DebugCommand,
    LogCommand,
    LogInfo,
    LogLevel,
    LogRecord,
    ProtocolError,
    Service,
    Status,
    decode_can_read,
    decode_can_status,
    decode_frame,
    decode_imu_state,
    decode_imu_status,
    decode_info,
    encode_frame,
    format_records,
    parse_records,
)
from trobot_host.tui import (  # noqa: E402
    ConfirmClearScreen,
    TRobotTui,
    filter_records,
    group_records_by_boot,
)
from trobot_host.transport import DeviceInfo, UsbTransport  # noqa: E402


INFO_REQUEST = bytes.fromhex(
    "5444424701010100000000004433221100000000a95acf5b"
)

VALID_RECORD = bytes.fromhex(
    "4c4f4731010300002c0000000700000002000000d2040000"
    "090004001b5aa4276d61696e68656c6c6fffffff"
)


class FrameProtocolTests(unittest.TestCase):
    def test_info_request_has_stable_wire_format(self):
        encoded = encode_frame(Service.LOG, LogCommand.INFO, request_id=0x11223344)

        self.assertEqual(encoded, INFO_REQUEST)

    def test_info_request_decodes_from_wire_format(self):
        frame = decode_frame(INFO_REQUEST)

        self.assertEqual(frame.service, Service.LOG)
        self.assertEqual(frame.command, LogCommand.INFO)
        self.assertEqual(frame.status, Status.OK)
        self.assertEqual(frame.request_id, 0x11223344)
        self.assertEqual(frame.payload, b"")

    def test_frame_with_corrupt_crc_is_rejected(self):
        corrupt = bytearray(INFO_REQUEST)
        corrupt[-1] ^= 0x80

        with self.assertRaises(ProtocolError):
            decode_frame(bytes(corrupt))

    def test_frame_with_nonzero_reserved_field_is_rejected(self):
        invalid = bytearray(INFO_REQUEST)
        invalid[10] = 1

        with self.assertRaises(ProtocolError):
            decode_frame(bytes(invalid))

    def test_info_payload_decodes_storage_status(self):
        payload = struct.pack("<8I", 1, 3, 1045504, 16, 12, 2, 8, 1)

        info = decode_info(payload)

        self.assertTrue(info.storage_ready)
        self.assertTrue(info.export_active)
        self.assertEqual(info.total_size, 1045504)
        self.assertEqual(info.used_size, 16)
        self.assertEqual(info.snapshot_size, 12)
        self.assertEqual(info.boot_id, 2)
        self.assertEqual(info.next_sequence, 8)
        self.assertEqual(info.queued_records, 1)

    def test_can_status_and_aggregate_snapshots_decode(self):
        status = decode_can_status(
            struct.pack("<6I", 1, 1_000_000, 7, 1, 16, 1000)
            + struct.pack("<8I", 43500, 3, 8_700_000, 2, 0, 0, 0, 0)
            + struct.pack("<16I", *([0] * 16))
        )
        frames = decode_can_read(
            struct.pack("<6I", 1, 1, 250, 3, 0, 0)
            + struct.pack("<BBHIIII", 0, 8, 0, 0x006, 4999, 43500, 8_700_000)
            + bytes.fromhex("11223344aabbccdd")
        )

        self.assertEqual(status.bitrate, 1_000_000)
        self.assertEqual(status.tracked_id_limit, 16)
        self.assertEqual(status.buses[0].captured, 43500)
        self.assertEqual(status.buses[0].frequency_hz, 8700.0)
        self.assertEqual(status.buses[0].fifo_lost, 2)
        self.assertEqual(status.sample_elapsed_ms, 1000)
        self.assertEqual(frames.sample_elapsed_ms, 250)
        self.assertEqual(frames.missed, (3, 0, 0))
        self.assertEqual(frames.frames[0].can_id, 0x006)
        self.assertEqual(frames.frames[0].count, 43500)
        self.assertEqual(frames.frames[0].frequency_hz, 8700.0)
        self.assertEqual(frames.frames[0].data, bytes.fromhex("11223344aabbccdd"))

    def test_imu_status_and_state_decode(self):
        status = decode_imu_status(
            struct.pack("<6I3f", 1, 0xF, 1, 0, 1200, 30000, 0.1, 0.2, 0.3)
        )
        state = decode_imu_state(
            struct.pack("<II7f6f4f4fI", 1, 42, *range(21), 3)
        )

        self.assertEqual(status.calibration_state, CalibrationState.RUNNING)
        self.assertEqual(status.calibration_progress_ms, 1200)
        self.assertTrue(status.calibration_persisted)
        self.assertEqual(state.timestamp_ms, 42)
        self.assertEqual(state.raw_accel, (0.0, 1.0, 2.0))
        self.assertTrue(state.converged)
        self.assertTrue(state.stationary)


class LogRecordTests(unittest.TestCase):
    def test_parser_resynchronizes_after_garbage(self):
        records = parse_records(b"\xff\xff\xff\xff" + VALID_RECORD + VALID_RECORD[:20])

        self.assertEqual(len(records), 1)
        record = records[0]
        self.assertEqual(record.level, LogLevel.INFO)
        self.assertEqual(record.sequence, 7)
        self.assertEqual(record.boot_id, 2)
        self.assertEqual(record.uptime_ms, 1234)
        self.assertEqual(record.task_name, "main")
        self.assertEqual(record.message, "hello")
        self.assertFalse(record.truncated)

    def test_parser_skips_record_with_corrupt_crc(self):
        corrupt = bytearray(VALID_RECORD)
        corrupt[35] ^= 0x01

        self.assertEqual(parse_records(bytes(corrupt)), [])

    def test_records_have_stable_text_and_jsonl_formats(self):
        records = parse_records(VALID_RECORD)

        self.assertEqual(
            format_records(records, "text"),
            "[boot=2 seq=7 uptime_ms=1234 level=info task=main] hello\n",
        )
        self.assertEqual(
            format_records(records, "jsonl"),
            '{"boot_id":2,"level":"info","message":"hello","sequence":7,'
            '"task_name":"main","truncated":false,"uptime_ms":1234}\n',
        )

    def test_tui_filter_combines_level_boot_and_text(self):
        first = parse_records(VALID_RECORD)[0]
        second = LogRecord(
            level=LogLevel.WARNING,
            sequence=8,
            boot_id=3,
            uptime_ms=250,
            task_name="worker",
            message="second boot",
            truncated=False,
        )
        records = [first, second]

        self.assertEqual(filter_records(records, "all", None, ""), records)
        self.assertEqual(filter_records(records, "info", None, "HELLO"), [first])
        self.assertEqual(filter_records(records, "all", 2, "MAIN"), [first])
        self.assertEqual(filter_records(records, "warning", 3, "SECOND"), [second])
        self.assertEqual(filter_records(records, "error", None, ""), [])

    def test_tui_groups_newest_boot_and_sequence_first(self):
        first = parse_records(VALID_RECORD)[0]
        records = [
            first,
            LogRecord(LogLevel.INFO, 8, 3, 100, "worker", "earlier", False),
            LogRecord(LogLevel.INFO, 9, 3, 200, "worker", "later", False),
        ]

        groups = group_records_by_boot(records)

        self.assertEqual([boot_id for boot_id, _ in groups], [3, 2])
        self.assertEqual(
            [[record.sequence for record in group] for _, group in groups],
            [[9, 8], [7]],
        )


class LogClientTests(unittest.TestCase):
    def test_download_reads_snapshot_in_bounded_chunks_and_ends_session(self):
        expected = bytes(range(250)) * 80

        class FakeTransport:
            def __init__(self):
                self.read_sizes = []
                self.ended = False

            def request(self, service, command, payload=b"", timeout_ms=None):
                self.assert_log_service(service)
                if command == LogCommand.BEGIN_EXPORT:
                    return struct.pack("<II", 7, len(expected))
                if command == LogCommand.READ:
                    session, offset, size = struct.unpack("<III", payload)
                    self.read_sizes.append(size)
                    return struct.pack("<II", session, offset) + expected[offset : offset + size]
                if command == LogCommand.END_EXPORT:
                    self.ended = struct.unpack("<I", payload)[0] == 7
                    return b""
                raise AssertionError(f"unexpected command {command}")

            @staticmethod
            def assert_log_service(service):
                if service != Service.LOG:
                    raise AssertionError(f"unexpected service {service}")

        transport = FakeTransport()
        progress = []

        self.assertEqual(LogClient(transport).download(lambda *value: progress.append(value)), expected)
        self.assertEqual(transport.read_sizes, [16348, 3652])
        self.assertTrue(transport.ended)
        self.assertEqual(progress, [(0, 20000), (16348, 20000), (20000, 20000)])

    def test_clear_uses_extended_timeout_and_confirmation_payload(self):
        calls = []

        class FakeTransport:
            def request(self, service, command, payload=b"", timeout_ms=None):
                calls.append((service, command, payload, timeout_ms))
                return b""

        LogClient(FakeTransport()).clear()

        self.assertEqual(
            calls,
            [(Service.LOG, LogCommand.CLEAR, b"CLR!", 60000)],
        )


class UsbTransportTests(unittest.TestCase):
    def test_transport_lifetime_controls_debug_session(self):
        requests = []

        class FakeDevice:
            iSerialNumber = 1

            def __init__(self):
                self.response = b""

            @staticmethod
            def get_active_configuration():
                return {(2, 0): object()}

            def write(self, endpoint, data, timeout):
                request = decode_frame(bytes(data))
                requests.append((request.service, request.command))
                payload = (
                    struct.pack("<III", 1, 0, 0x1E)
                    if request.command == DebugCommand.CONNECT else b""
                )
                self.response = encode_frame(
                    request.service,
                    request.command,
                    request.request_id,
                    payload,
                )
                return len(data)

            def read(self, endpoint, size, timeout):
                return self.response

        class FakeUsbUtil:
            released = False

            @staticmethod
            def get_string(device, index):
                return "ABC123"

            @staticmethod
            def claim_interface(device, interface):
                pass

            @classmethod
            def release_interface(cls, device, interface):
                cls.released = True

            @staticmethod
            def dispose_resources(device):
                pass

        device = FakeDevice()
        device_info = DeviceInfo(0x0483, 0x5741, "ABC123", "TRobot")
        with patch(
            "trobot_host.transport._matching_devices",
            return_value=(object(), FakeUsbUtil, [device]),
        ):
            transport = UsbTransport(device_info)
            transport.close()

        self.assertEqual(
            requests,
            [
                (Service.DEBUG, DebugCommand.CONNECT),
                (Service.DEBUG, DebugCommand.DISCONNECT),
            ],
        )
        self.assertTrue(FakeUsbUtil.released)


class TuiTests(unittest.IsolatedAsyncioTestCase):
    async def test_log_workspace_mounts_without_scanning(self):
        app = TRobotTui(auto_scan=False)

        async with app.run_test(size=(120, 40)) as pilot:
            await pilot.pause()
            self.assertEqual(len(app.query_one("#log-table").columns), 6)
            self.assertEqual(len(app.query_one("#can-table").columns), 7)
            self.assertEqual(app.query_one("#imu-values").content, "No IMU data")
            self.assertTrue(app.query_one("#load-logs").disabled)

    async def test_can_rows_show_aggregate_frequency(self):
        app = TRobotTui(auto_scan=False)

        async with app.run_test(size=(120, 40)) as pilot:
            app._show_can_frames(
                CanRead(
                    1100,
                    (0, 0, 0),
                    (CanFrame(0, 0x123, 1100, b"\x02", 2, 10.0),),
                )
            )
            await pilot.pause()
            row = app.query_one("#can-table").get_row_at(0)

            self.assertEqual(row[1:4], ["0x123", "2", "10.0 Hz"])
            self.assertEqual(row[6], "02")

    async def test_can_summary_does_not_mix_sampling_windows(self):
        app = TRobotTui(auto_scan=False)
        bus = CanBusStatus(100, 0, 100.0, 0, 0, 0, 0, 0)

        async with app.run_test(size=(120, 40)) as pilot:
            app._show_can_frames(CanRead(250, (0, 0, 0), ()))
            app._show_can_status(CanStatus(1_000_000, 7, 1, 16, 1000, (bus, bus, bus)))
            await pilot.pause()

            summary = str(app.query_one("#can-summary").content)
            self.assertNotIn("window", summary)

    async def test_log_controls_fit_narrow_terminal(self):
        app = TRobotTui(auto_scan=False)

        async with app.run_test(size=(80, 24)) as pilot:
            await pilot.pause()
            controls = [
                app.query_one(selector)
                for selector in (
                    "#refresh-info",
                    "#load-logs",
                    "#save-logs",
                    "#clear-logs",
                    "#level-filter",
                    "#boot-filter",
                    "#search-filter",
                )
            ]
            self.assertTrue(app.screen.has_class("-narrow"))
            self.assertGreaterEqual(app.query_one("#search-filter").region.width, 16)
            self.assertTrue(all(widget.region.right <= 80 for widget in controls))
            controls_bottom = app.query_one("#log-controls").region.bottom
            self.assertTrue(
                all(widget.region.bottom <= controls_bottom for widget in controls)
            )

    async def test_boot_groups_collapse_and_selected_boot_expands(self):
        app = TRobotTui(auto_scan=False)
        records = [
            LogRecord(LogLevel.INFO, 7, 2, 100, "main", "old boot", False),
            LogRecord(LogLevel.INFO, 8, 3, 100, "main", "earlier", False),
            LogRecord(LogLevel.INFO, 9, 3, 200, "main", "later", False),
        ]
        info = LogInfo(True, False, 1024, 128, 128, 3, 10, 0)

        async with app.run_test(size=(120, 40)) as pilot:
            app._logs_loaded(b"logs", records, info)
            await pilot.pause()
            table = app.query_one("#log-table")
            boot_filter = app.query_one("#boot-filter")

            self.assertEqual(
                boot_filter._options,
                [("All boots", "all"), ("3", 3), ("2", 2)],
            )
            self.assertEqual(table.row_count, 4)
            self.assertEqual(table.get_row_at(0)[1].plain, "▾ 3")
            self.assertEqual(table.get_row_at(1)[:2], ["9", ""])
            self.assertEqual(table.get_row_at(2)[:2], ["8", ""])
            self.assertEqual(table.get_row_at(3)[1].plain, "▸ 2")

            table.focus()
            table.move_cursor(row=0)
            await pilot.press("enter")
            await pilot.pause()
            self.assertEqual(table.row_count, 2)

            boot_filter.value = 2
            await pilot.pause()
            self.assertEqual(table.row_count, 2)
            self.assertEqual(table.get_row_at(0)[1].plain, "▾ 2")
            self.assertEqual(table.get_row_at(1)[0], "7")

    async def test_scan_and_connect_enable_log_operations(self):
        device = DeviceInfo(0x0483, 0x5741, "ABC123", "TRobot")
        scan_count = 0

        def discover():
            nonlocal scan_count
            scan_count += 1
            return [device]

        class FakeTransport:
            def close(self):
                pass

            def request(self, service, command, payload=b"", timeout_ms=None):
                if service == Service.LOG and command == LogCommand.INFO:
                    return struct.pack("<8I", 1, 1, 1045504, 44, 0, 2, 8, 0)
                raise AssertionError(f"unexpected request {service}:{command}")

        app = TRobotTui(
            discoverer=discover,
            connector=lambda _: FakeTransport(),
        )

        async with app.run_test(size=(120, 40)) as pilot:
            await pilot.pause(0.2)
            await pilot.click("#connect")
            await pilot.pause(0.2)
            self.assertFalse(app.query_one("#load-logs").disabled)
            self.assertFalse(app.query_one("#clear-logs").disabled)
            self.assertTrue(app.query_one("#scan").disabled)
            self.assertTrue(app.query_one("#device-select").disabled)
            app.action_scan()
            await pilot.pause()
            self.assertEqual(scan_count, 1)

            await pilot.click("#disconnect")
            await pilot.pause(0.2)
            self.assertFalse(app.query_one("#scan").disabled)
            self.assertFalse(app.query_one("#device-select").disabled)

    async def test_app_exit_closes_active_transport(self):
        device = DeviceInfo(0x0483, 0x5741, "ABC123", "TRobot")

        class FakeTransport:
            def __init__(self):
                self.close_count = 0

            def close(self):
                self.close_count += 1

            def request(self, service, command, payload=b"", timeout_ms=None):
                if service == Service.LOG and command == LogCommand.INFO:
                    return struct.pack("<8I", 1, 1, 1045504, 44, 0, 2, 8, 0)
                raise AssertionError(f"unexpected request {service}:{command}")

        transport = FakeTransport()
        app = TRobotTui(
            discoverer=lambda: [device],
            connector=lambda _: transport,
        )

        async with app.run_test(size=(120, 40)) as pilot:
            await pilot.pause(0.2)
            await pilot.click("#connect")
            await pilot.pause(0.2)

            app.exit()

            self.assertEqual(transport.close_count, 1)

    async def test_connect_shortcut_is_ignored_while_scan_is_running(self):
        device = DeviceInfo(0x0483, 0x5741, "ABC123", "TRobot")
        scan_started = threading.Event()
        finish_scan = threading.Event()
        connect_count = 0

        def discover():
            scan_started.set()
            finish_scan.wait(timeout=1)
            return [device]

        def connect(_):
            nonlocal connect_count
            connect_count += 1
            raise AssertionError("connection must not start during a scan")

        app = TRobotTui(discoverer=discover, connector=connect, auto_scan=False)

        async with app.run_test(size=(120, 40)) as pilot:
            app._set_devices([device])
            app.scan_devices()
            await pilot.pause(0.1)
            self.assertTrue(scan_started.is_set())
            self.assertTrue(app.query_one("#connect").disabled)
            app.action_connect()
            await pilot.pause()
            self.assertEqual(connect_count, 0)
            finish_scan.set()
            await pilot.pause(0.2)

    async def test_failed_debug_handshake_restores_connection_controls(self):
        device = DeviceInfo(0x0483, 0x5741, "ABC123", "TRobot")

        class FailingTransport:
            def close(self):
                raise RuntimeError("release failed")

            def request(self, service, command, payload=b"", timeout_ms=None):
                raise TimeoutError("operation timed out")

        app = TRobotTui(
            discoverer=lambda: [device],
            connector=lambda _: FailingTransport(),
        )

        async with app.run_test(size=(120, 40)) as pilot:
            await pilot.pause(0.2)
            await pilot.click("#connect")
            await pilot.pause(0.2)
            self.assertEqual(app.query_one("#connection-state").content, "OFFLINE")
            self.assertIn(
                "No debug response",
                str(app.query_one("#connection-status").content),
            )
            self.assertFalse(app.query_one("#scan").disabled)
            self.assertFalse(app.query_one("#connect").disabled)

    async def test_device_removal_disconnects_automatically(self):
        device = DeviceInfo(0x0483, 0x5741, "ABC123", "TRobot")

        class RemovedTransport:
            def __init__(self):
                self.request_count = 0
                self.closed = False

            def close(self):
                self.closed = True

            def request(self, service, command, payload=b"", timeout_ms=None):
                self.request_count += 1
                if self.request_count == 1:
                    return struct.pack("<8I", 1, 1, 1045504, 44, 0, 2, 8, 0)
                raise RuntimeError("USB device was disconnected")

        transport = RemovedTransport()
        app = TRobotTui(
            discoverer=lambda: [device],
            connector=lambda _: transport,
        )
        app.CONNECTION_CHECK_INTERVAL = 0.05

        async with app.run_test(size=(120, 40)) as pilot:
            await pilot.pause(0.2)
            await pilot.click("#connect")
            await pilot.pause(0.2)
            self.assertEqual(app.query_one("#connection-state").content, "OFFLINE")
            self.assertIn("Connection lost", str(app.query_one("#connection-status").content))
            self.assertGreaterEqual(transport.request_count, 2)
            self.assertTrue(transport.closed)
            self.assertFalse(app.query_one("#scan").disabled)
            self.assertTrue(app.query_one("#disconnect").disabled)

    async def test_clear_dialog_returns_confirmation(self):
        app = TRobotTui(auto_scan=False)
        result = []

        async with app.run_test(size=(120, 40)) as pilot:
            app.push_screen(ConfirmClearScreen(), result.append)
            await pilot.pause()
            await pilot.click("#confirm-clear")
            await pilot.pause()

        self.assertEqual(result, [True])


if __name__ == "__main__":
    unittest.main()
