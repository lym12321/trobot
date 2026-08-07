# SEGGER SystemView

This directory contains the bundled target sources required for TRobot tracing:

- SEGGER SystemView V4.12.0
- SEGGER RTT V8.58.0
- SEGGER's FreeRTOS V10.4 adapter, used with the project's FreeRTOS 10.6.2 legacy trace hooks

The snapshots come from the corresponding releases in the official
`SEGGERMicro/SystemView` and `SEGGERMicro/RTT` GitHub repositories. Their
licenses are retained under `systemview/` and `rtt/`.

Only the RTT core, SystemView core, and FreeRTOS adapter are compiled. RTT
printf, syscall retargeting, assembly acceleration, examples, and FreeRTOS port
patches are intentionally excluded. TRobot-owned configuration lives in
`../../internal/segger/`, and initialization lives in
`../../src/segger_trace.c`.

The adapter has one local configuration patch: its fixed task table size is
guarded with `SYSVIEW_FREERTOS_MAX_NOF_TASKS`, allowing CMake to set the TRobot
limit to 16 without changing SystemView protocol behavior.

Enable tracing with `-DTROBOT_ENABLE_SYSTEMVIEW=ON`. CMake force-includes the
project-owned `../../internal/segger/freertos_trace_config.h` bridge while
compiling the FreeRTOS target, so CubeMX-generated files do not require changes.
Recording remains stopped until the SystemView host sends a start command. ISR
timeline instrumentation is not enabled because the CubeMX-managed FreeRTOS
port is left unchanged.
