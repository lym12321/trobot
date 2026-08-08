# TRobot Host Tools

The host tools are an independent Python project managed with `uv`.

```powershell
cd tools
uv sync
uv run python trobot.py
```

The device must be booted in debug mode. Hold KEY low continuously for the first 100 ms
after startup. The TUI has separate log, CAN, and IMU workspaces. The firmware aggregates
CAN traffic by bus and standard ID; the TUI shows low-rate snapshots with frequency,
recent-window frequency, untracked-frame, and hardware FIFO-loss statistics.
The IMU workspace shows raw and corrected sensor values,
attitude output, convergence state, and calibration progress.

The CLI uses the same transport and protocol modules:

```powershell
uv run python trobot.py info
uv run python trobot.py log info
uv run python trobot.py log dump --format jsonl -o logs.jsonl
uv run python trobot.py log clear --yes
uv run python trobot.py can status
uv run python trobot.py can watch --buses 0x7
uv run python trobot.py imu status
uv run python trobot.py imu watch
uv run python trobot.py imu calibrate
```

Run host tests with:

```powershell
uv run python -m unittest discover -s tests -v
```

`trobot_host.protocol` owns the generic `TDBG` framing and payload decoders,
`trobot_host.transport` owns PyUSB/libusb access, and the three service clients provide
the log, CAN, and IMU operations shared by the TUI and CLI.
