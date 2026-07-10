# Repository Guidelines

## Project Structure & Module Organization

TRobot is an STM32H723 firmware framework built with CMake, FreeRTOS, STM32 HAL, and C++ components. CubeMX-generated code stays in `Core/`, `Drivers/`, `Middlewares/`, `USB_DEVICE/`, and `cmake/stm32cubemx/`. The generated FreeRTOS `entrance` thread calls C++ `app_entrance()` in `app/main/main.cc`; keep application task orchestration and robot behavior under `app/`.

Hardware ownership belongs in `bsp/`: public C headers are in `bsp/include/bsp/`, implementations in `bsp/src/`, private helpers in `bsp/internal/`, and bundled ports such as SEGGER, EasyFlash, USB, and W25Q64 remain in their existing folders. Reusable modules live in `components/<name>/` with `include/<name>/`, `src/`, optional `internal/`, and a `CMakeLists.txt` exporting `components::<name>`.

## Build, Test, and Development Commands

- `git clone --recursive <repo>`: clone required component submodules.
- `cmake --preset Debug`: configure the Ninja Debug build with `cmake/gcc-arm-none-eabi.cmake`.
- `cmake --build --preset Debug`: build firmware; `app` uses `-Werror`.
- `cmake --build --preset Release`: build the optimized preset.
- `cmake --build --preset Debug --target flash_and_verify`: flash and verify through OpenOCD using `stlink.cfg`.

Use CLion CMake preset profiles. After CubeMX regeneration, check whether `STM32H723XG_FLASH.ld` was overwritten.

## Codex Execution Policy

For Codex sessions in this repository, do static inspection only. Do not run firmware builds, `make`, CMake/Ninja build commands, OpenOCD, flash, or verify targets. Build and hardware verification are handled by other models or by humans.

## Coding Style & Naming Conventions

Use C17 for BSP/generated C and C++23 for application/components. Keep four-space indentation, concise functions, and descriptive `snake_case` names. BSP APIs use `bsp_*` and C enums such as `E_CAN_1` or `E_UART_1`; keep HAL, FreeRTOS, SEGGER, CubeMX, and third-party names unchanged. Components use namespaces such as `os`, `motor`, `rc::dr16`, `ins`, and `robot::chassis`, with small classes or structs for typed configuration.

Prefer typed constants, `static constexpr`, local `static` helpers, and `BSP_ASSERT` for invariants. Comments should explain hardware timing, units, protocol layouts, calibration, concurrency, or DMA/cache constraints.

## RTOS & Embedded Design Guidelines

Initialize board hardware through `bsp_hw_init()` before starting application tasks. Create tasks from `app_entrance()` using `os::task::static_create()` or FreeRTOS APIs, and keep watchdog refresh paths explicit. Use `os::task::sleep()` or `vTaskDelayUntil()` for periodic work; reserve busy microsecond waits for short hardware timing.

Keep ISRs and HAL callbacks short. Defer work through callbacks, queues, notifications, or component state updates. Protect shared ISR/task state with `bsp/sys.h` critical-section helpers. DMA-visible buffers may need `_ram_d1`; review lifetime and cache behavior before changing UART, CAN, SPI, USB, IMU, or flash paths.

## CubeMX, Testing, and PRs

Treat `trobot.ioc` as the source for peripheral, clock, DMA, NVIC, and FreeRTOS configuration. Keep custom logic out of generated files unless inside user sections. BSP drivers should expose narrow C wrappers and hide HAL handles, buffers, filters, and callbacks in `bsp/src/`.

No standalone automated test harness is present. For firmware changes, run a clean CMake build and document board verification: flash result, OpenOCD verify status, terminal output, sensor data, CAN/UART traffic, or motor response.

History uses `feat:`, `fix:`, `chore:`, and `doc:` prefixes, sometimes with scopes like `feat (ins):`. PRs should include a summary, affected modules, build results, hardware verification status, and linked issues when applicable.
