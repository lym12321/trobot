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

### OpenOCD GDB Debugging

- Before starting OpenOCD, confirm whether the connected debug probe is DAPLink/CMSIS-DAP or ST-Link; do not infer the probe type from prior sessions or the default flash target.
- Start the GDB server from the repository root with `openocd -f daplink.cfg` for DAPLink/CMSIS-DAP, or `openocd -f stlink.cfg` for ST-Link.
- In another terminal, run `arm-none-eabi-gdb build/Debug/trobot.elf`, then connect with `target extended-remote localhost:3333`.
- Use `monitor reset halt` to reproduce initialization paths, set breakpoints, and run `continue`. When leaving a halted session, run `monitor resume` followed by `detach` so the target keeps running.
- Ensure the ELF matches the firmware on the target. Starting a debug session does not authorize `load`, `monitor program`, or any flash operation; those require an explicit user request.

Use CLion CMake preset profiles. After CubeMX regeneration, check whether `STM32H723XG_FLASH.ld` was overwritten.

## Codex Execution Policy

Before build verification, every agent MUST determine whether it is running as Codex. A Codex agent MUST then inspect the active permission profile. If the session has Full Access (`danger-full-access` or an equivalent unrestricted profile), it MUST run the relevant CMake build and report the result. If the session is sandboxed, restricted, or the permission level cannot be confirmed, it MUST NOT run firmware builds, `make`, or CMake/Ninja build commands, and MUST tell the user that build verification was skipped because Full Access is unavailable. Build permission does not authorize OpenOCD, flash, or hardware verify targets; those still require an explicit user request.

## Coding Style & Naming Conventions

Use C17 for BSP/generated C and C++23 for application/components. Keep four-space indentation, concise functions, and descriptive `snake_case` names. BSP APIs use `bsp_*` and C enums such as `E_CAN_1` or `E_UART_1`; keep HAL, FreeRTOS, SEGGER, CubeMX, and third-party names unchanged. Components use namespaces such as `os`, `motor`, `rc::dr16`, `ins`, and `robot::chassis`, with small classes or structs for typed configuration.

Prefer typed constants, `static constexpr`, local `static` helpers, and `BSP_ASSERT` for invariants. Keep comments minimal — only for hardware timing, units, protocol layouts, calibration, concurrency, or DMA/cache constraints. Do not add section-header banners, ASCII art, "Created by" blocks, or explanatory comments for self-documenting code. When in doubt, omit the comment.

## RTOS & Embedded Design Guidelines

Initialize board hardware through `bsp_hw_init()` before starting application tasks. Create tasks from `app_entrance()` using `os::task::static_create()` or FreeRTOS APIs, and keep watchdog refresh paths explicit. Use `os::task::sleep()` or `vTaskDelayUntil()` for periodic work; reserve busy microsecond waits for short hardware timing.

Keep ISRs and HAL callbacks short. Defer work through callbacks, queues, notifications, or component state updates. Protect shared ISR/task state with `bsp/sys.h` critical-section helpers. DMA-visible buffers must be placed in a DMA-accessible RAM domain; review lifetime and cache behavior before changing UART, CAN, SPI, USB, IMU, or flash paths.

## CubeMX, Testing, and PRs

Treat `trobot.ioc` as the source for peripheral, clock, DMA, NVIC, and FreeRTOS configuration. Keep custom logic out of generated files unless inside user sections. BSP drivers should expose narrow C wrappers and hide HAL handles, buffers, filters, and callbacks in `bsp/src/`.

**CubeMX modification boundary**: Changes that require adjusting CubeMX-generated content — including `FreeRTOSConfig.h`, interrupt priorities in NVIC, peripheral pin assignments, clock tree, DMA streams, linker script memory regions, or any `.ioc`-managed setting — MUST NOT be made directly by the agent. Instead, describe the required CubeMX change and ask the user to apply it manually through the CubeMX IDE. Direct edits to `trobot.ioc`, generated `Core/` files outside `USER CODE` sections, `Drivers/`, `Middlewares/`, `USB_DEVICE/`, or `startup_stm32h723xx.s` are PROHIBITED.

No standalone automated test harness is present. For firmware changes, run a clean CMake build and document board verification: flash result, OpenOCD verify status, terminal output, sensor data, CAN/UART traffic, or motor response.

History uses `feat:`, `fix:`, `chore:`, and `doc:` prefixes, sometimes with scopes like `feat (ins):`. PRs should include a summary, affected modules, build results, hardware verification status, and linked issues when applicable.

## Agent Conventions

### Modification Discipline

Do not rewrite or restructure code speculatively. Every change must follow the existing intent, naming, formatting, and architectural patterns of the file it touches. Before making a change that alters more than a few localized lines — or that changes interfaces, deletes user-authored comments, renames symbols, or introduces new abstractions — ask the user whether the change is desired. When in doubt, ask.

### Submodule Synchronization

- **URL protocol**: Submodule URLs in `.gitmodules` MUST use HTTPS, never SSH.
- **Local iteration**: commit only the submodule (`components/<name>`); do NOT sync the trobot superproject. Avoids noise commits like `chore(<name>): update submodule`.
- **Remote push**: after pushing the submodule, MUST update the submodule pointer in trobot (`git add components/<name>`, commit, push). The remote depends on the pointer to resolve the submodule version.

### Default Configuration in main.cc

`logger::init` and `terminal::init` in `app/main/main.cc` are **commented out by default**. Uncomment them during debugging, but keep them commented when committing. Prevents debug output from occupying the serial port in production firmware.

### Commit Attribution

Commit messages MUST be written in Chinese. Use the format `<type>(<scope>): <中文简述>`, e.g. `fix(bsp): 修复 CAN 发送临界区过长问题`. The agent marker `[agent: <model>]` goes on a separate line after the body, e.g.:

```
fix(bsp): 替换运行时 BSP_ASSERT 为错误返回

[agent: deepseek/deepseek-v4-pro]
```

### Modification Review

After every modification, the agent MUST review the changes for correctness before yielding. The review scope includes but is not limited to:
- Code logic and invariants
- API contract consistency (callers updated, signatures match)
- Concurrency correctness (ISR/task shared state)
- Memory safety (buffer sizes, DMA alignment, RAM domain placement)
- Runtime `BSP_ASSERT` misuse (assert only for init-time errors, never runtime)

Non-Codex agents MUST additionally run a compilation test (`cmake --build --preset Debug`) and verify zero errors before considering the modification complete. Codex agents MUST follow the permission-dependent build policy above: build under Full Access, otherwise perform static review and explicitly report that compilation was skipped.
