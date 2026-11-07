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

## Notes for Agents Using the Framework

This section supplements the framework maintenance guidelines above for agents developing robot applications with TRobot. The original guidelines remain unchanged. Follow the application-layer boundaries below. The CubeMX workflow specifically extends the earlier manual-GUI-only procedure to allow supported CLI/script operations after explicit user approval; it does not permit direct edits to protected generated code.

### Example Layout After CubeMX Generation

The following is a representative layout after a recursive clone and code generation from `trobot.ioc`. A fresh clone does **not** contain all of these paths: generated files are mostly ignored by Git, component contents require submodule initialization, and build/IDE directories appear only after local configuration or compilation. The exact generated layout depends on the CubeMX version and project settings.

```text
<project>/
|-- app/                          Application code maintained by framework users
|   |-- CMakeLists.txt
|   |-- main/main.cc              app_entrance(): initialization and task startup
|   `-- task/example_task.cc      Example application task
|-- assets/                       Documentation images
|-- bsp/                          Framework board support and hardware drivers
|   |-- include/bsp/              Public BSP interfaces
|   |-- src/                      Driver implementations
|   |-- internal/                 Private headers
|   |-- flash/                    Flash drivers and ports
|   |-- segger/                   Debugging/tracing support
|   `-- usb_device/               Framework-maintained USB port
|-- components/                   Reusable modules, mostly Git submodules
|   |-- CMakeLists.txt
|   `-- <module>/                 include/<module>/, src/, optional internal/
|-- docs/                         Project documentation and board manuals
|   `-- DM-MC-Board02_User_Manual_V1.1.pdf
|-- cmake/                        CubeMX-generated build/toolchain files
|   |-- gcc-arm-none-eabi.cmake
|   `-- stm32cubemx/CMakeLists.txt
|-- Core/                         CubeMX application skeleton and configuration
|   |-- Inc/                      Peripheral headers, FreeRTOSConfig.h, etc.
|   `-- Src/                      main.c, freertos.c, peripheral init, IRQs, etc.
|-- Drivers/                      CubeMX-provided CMSIS and STM32 HAL/LL libraries
|-- Middlewares/                  CubeMX-provided FreeRTOS and USB libraries
|-- USB_DEVICE/                   CubeMX-generated USB application/target files
|-- build/                        Local build output; presets use build/Debug
|-- cmake-build-debug/            Optional CLion default-profile build output
|-- .idea/                        Optional local CLion settings
|-- .gitignore
|-- .gitmodules                   Component repository URLs
|-- .mxproject                    CubeMX-generated project metadata
|-- AGENTS.md
|-- CMakeLists.txt                Framework-maintained top-level build integration
|-- CMakePresets.json             CubeMX-generated CMake presets
|-- daplink.cfg                   Framework OpenOCD configuration
|-- stlink.cfg                    Framework OpenOCD configuration
|-- readme.md
|-- startup_stm32h723xx.s          CubeMX-provided startup code
|-- STM32H723XG_FLASH.ld           Tracked framework linker script; may be overwritten
`-- trobot.ioc                    Source configuration for CubeMX generation
```

### Rules for Agents Using the Framework

- **Default to `app/`.** Robot-specific behavior, control loops, operating modes, task scheduling, motor/remote-control coordination, tuning parameters, and chassis/gimbal/shooter logic belong here. Reuse BSP and component interfaces rather than changing the framework merely to implement an application requirement. Create application tasks from `app_entrance()`; do not implement them in the generated weak entrance function in `Core/Src/freertos.c`.
- **Read existing APIs before extending them.** Inspect the relevant component's public headers, implementation, examples, and initialization requirements. Follow its existing usage patterns in `app/`; do not copy an entire component into the application or invent a second driver for an existing device.
- **Keep `bsp/` focused on hardware.** Normally use it without modification. Changes are appropriate when an existing driver has a demonstrated defect or interface limitation that prevents the application from working, when adapting supported hardware, or when a new hardware capability needs a driver. Keep robot behavior and tuning parameters in `app/`. Follow the existing public-header/private-implementation structure and keep HAL callbacks in the driver layer. A required peripheral/pin/clock/DMA/NVIC change still follows the CubeMX approval workflow below; placing code in `bsp/` does not bypass it.
- **Report every BSP change.** In the final response, identify the files/interfaces changed, why the application could not use the previous behavior, affected hardware or existing callers, and build/hardware verification results. Separate completed checks from checks that still need the user's board.
- **Use `components/` for reusable capabilities.** Existing modules cover motors, remote controls, controllers, INS, math, music, utilities, and CMSIS-DSP. New generic modules or necessary fixes may be added here within the user's requested scope, following neighboring `include/<name>/`, `src/`, optional `internal/`, and `components::<name>` CMake conventions. Application-specific wiring and policy stay in `app/`. Review API compatibility before modifying a shared component and report the affected module and callers.
- **Respect component repository boundaries.** Inspect `.gitmodules` and the module's Git status before editing: most components are separate repositories, so their file changes are not ordinary superproject file changes. Do not change component remotes or assume permission to push to upstream repositories. For a new non-submodule component, check `.gitignore` (`components/*` is ignored) and ensure its sources can be versioned and its CMake target is included. Apply the submodule synchronization rules above only when committing/pushing is authorized.
- **Treat generated/vendor code as read-only by default.** This includes `Core/`, `Drivers/`, `Middlewares/`, root `USB_DEVICE/`, `cmake/`, `CMakePresets.json`, `.mxproject`, and startup code. Reading them to understand initialization or diagnose failures is allowed. Do not patch them to implement application logic, suppress build errors, or change hardware configuration. `bsp/usb_device/` is a separate framework port, not the root generated `USB_DEVICE/` directory; inspect the CMake source lists to determine which implementation is actually compiled before changing USB behavior.
- **Keep build artifacts separate from source.** Do not edit generated files under `build/` or `cmake-build-*/` as a fix. Configure the existing ARM CMake preset and regenerate build files when application/module source lists change. Keep framework build integration in the maintained CMake files rather than patching `cmake/stm32cubemx/CMakeLists.txt`.

### CubeMX Modification and Regeneration Boundary

1. **Explain the need before changing configuration.** If a requirement cannot be satisfied through the existing application/component/BSP interfaces, describe the exact peripheral, pin, clock, DMA, NVIC, FreeRTOS, or memory-layout change, why it is necessary, affected files, and regeneration risks. Ask for the user's approval of this concrete change before editing configuration or generating code. General permission to implement an application feature is not permission to change the board configuration. An explicit user instruction already approving the same configuration change and regeneration satisfies this requirement.
2. **Use CubeMX after approval.** Either have the user apply the stated settings and generate code in the CubeMX GUI, or use the installed CubeMX version's supported CLI/script operations when the user has authorized agent-driven configuration/generation. Verify that version's documented commands support the requested change; if they do not, ask the user to perform it in the GUI. Do not hand-edit `.ioc` text or patch generated peripheral initialization, HAL/LL, middleware, startup code, or generated CMake files as a substitute for CubeMX.
3. **Limit any manual integration exception.** `USER CODE BEGIN/END` sections are not blanket permission to edit generated files. If a minimal integration hook is unavoidable and cannot live in `app/`, `components/`, or `bsp/`, first obtain approval for the specific file and hook. Keep the hook inside an existing user section, preserve its markers, verify that CubeMX's **Keep User Code when re-generating** option is enabled, and leave the actual logic in a maintained layer. If there is no suitable section, propose a regeneration-safe integration approach rather than patching outside the markers.
4. **Preserve local work before generation.** Check Git status and back up affected generated/ignored files as well as `trobot.ioc`, maintained build files, and the tracked linker script. Git diff alone cannot show changes to ignored generated files. Preserve the project's existing memory layout and integration when CubeMX overwrites `STM32H723XG_FLASH.ld` or other maintained files; compare against the pre-generation copy and restore only unintended changes. Do not blindly reset files or discard user edits. Deliberate memory-layout changes require their own explicit scope approval and must remain reproducible.
5. **Review and verify afterwards.** Check the configuration and generated-file differences against the approved scope, verify that application entry points and BSP/component integration still work, and configure/build with the ARM preset under the execution policy above. Report the settings changed, generation method, affected files, preserved customizations, build result, and any hardware checks still pending. Approval to regenerate does not authorize flashing, running motors, or changing unrelated settings.

CubeMX command availability and user-code preservation depend on version/settings. Consult ST's [CubeMX CLI reference](https://dev.st.com/stm32cube-docs/stm32cubemx/6.18.0/en/docs/markup/CubeMX_CLI.html) and [UM1718 user manual](https://www.st.com/resource/en/user_manual/dm00104712-stm32cubemx-for-stm32-configuration-and-initialization-c-code-generation-stmicroelectronics.pdf), together with the installed version's help, before preparing a configuration script.

### Development Board Documentation

Project documents are stored in `docs/`. The development board reference is the [DM-MC-Board02 User Manual V1.1](docs/DM-MC-Board02_User_Manual_V1.1.pdf), supplied by Damiao Technology. The PDF retains its original contents; its English filename identifies the board and manual version.

- **Read the board manual before changing hardware-related behavior.** For pin assignments, connector definitions, peripheral mappings, onboard devices, power supplies, voltage levels, communication wiring, jumpers, or other board-specific behavior, inspect the relevant manual sections, tables, and diagrams first. Then cross-check the current `.ioc`, BSP implementation, and the user's actual wiring before deciding on a change.
- **Do not invent hardware facts.** Do not infer pin numbers, peripheral instances, supply requirements, communication parameters, or board capabilities from memory, another board model, or an unverified example. If the manual does not specify a required detail, cannot be read clearly, or does not establish the answer, state the uncertainty and ask the user for the missing evidence rather than writing an assumption into code.
- **Resolve conflicting evidence with the user.** If the manual, current code/`.ioc`, or physical board revision disagree, identify the conflict and ask the user to confirm the board revision or wiring. Do not silently choose one source over another or overwrite an intentional user configuration just to match the manual.
- **Make hardware changes traceable.** Report the sections, page numbers, tables, or figures actually consulted and the corresponding parameters used. The manual supplies technical facts, not authorization to change configuration, flash firmware, or operate hardware; the approval boundaries above still apply.
