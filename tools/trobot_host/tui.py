from __future__ import annotations

import math
import pathlib
from collections.abc import Callable, Iterable
from typing import Protocol

from rich.console import RenderableType
from rich.text import Text
from textual import on, work
from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.events import Resize
from textual.screen import ModalScreen
from textual.widgets import (
    Button,
    Checkbox,
    DataTable,
    Footer,
    Input,
    Label,
    ProgressBar,
    Select,
    Static,
    TabbedContent,
    TabPane,
)

from .can_client import CanClient
from .client import LogClient
from .export import save_log
from .imu_client import ImuClient
from .protocol import (
    CanFrame,
    CanRead,
    CanStatus,
    ImuState,
    ImuStatus,
    LogInfo,
    LogRecord,
    parse_records,
)
from .transport import DeviceInfo, UsbTransport, discover_devices


class Transport(Protocol):
    def close(self) -> None: ...

    def request(
        self,
        service: int,
        command: int,
        payload: bytes = b"",
        timeout_ms: int | None = None,
    ) -> bytes: ...

    def ping(self) -> object: ...


def filter_records(
    records: Iterable[LogRecord],
    level: str,
    boot_id: int | None,
    search: str,
) -> list[LogRecord]:
    query = search.casefold().strip()
    return [
        record
        for record in records
        if (level == "all" or record.level.name.lower() == level)
        and (boot_id is None or record.boot_id == boot_id)
        and (
            not query
            or query in record.task_name.casefold()
            or query in record.message.casefold()
        )
    ]


def group_records_by_boot(
    records: Iterable[LogRecord],
) -> list[tuple[int, list[LogRecord]]]:
    groups: dict[int, list[LogRecord]] = {}
    for record in records:
        groups.setdefault(record.boot_id, []).append(record)
    return [
        (
            boot_id,
            sorted(groups[boot_id], key=lambda record: record.sequence, reverse=True),
        )
        for boot_id in sorted(groups, reverse=True)
    ]


def format_uptime(uptime_ms: int) -> str:
    total_seconds, milliseconds = divmod(uptime_ms, 1000)
    hours, remainder = divmod(total_seconds, 3600)
    minutes, seconds = divmod(remainder, 60)
    return f"{hours:02}:{minutes:02}:{seconds:02}.{milliseconds:03}"


def format_size(size: int) -> str:
    if size < 1024:
        return f"{size} B"
    if size < 1024 * 1024:
        return f"{size / 1024:.1f} KiB"
    return f"{size / (1024 * 1024):.2f} MiB"


class ConfirmClearScreen(ModalScreen[bool]):
    CSS = """
    ConfirmClearScreen {
        align: center middle;
    }

    #clear-dialog {
        width: 58;
        height: auto;
        padding: 1 2;
        border: solid $error;
        background: $surface;
    }

    #clear-dialog Horizontal {
        height: auto;
        margin-top: 1;
        align-horizontal: right;
    }
    """

    def compose(self) -> ComposeResult:
        with Vertical(id="clear-dialog"):
            yield Label("Erase all persistent logs and reset boot ID and sequence?")
            with Horizontal():
                yield Button("Cancel", id="cancel-clear")
                yield Button("Clear", id="confirm-clear", variant="error")

    @on(Button.Pressed, "#cancel-clear")
    def cancel_clear(self) -> None:
        self.dismiss(False)

    @on(Button.Pressed, "#confirm-clear")
    def confirm_clear(self) -> None:
        self.dismiss(True)


class SaveLogScreen(ModalScreen[tuple[pathlib.Path, str] | None]):
    CSS = """
    SaveLogScreen {
        align: center middle;
    }

    #save-dialog {
        width: 64;
        height: auto;
        padding: 1 2;
        border: solid $accent;
        background: $surface;
    }

    #save-dialog Label {
        margin-top: 1;
    }

    #save-dialog Horizontal {
        height: auto;
        margin-top: 1;
        align-horizontal: right;
    }
    """

    def compose(self) -> ComposeResult:
        with Vertical(id="save-dialog"):
            yield Label("Path")
            yield Input("trobot-log.jsonl", id="save-path")
            yield Label("Format")
            yield Select(
                [("JSON Lines", "jsonl"), ("Text", "text"), ("Raw", "raw")],
                value="jsonl",
                allow_blank=False,
                id="save-format",
            )
            with Horizontal():
                yield Button("Cancel", id="cancel-save")
                yield Button("Save", id="confirm-save", variant="primary")

    @on(Button.Pressed, "#cancel-save")
    def cancel_save(self) -> None:
        self.dismiss(None)

    @on(Button.Pressed, "#confirm-save")
    def confirm_save(self) -> None:
        path = self.query_one("#save-path", Input).value.strip()
        output_format = self.query_one("#save-format", Select).value
        if not path or not isinstance(output_format, str):
            return
        self.dismiss((pathlib.Path(path).expanduser(), output_format))


class TRobotTui(App[None]):
    TITLE = "TRobot Debug Console"
    SUB_TITLE = "Logs / CAN / IMU"
    CONNECTION_CHECK_INTERVAL = 1.0
    LIVE_UPDATE_INTERVAL = 0.05

    CSS = """
    Screen {
        layout: vertical;
        background: #0e1215;
        color: #dce5e3;
    }

    #masthead {
        height: 3;
        padding: 0 2;
        background: #090c0e;
        border-bottom: solid #283136;
        align-vertical: middle;
    }

    #brand {
        width: auto;
        color: #62d3a4;
        text-style: bold;
    }

    #console-name {
        width: 1fr;
        margin-left: 2;
        color: #7f8d91;
    }

    #connection-state {
        width: 18;
        text-align: right;
        color: #e5b567;
        text-style: bold;
    }

    #device-bar {
        height: 3;
        padding: 0 2;
        background: #151a1e;
        align-vertical: middle;
    }

    #device-bar Label {
        width: 9;
        color: #879397;
        text-style: bold;
    }

    #device-select {
        width: 1fr;
        min-width: 24;
    }

    #device-bar Button {
        margin-left: 1;
        min-width: 11;
    }

    #connection-status {
        height: 2;
        padding: 0 2;
        background: #151a1e;
        color: #879397;
        border-bottom: solid #283136;
    }

    #services {
        height: 1fr;
    }

    #metrics {
        height: 5;
        background: #111619;
        border-bottom: solid #283136;
    }

    .metric {
        width: 1fr;
        height: 5;
        padding: 1 2;
        border-right: solid #283136;
    }

    .metric-label {
        height: 1;
        color: #738085;
        text-style: bold;
    }

    .metric-value {
        height: 2;
        color: #dce5e3;
        text-style: bold;
    }

    #log-controls {
        height: 3;
        padding: 0 2;
        background: #111619;
        align-vertical: middle;
    }

    #section-title {
        width: 9;
        color: #62d3a4;
        text-style: bold;
    }

    #log-controls Button {
        margin-right: 1;
        min-width: 9;
    }

    #level-filter {
        width: 18;
        margin-left: 2;
    }

    #boot-filter {
        width: 18;
        margin-left: 1;
    }

    #search-filter {
        width: 1fr;
        min-width: 18;
        margin-left: 1;
    }

    #download-progress {
        height: 1;
        margin: 0 2;
    }

    #log-table {
        height: 1fr;
        margin: 0 2 1 2;
        background: #0e1215;
        border-top: solid #283136;
    }

    #can-controls, #imu-controls {
        height: 3;
        padding: 0 2;
        align-vertical: middle;
    }

    #can-controls Checkbox, #can-controls Button, #imu-controls Button {
        margin-right: 1;
    }

    #can-summary, #imu-calibration {
        width: 1fr;
        color: #879397;
    }

    #can-table {
        height: 1fr;
        margin: 0 2 1 2;
    }

    #imu-progress {
        height: 1;
        margin: 0 2;
    }

    #imu-values {
        height: 1fr;
        margin: 1 2;
        padding: 1 2;
        border: solid #283136;
    }

    Screen.-narrow #log-controls {
        height: 6;
        padding: 0 1;
        layout: grid;
        grid-size: 4 2;
        grid-columns: 1fr;
        grid-rows: 3;
    }

    Screen.-narrow #section-title {
        display: none;
    }

    Screen.-narrow #log-controls Button {
        width: 1fr;
        min-width: 0;
        margin-right: 0;
    }

    Screen.-narrow #level-filter {
        width: 1fr;
        margin-left: 0;
    }

    Screen.-narrow #boot-filter {
        width: 1fr;
        margin-left: 0;
    }

    Screen.-narrow #search-filter {
        width: 1fr;
        min-width: 16;
        margin-left: 0;
        column-span: 2;
    }

    .hidden {
        display: none;
    }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("r", "scan", "Scan"),
        ("c", "connect", "Connect"),
        ("l", "load_logs", "Load logs"),
    ]

    def __init__(
        self,
        discoverer: Callable[[], list[DeviceInfo]] = discover_devices,
        connector: Callable[[DeviceInfo], Transport] = UsbTransport,
        auto_scan: bool = True,
    ) -> None:
        super().__init__()
        self._discoverer = discoverer
        self._connector = connector
        self._auto_scan = auto_scan
        self._devices: dict[str, DeviceInfo] = {}
        self._transport: Transport | None = None
        self._client: LogClient | None = None
        self._can_client: CanClient | None = None
        self._imu_client: ImuClient | None = None
        self._connection_check_transport: Transport | None = None
        self._connection_timer = None
        self._live_timer = None
        self._live_request_pending = False
        self._live_cycle = 0
        self._can_running = False
        self._imu_ready = False
        self._can_frames: dict[tuple[int, int], CanFrame] = {}
        self._can_rows: set[tuple[int, int]] = set()
        self._can_bus_rate = 0.0
        self._can_fifo_lost = 0
        self._can_missed = 0
        self._raw_log = b""
        self._records: list[LogRecord] = []
        self._collapsed_boots: set[int] = set()

    def compose(self) -> ComposeResult:
        with Horizontal(id="masthead"):
            yield Static("TROBOT", id="brand")
            yield Static("DEBUG CONSOLE", id="console-name")
            yield Static("OFFLINE", id="connection-state")
        with Horizontal(id="device-bar"):
            yield Label("DEVICE")
            yield Select([], prompt="No device selected", id="device-select")
            yield Button("Scan", id="scan", variant="default")
            yield Button("Connect", id="connect", variant="primary")
            yield Button("Disconnect", id="disconnect", disabled=True)
        yield Static("Disconnected", id="connection-status")
        with TabbedContent(initial="logs", id="services"):
            with TabPane("Logs", id="logs"):
                with Horizontal(id="metrics"):
                    with Vertical(classes="metric"):
                        yield Static("STORAGE", classes="metric-label")
                        yield Static("OFFLINE", id="metric-storage", classes="metric-value")
                    with Vertical(classes="metric"):
                        yield Static("USED", classes="metric-label")
                        yield Static("--", id="metric-used", classes="metric-value")
                    with Vertical(classes="metric"):
                        yield Static("BOOT", classes="metric-label")
                        yield Static("--", id="metric-boot", classes="metric-value")
                    with Vertical(classes="metric"):
                        yield Static("NEXT SEQUENCE", classes="metric-label")
                        yield Static("--", id="metric-sequence", classes="metric-value")
                with Horizontal(id="log-controls"):
                    yield Static("LOGS", id="section-title")
                    yield Button("Refresh", id="refresh-info", disabled=True)
                    yield Button("Load", id="load-logs", variant="primary", disabled=True)
                    yield Button("Save", id="save-logs", disabled=True)
                    yield Button("Clear", id="clear-logs", variant="error", disabled=True)
                    yield Select(
                        [
                            ("All levels", "all"),
                            ("Error", "error"),
                            ("Warning", "warning"),
                            ("Info", "info"),
                        ],
                        value="all",
                        allow_blank=False,
                        id="level-filter",
                    )
                    yield Select(
                        [("All boots", "all")],
                        value="all",
                        allow_blank=False,
                        id="boot-filter",
                    )
                    yield Input(placeholder="Filter task or message", id="search-filter")
                yield ProgressBar(
                    total=100,
                    show_eta=False,
                    id="download-progress",
                    classes="hidden",
                )
                yield DataTable(id="log-table", zebra_stripes=True)
            with TabPane("CAN", id="can"):
                with Horizontal(id="can-controls"):
                    yield Checkbox("CAN1", value=True, id="can-1", disabled=True)
                    yield Checkbox("CAN2", value=True, id="can-2", disabled=True)
                    yield Checkbox("CAN3", value=True, id="can-3", disabled=True)
                    yield Button("Start", id="can-start", variant="primary", disabled=True)
                    yield Button("Stop", id="can-stop", disabled=True)
                    yield Static("Unavailable", id="can-summary")
                yield DataTable(id="can-table", zebra_stripes=True)
            with TabPane("IMU", id="imu"):
                with Horizontal(id="imu-controls"):
                    yield Button(
                        "Calibrate", id="imu-calibrate", variant="primary", disabled=True
                    )
                    yield Button("Cancel", id="imu-cancel", disabled=True)
                    yield Static("Unavailable", id="imu-calibration")
                yield ProgressBar(total=30000, show_eta=False, id="imu-progress")
                yield Static("No IMU data", id="imu-values")
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#log-table", DataTable)
        table.cursor_type = "row"
        table.add_columns("Sequence", "Boot", "Uptime", "Level", "Task", "Message")
        can_table = self.query_one("#can-table", DataTable)
        can_table.cursor_type = "row"
        for label, key in (
            ("Bus", "bus"), ("ID", "id"), ("Count", "count"),
            ("Frequency", "frequency"), ("Time", "time"),
            ("Length", "length"), ("Data", "data"),
        ):
            can_table.add_column(label, key=key)
        self._connection_timer = self.set_interval(
            self.CONNECTION_CHECK_INTERVAL,
            self._poll_connection,
            pause=True,
        )
        self._live_timer = self.set_interval(
            self.LIVE_UPDATE_INTERVAL, self._poll_live, pause=True
        )
        self._update_layout(self.size.width)
        if self._auto_scan:
            self.scan_devices()

    def on_resize(self, event: Resize) -> None:
        self._update_layout(event.size.width)

    def on_unmount(self) -> None:
        if self._connection_timer is not None:
            self._connection_timer.stop()
        if self._live_timer is not None:
            self._live_timer.stop()
        self._close_transport()

    def exit(
        self,
        result: None = None,
        return_code: int = 0,
        message: RenderableType | None = None,
    ) -> None:
        self._close_transport()
        super().exit(result, return_code, message)

    def _close_transport(self) -> None:
        transport = self._transport
        self._transport = None
        self._client = None
        self._can_client = None
        self._imu_client = None
        if transport is not None:
            try:
                transport.close()
            except Exception:
                pass
        self._can_running = False
        self._imu_ready = False

    def action_scan(self) -> None:
        if not self.query_one("#scan", Button).disabled:
            self.scan_devices()

    def action_connect(self) -> None:
        if not self.query_one("#connect", Button).disabled:
            self.connect_selected()

    def action_load_logs(self) -> None:
        if not self.query_one("#load-logs", Button).disabled:
            self.load_logs()

    @on(Button.Pressed, "#scan")
    def scan_pressed(self) -> None:
        self.scan_devices()

    @on(Button.Pressed, "#connect")
    def connect_pressed(self) -> None:
        self.connect_selected()

    @on(Button.Pressed, "#disconnect")
    def disconnect_pressed(self) -> None:
        if self._connection_timer is not None:
            self._connection_timer.pause()
        self.query_one("#disconnect", Button).disabled = True
        self.disconnect_device()

    @on(Button.Pressed, "#refresh-info")
    def refresh_info_pressed(self) -> None:
        self.refresh_info()

    @on(Button.Pressed, "#load-logs")
    def load_logs_pressed(self) -> None:
        self.load_logs()

    @on(Button.Pressed, "#save-logs")
    def save_logs_pressed(self) -> None:
        self.push_screen(SaveLogScreen(), self._save_log)

    @on(Button.Pressed, "#clear-logs")
    def clear_logs_pressed(self) -> None:
        self.push_screen(ConfirmClearScreen(), self._clear_confirmed)

    @on(Button.Pressed, "#can-start")
    def can_start_pressed(self) -> None:
        mask = sum(
            1 << index
            for index in range(3)
            if self.query_one(f"#can-{index + 1}", Checkbox).value
        )
        if mask:
            self.start_can(mask)

    @on(Button.Pressed, "#can-stop")
    def can_stop_pressed(self) -> None:
        self.stop_can()

    @on(Button.Pressed, "#imu-calibrate")
    def imu_calibrate_pressed(self) -> None:
        self.start_imu_calibration()

    @on(Button.Pressed, "#imu-cancel")
    def imu_cancel_pressed(self) -> None:
        self.cancel_imu_calibration()

    @on(Select.Changed, "#level-filter")
    @on(Select.Changed, "#boot-filter")
    @on(Input.Changed, "#search-filter")
    def filter_changed(self) -> None:
        self._reset_boot_groups()
        self._render_records()

    @on(DataTable.RowSelected, "#log-table")
    def log_group_selected(self, event: DataTable.RowSelected) -> None:
        row_key = event.row_key.value
        if row_key is None or not row_key.startswith("boot:"):
            return
        boot_id = int(row_key.removeprefix("boot:"))
        if boot_id in self._collapsed_boots:
            self._collapsed_boots.remove(boot_id)
        else:
            self._collapsed_boots.add(boot_id)
        self._render_records()

    def scan_devices(self) -> None:
        if self._transport is not None:
            self.notify("Disconnect before scanning", severity="warning")
            return
        self.query_one("#scan", Button).disabled = True
        self.query_one("#connect", Button).disabled = True
        self.query_one("#device-select", Select).disabled = True
        self._set_status("Scanning USB devices...")
        self._scan_devices_worker()

    @work(thread=True, exclusive=True, group="scan")
    def _scan_devices_worker(self) -> None:
        try:
            devices = self._discoverer()
        except Exception as error:
            self.call_from_thread(self._scan_failed, error)
            return
        self.call_from_thread(self._set_devices, devices)

    def _scan_failed(self, error: Exception) -> None:
        self.query_one("#scan", Button).disabled = False
        self.query_one("#connect", Button).disabled = False
        self.query_one("#device-select", Select).disabled = False
        self._operation_failed("Scan", error)

    def _set_devices(self, devices: list[DeviceInfo]) -> None:
        self._devices = {
            f"{device.vid:04x}:{device.pid:04x}:{device.serial}:{index}": device
            for index, device in enumerate(devices)
        }
        selector = self.query_one("#device-select", Select)
        selector.set_options(
            [(device.label, key) for key, device in self._devices.items()]
        )
        selector.disabled = False
        self.query_one("#scan", Button).disabled = False
        self.query_one("#connect", Button).disabled = False
        if not devices:
            self._set_status("No TRobot device found")
        elif len(devices) == 1:
            selector.value = next(iter(self._devices))
            self._set_status("Device found")
        else:
            self._set_status(f"{len(devices)} devices found")

    def connect_selected(self) -> None:
        if self._transport is not None:
            return
        selected = self.query_one("#device-select", Select).value
        if not isinstance(selected, str) or selected not in self._devices:
            self.notify("Select a device first", severity="warning")
            return
        self.query_one("#scan", Button).disabled = True
        self.query_one("#connect", Button).disabled = True
        self.query_one("#device-select", Select).disabled = True
        self._set_connection_state("CONNECTING", "#e5b567")
        self._set_status(f"Connecting to {self._devices[selected].label}...")
        self.connect_device(self._devices[selected])

    @work(thread=True, exclusive=True, group="connection")
    def connect_device(self, device: DeviceInfo) -> None:
        try:
            transport = self._connector(device)
        except Exception as error:
            self.call_from_thread(self._connect_failed, error)
            return

        client = LogClient(transport)
        can_client = CanClient(transport)
        imu_client = ImuClient(transport)
        probe_error: Exception | None = None
        try:
            info = client.info()
        except Exception as error:
            info = None
            probe_error = error
        try:
            can_status = can_client.status()
        except Exception as error:
            can_status = None
            if probe_error is None:
                probe_error = error
        try:
            imu_status = imu_client.status()
        except Exception as error:
            imu_status = None
            if probe_error is None:
                probe_error = error
        if info is None and can_status is None and imu_status is None:
            try:
                transport.close()
            except Exception:
                pass
            self.call_from_thread(
                self._connect_failed,
                probe_error or RuntimeError("No debug service responded"),
            )
            return
        self.call_from_thread(
            self._set_connected,
            device,
            transport,
            client,
            info,
            can_client,
            can_status,
            imu_client,
            imu_status,
        )

    def _connect_failed(self, error: Exception) -> None:
        self.query_one("#scan", Button).disabled = False
        self.query_one("#connect", Button).disabled = False
        self.query_one("#device-select", Select).disabled = False
        self._set_connection_state("OFFLINE", "#e5b567")
        if "timed out" in str(error).lower():
            self._set_status("No debug response. Reboot with KEY held low for 100 ms.")
            self.notify("Device is not responding in debug mode", severity="error")
        else:
            self._operation_failed("Connect", error)

    def _set_connected(
        self,
        device: DeviceInfo,
        transport: Transport,
        client: LogClient,
        info: LogInfo | None,
        can_client: CanClient,
        can_status: CanStatus | None,
        imu_client: ImuClient,
        imu_status: ImuStatus | None,
    ) -> None:
        if self._transport is not None:
            self._transport.close()
        self._transport = transport
        self._client = client
        self._can_client = can_client if can_status is not None else None
        self._imu_client = imu_client if imu_status is not None else None
        self.query_one("#scan", Button).disabled = True
        self.query_one("#device-select", Select).disabled = True
        self.query_one("#connect", Button).disabled = True
        self.query_one("#disconnect", Button).disabled = False
        for selector in ("#refresh-info", "#load-logs", "#clear-logs"):
            self.query_one(selector, Button).disabled = info is None
        self._set_connection_state("ONLINE", "#62d3a4")
        self._set_status(f"Connected to {device.label}")
        if info is not None:
            self._show_info(info)
        else:
            self._reset_metrics()
        if can_status is not None:
            self._show_can_status(can_status)
        if imu_status is not None:
            self._show_imu_status(imu_status)
        if self._connection_timer is not None:
            self._connection_timer.resume()
        if self._live_timer is not None:
            self._live_timer.resume()

    @work(thread=True, exclusive=True, group="connection")
    def disconnect_device(self) -> None:
        transport = self._transport
        if transport is not None:
            try:
                transport.close()
            except Exception as error:
                self.call_from_thread(self._operation_failed, "Disconnect", error)
        self.call_from_thread(self._set_disconnected)

    def _set_disconnected(self) -> None:
        if self._connection_timer is not None:
            self._connection_timer.pause()
        if self._live_timer is not None:
            self._live_timer.pause()
        self._transport = None
        self._client = None
        self._can_client = None
        self._imu_client = None
        self._can_running = False
        self._imu_ready = False
        self._live_request_pending = False
        self.query_one("#scan", Button).disabled = False
        self.query_one("#device-select", Select).disabled = False
        self.query_one("#connect", Button).disabled = False
        self.query_one("#disconnect", Button).disabled = True
        for selector in ("#refresh-info", "#load-logs", "#clear-logs"):
            self.query_one(selector, Button).disabled = True
        self.query_one("#save-logs", Button).disabled = not bool(self._raw_log)
        for index in range(1, 4):
            self.query_one(f"#can-{index}", Checkbox).disabled = True
        self.query_one("#can-start", Button).disabled = True
        self.query_one("#can-stop", Button).disabled = True
        self.query_one("#imu-calibrate", Button).disabled = True
        self.query_one("#imu-cancel", Button).disabled = True
        self.query_one("#can-summary", Static).update("Unavailable")
        self.query_one("#imu-calibration", Static).update("Unavailable")
        self._set_connection_state("OFFLINE", "#e5b567")
        self._reset_metrics()
        self._set_status("Disconnected")

    def _poll_connection(self) -> None:
        transport = self._transport
        if transport is None or self._connection_check_transport is not None:
            return
        self._connection_check_transport = transport
        self._check_connection(transport)

    @work(thread=True, group="connection-monitor")
    def _check_connection(self, transport: Transport) -> None:
        try:
            transport.ping()
        except Exception as error:
            try:
                transport.close()
            except Exception:
                pass
            self.call_from_thread(self._connection_lost, transport, error)
            return
        self.call_from_thread(self._connection_check_finished, transport)

    def _connection_check_finished(self, transport: Transport) -> None:
        if self._connection_check_transport is transport:
            self._connection_check_transport = None

    def _connection_lost(self, transport: Transport, error: Exception) -> None:
        self._connection_check_finished(transport)
        if self._transport is not transport:
            return
        self._set_disconnected()
        self._set_status(f"Connection lost: {error}")
        self.notify("USB device disconnected", severity="error")

    @work(thread=True, exclusive=True, group="can-control")
    def start_can(self, mask: int) -> None:
        client = self._can_client
        if client is None:
            return
        try:
            client.start(mask)
            status = client.status()
        except Exception as error:
            self.call_from_thread(self._operation_failed, "Start CAN capture", error)
            return
        self.call_from_thread(self._can_started, status)

    def _can_started(self, status: CanStatus) -> None:
        self._can_running = True
        self._can_frames.clear()
        self._can_rows.clear()
        self.query_one("#can-table", DataTable).clear(columns=False)
        self.query_one("#can-start", Button).disabled = True
        self.query_one("#can-stop", Button).disabled = False
        for index in range(1, 4):
            self.query_one(f"#can-{index}", Checkbox).disabled = True
        self._show_can_status(status)

    @work(thread=True, exclusive=True, group="can-control")
    def stop_can(self) -> None:
        client = self._can_client
        if client is None:
            return
        try:
            client.stop()
            status = client.status()
        except Exception as error:
            self.call_from_thread(self._operation_failed, "Stop CAN capture", error)
            return
        self.call_from_thread(self._can_stopped, status)

    def _can_stopped(self, status: CanStatus) -> None:
        self._can_running = False
        self.query_one("#can-start", Button).disabled = status.available_mask == 0
        self.query_one("#can-stop", Button).disabled = True
        self._show_can_status(status)

    @work(thread=True, exclusive=True, group="imu-control")
    def start_imu_calibration(self) -> None:
        client = self._imu_client
        if client is None:
            return
        try:
            client.start_calibration()
            status = client.status()
        except Exception as error:
            self.call_from_thread(self._operation_failed, "Start IMU calibration", error)
            return
        self.call_from_thread(self._show_imu_status, status)

    @work(thread=True, exclusive=True, group="imu-control")
    def cancel_imu_calibration(self) -> None:
        client = self._imu_client
        if client is None:
            return
        try:
            client.cancel_calibration()
            status = client.status()
        except Exception as error:
            self.call_from_thread(self._operation_failed, "Cancel IMU calibration", error)
            return
        self.call_from_thread(self._show_imu_status, status)

    def _poll_live(self) -> None:
        if self._live_request_pending or self._transport is None:
            return
        self._live_request_pending = True
        self._read_live()

    @work(thread=True, group="live")
    def _read_live(self) -> None:
        can_read = None
        can_status = None
        imu_state = None
        imu_status = None
        try:
            self._live_cycle += 1
            if self._can_running and self._can_client is not None and (
                self._live_cycle % 5 == 0
            ):
                can_read = self._can_client.read()
            if self._can_running and self._can_client is not None and (
                self._live_cycle % 20 == 0
            ):
                can_status = self._can_client.status()
            if self._imu_client is not None:
                if self._live_cycle % 4 == 0:
                    imu_status = self._imu_client.status()
                if self._imu_ready or (imu_status is not None and imu_status.ins_ready):
                    imu_state = self._imu_client.state()
        except Exception:
            pass
        self.call_from_thread(
            self._live_updated, can_read, can_status, imu_state, imu_status
        )

    def _live_updated(
        self,
        can_read: CanRead | None,
        can_status: CanStatus | None,
        imu_state: ImuState | None,
        imu_status: ImuStatus | None,
    ) -> None:
        self._live_request_pending = False
        if can_read is not None:
            self._show_can_frames(can_read)
        if can_status is not None:
            self._show_can_status(can_status)
        if imu_state is not None:
            self._show_imu_state(imu_state)
        if imu_status is not None:
            self._show_imu_status(imu_status)

    def _show_can_status(self, status: CanStatus) -> None:
        for index in range(3):
            checkbox = self.query_one(f"#can-{index + 1}", Checkbox)
            available = bool(status.available_mask & 1 << index)
            checkbox.disabled = self._can_running or not available
            if not available:
                checkbox.value = False
        self.query_one("#can-start", Button).disabled = (
            self._can_running or status.available_mask == 0
        )
        self.query_one("#can-stop", Button).disabled = not self._can_running
        self._can_missed = sum(bus.missed for bus in status.buses)
        self._can_fifo_lost = sum(bus.fifo_lost for bus in status.buses)
        self._can_bus_rate = sum(bus.frequency_hz for bus in status.buses)
        self.query_one("#can-summary", Static).update(
            f"IDs {len(self._can_frames)} | bus {self._can_bus_rate:.1f} Hz | "
            f"not shown {self._can_missed} | "
            f"FIFO lost {self._can_fifo_lost}"
        )

    def _show_can_frames(self, result: CanRead) -> None:
        table = self.query_one("#can-table", DataTable)
        updated = set()
        for frame in result.frames:
            key = (frame.bus, frame.can_id)
            self._can_frames[key] = frame
            updated.add(key)

        for key in updated:
            frame = self._can_frames[key]
            row_key = f"{frame.bus}:{frame.can_id}"
            cells = (
                f"CAN{frame.bus + 1}",
                f"0x{frame.can_id:03X}",
                str(frame.count),
                f"{frame.frequency_hz:.1f} Hz",
                str(frame.timestamp_ms),
                str(len(frame.data)),
                frame.data.hex(" ").upper(),
            )
            if key not in self._can_rows:
                table.add_row(*cells, key=row_key)
                self._can_rows.add(key)
            else:
                for column, value in zip(
                    (
                        "bus", "id", "count", "frequency",
                        "time", "length", "data",
                    ),
                    cells,
                ):
                    table.update_cell(row_key, column, value, update_width=True)
        self._can_missed = sum(result.missed)
        self.query_one("#can-summary", Static).update(
            f"IDs {len(self._can_frames)} | bus {self._can_bus_rate:.1f} Hz | "
            f"not shown {self._can_missed} | "
            f"FIFO lost {self._can_fifo_lost}"
        )

    def _show_imu_status(self, status: ImuStatus) -> None:
        self._imu_ready = status.ins_ready
        running = status.calibration_state.name in ("RUNNING", "SAVING")
        self.query_one("#imu-calibrate", Button).disabled = (
            not status.ins_ready or running
        )
        self.query_one("#imu-cancel", Button).disabled = (
            status.calibration_state.name != "RUNNING"
        )
        self.query_one("#imu-progress", ProgressBar).update(
            total=max(status.calibration_duration_ms, 1),
            progress=status.calibration_progress_ms,
        )
        readiness = (
            "ready" if status.ins_ready else
            "starting" if status.hardware_ready else
            "hardware error"
        )
        saved = "saved" if status.calibration_persisted else "not saved"
        error = (
            "" if status.calibration_error.name == "NONE" else
            f" | error {status.calibration_error.name.lower()}"
        )
        self.query_one("#imu-calibration", Static).update(
            f"{readiness} | {status.calibration_state.name.lower()} | {saved}{error} | "
            f"bias [{', '.join(f'{value:+.6f}' for value in status.gyro_correction)}]"
        )

    def _show_imu_state(self, state: ImuState) -> None:
        vector = lambda values: "  ".join(f"{value:+.6f}" for value in values)
        self.query_one("#imu-values", Static).update(
            f"Raw accel    {vector(state.raw_accel)} m/s²\n"
            f"Raw gyro     {vector(state.raw_gyro)} rad/s\n"
            f"Accel        {vector(state.accel)} m/s²\n"
            f"Gyro         {vector(state.gyro)} rad/s\n"
            f"Quaternion   {vector(state.quaternion)}\n"
            f"Euler        roll {math.degrees(state.roll):+.3f}°  "
            f"pitch {math.degrees(state.pitch):+.3f}°  "
            f"yaw {math.degrees(state.yaw):+.3f}°\n"
            f"Yaw total    {math.degrees(state.yaw_total):+.3f}°\n"
            f"Temperature  {state.temperature:.2f} °C\n"
            f"Converged    {state.converged}    Stationary {state.stationary}"
        )

    @work(thread=True, exclusive=True, group="logs")
    def refresh_info(self) -> None:
        client = self._client
        if client is None:
            return
        try:
            info = client.info()
        except Exception as error:
            self.call_from_thread(self._operation_failed, "Info", error)
            return
        self.call_from_thread(self._show_info, info)

    def _show_info(self, info: LogInfo) -> None:
        used_percent = (info.used_size * 100 / info.total_size) if info.total_size else 0
        storage = self.query_one("#metric-storage", Static)
        storage.update("READY" if info.storage_ready else "ERROR")
        storage.styles.color = "#62d3a4" if info.storage_ready else "#ef6f6c"
        self.query_one("#metric-used", Static).update(
            f"{format_size(info.used_size)}  {used_percent:.1f}%"
        )
        self.query_one("#metric-boot", Static).update(str(info.boot_id))
        self.query_one("#metric-sequence", Static).update(str(info.next_sequence))
        if self._transport is not None:
            self._set_status(
                f"Storage {format_size(info.used_size)} / {format_size(info.total_size)}"
                f" | {info.queued_records} queued"
            )

    @work(thread=True, exclusive=True, group="logs")
    def load_logs(self) -> None:
        client = self._client
        if client is None:
            return
        self.call_from_thread(self._show_progress, 0, 1)
        try:
            data = client.download(
                lambda current, total: self.call_from_thread(
                    self._show_progress, current, total
                )
            )
            records = parse_records(data)
            info = client.info()
        except Exception as error:
            self.call_from_thread(self._hide_progress)
            self.call_from_thread(self._operation_failed, "Load logs", error)
            return
        self.call_from_thread(self._logs_loaded, data, records, info)

    def _logs_loaded(
        self,
        data: bytes,
        records: list[LogRecord],
        info: LogInfo,
    ) -> None:
        self._raw_log = data
        self._records = records
        self._update_boot_filter(records)
        self._reset_boot_groups()
        self._hide_progress()
        self._show_info(info)
        self._render_records()
        self.query_one("#save-logs", Button).disabled = False
        self.notify(f"Loaded {len(records)} records")

    def _render_records(self) -> None:
        level_value = self.query_one("#level-filter", Select).value
        level = level_value if isinstance(level_value, str) else "all"
        boot_value = self.query_one("#boot-filter", Select).value
        boot_id = boot_value if isinstance(boot_value, int) else None
        search = self.query_one("#search-filter", Input).value
        records = filter_records(self._records, level, boot_id, search)
        table = self.query_one("#log-table", DataTable)
        table.clear(columns=False)
        level_styles = {
            "ERROR": "bold #ef6f6c",
            "WARNING": "bold #e5b567",
            "INFO": "bold #67b7d1",
        }
        for boot_id, boot_records in group_records_by_boot(records):
            collapsed = boot_id in self._collapsed_boots
            marker = "▸" if collapsed else "▾"
            table.add_row(
                "",
                Text(f"{marker} {boot_id}", style="bold #62d3a4"),
                "",
                "",
                "",
                Text(f"{len(boot_records)} records", style="#879397"),
                key=f"boot:{boot_id}",
            )
            if collapsed:
                continue
            for index, record in enumerate(boot_records):
                level_name = record.level.name
                message = Text(record.message)
                if record.truncated:
                    message.append("  TRUNCATED", style="bold #e5b567")
                table.add_row(
                    str(record.sequence),
                    "",
                    format_uptime(record.uptime_ms),
                    Text(level_name, style=level_styles[level_name]),
                    record.task_name,
                    message,
                    key=f"record:{record.boot_id}:{record.sequence}:{index}",
                )

    def _save_log(self, result: tuple[pathlib.Path, str] | None) -> None:
        if result is None:
            return
        path, output_format = result
        try:
            save_log(self._raw_log, output_format, path)
        except OSError as error:
            self._operation_failed("Save", error)
            return
        self.notify(f"Saved {path}")

    def _clear_confirmed(self, confirmed: bool) -> None:
        if confirmed:
            self.clear_logs()

    @work(thread=True, exclusive=True, group="logs")
    def clear_logs(self) -> None:
        client = self._client
        if client is None:
            return
        self.call_from_thread(self._set_status, "Clearing logs...")
        try:
            client.clear()
            info = client.info()
        except Exception as error:
            self.call_from_thread(self._operation_failed, "Clear", error)
            return
        self.call_from_thread(self._logs_cleared, info)

    def _logs_cleared(self, info: LogInfo) -> None:
        self._raw_log = b""
        self._records = []
        self._collapsed_boots.clear()
        self._update_boot_filter([])
        self._render_records()
        self._show_info(info)
        self.query_one("#save-logs", Button).disabled = True
        self._set_status("Logs cleared")
        self.notify("Logs cleared")

    def _show_progress(self, current: int, total: int) -> None:
        progress = self.query_one("#download-progress", ProgressBar)
        progress.remove_class("hidden")
        progress.update(total=max(total, 1), progress=current)

    def _hide_progress(self) -> None:
        self.query_one("#download-progress", ProgressBar).add_class("hidden")

    def _update_boot_filter(self, records: Iterable[LogRecord]) -> None:
        boot_filter = self.query_one("#boot-filter", Select)
        selected = boot_filter.value
        boot_ids = sorted({record.boot_id for record in records}, reverse=True)
        boot_filter.set_options(
            [
                ("All boots", "all"),
                *[(str(boot_id), boot_id) for boot_id in boot_ids],
            ]
        )
        if isinstance(selected, int) and selected in boot_ids:
            boot_filter.value = selected

    def _reset_boot_groups(self) -> None:
        boot_ids = {record.boot_id for record in self._records}
        level = self.query_one("#level-filter", Select).value
        boot = self.query_one("#boot-filter", Select).value
        search = self.query_one("#search-filter", Input).value.strip()
        filtered = level != "all" or isinstance(boot, int) or bool(search)
        self._collapsed_boots = set() if filtered else boot_ids
        if boot_ids:
            self._collapsed_boots.discard(max(boot_ids))

    def _set_status(self, message: str) -> None:
        self.query_one("#connection-status", Static).update(message)

    def _set_connection_state(self, state: str, color: str) -> None:
        indicator = self.query_one("#connection-state", Static)
        indicator.update(state)
        indicator.styles.color = color

    def _update_layout(self, width: int) -> None:
        self.screen.set_class(width < 112, "-narrow")

    def _reset_metrics(self) -> None:
        storage = self.query_one("#metric-storage", Static)
        storage.update("OFFLINE")
        storage.styles.color = "#879397"
        for selector in ("#metric-used", "#metric-boot", "#metric-sequence"):
            self.query_one(selector, Static).update("--")

    def _operation_failed(self, operation: str, error: Exception) -> None:
        message = f"{operation} failed: {error}"
        self._set_status(message)
        self.notify(message, severity="error")


def run() -> None:
    TRobotTui().run()
