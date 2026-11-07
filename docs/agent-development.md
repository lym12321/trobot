# TRobot Application Development Guide

This guide supplements the mandatory rules in [`AGENTS.md`](../AGENTS.md). Agents using the framework to develop robot applications must follow both documents. It provides application-layer boundaries, CubeMX regeneration procedures, and hardware-reference requirements; it does not authorize unapproved changes, firmware flashing, or hardware operation.

## Example Layout After CubeMX Generation

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

## Rules for Agents Using the Framework

- **Default to `app/`.** Robot-specific behavior, control loops, operating modes, task scheduling, motor/remote-control coordination, tuning parameters, and chassis/gimbal/shooter logic belong here. Reuse BSP and component interfaces rather than changing the framework merely to implement an application requirement. Create application tasks from `app_entrance()`; do not implement them in the generated weak entrance function in `Core/Src/freertos.c`.
- **Read existing APIs before extending them.** Inspect the relevant component's public headers, implementation, examples, and initialization requirements. Follow its existing usage patterns in `app/`; do not copy an entire component into the application or invent a second driver for an existing device.
- **Keep `bsp/` focused on hardware.** Normally use it without modification. Changes are appropriate when an existing driver has a demonstrated defect or interface limitation that prevents the application from working, when adapting supported hardware, or when a new hardware capability needs a driver. Keep robot behavior and tuning parameters in `app/`. Follow the existing public-header/private-implementation structure and keep HAL callbacks in the driver layer. A required peripheral/pin/clock/DMA/NVIC change still follows the CubeMX approval workflow below; placing code in `bsp/` does not bypass it.
- **Report every BSP change.** In the final response, identify the files/interfaces changed, why the application could not use the previous behavior, affected hardware or existing callers, and build/hardware verification results. Separate completed checks from checks that still need the user's board.
- **Use `components/` for reusable capabilities.** Existing modules cover motors, remote controls, controllers, INS, math, music, utilities, and CMSIS-DSP. New generic modules or necessary fixes may be added here within the user's requested scope, following neighboring `include/<name>/`, `src/`, optional `internal/`, and `components::<name>` CMake conventions. Application-specific wiring and policy stay in `app/`. Review API compatibility before modifying a shared component and report the affected module and callers.
- **Respect component repository boundaries.** Inspect `.gitmodules` and the module's Git status before editing: most components are separate repositories, so their file changes are not ordinary superproject file changes. Do not change component remotes or assume permission to push to upstream repositories. For a new non-submodule component, check `.gitignore` (`components/*` is ignored) and ensure its sources can be versioned and its CMake target is included. Apply the submodule synchronization rules above only when committing/pushing is authorized.
- **Treat generated/vendor code as read-only by default.** This includes `Core/`, `Drivers/`, `Middlewares/`, root `USB_DEVICE/`, `cmake/`, `CMakePresets.json`, `.mxproject`, and startup code. Reading them to understand initialization or diagnose failures is allowed. Do not patch them to implement application logic, suppress build errors, or change hardware configuration. `bsp/usb_device/` is a separate framework port, not the root generated `USB_DEVICE/` directory; inspect the CMake source lists to determine which implementation is actually compiled before changing USB behavior.
- **Keep build artifacts separate from source.** Do not edit generated files under `build/` or `cmake-build-*/` as a fix. Configure the existing ARM CMake preset and regenerate build files when application/module source lists change. Keep framework build integration in the maintained CMake files rather than patching `cmake/stm32cubemx/CMakeLists.txt`.

## CubeMX Modification and Regeneration Boundary

1. **Explain the need before changing configuration.** If a requirement cannot be satisfied through the existing application/component/BSP interfaces, describe the exact peripheral, pin, clock, DMA, NVIC, FreeRTOS, or memory-layout change, why it is necessary, affected files, and regeneration risks. Ask for the user's approval of this concrete change before editing configuration or generating code. General permission to implement an application feature is not permission to change the board configuration. An explicit user instruction already approving the same configuration change and regeneration satisfies this requirement.
2. **Use CubeMX after approval.** Either have the user apply the stated settings and generate code in the CubeMX GUI, or use the installed CubeMX version's supported CLI/script operations when the user has authorized agent-driven configuration/generation. Verify that version's documented commands support the requested change; if they do not, ask the user to perform it in the GUI. Do not hand-edit `.ioc` text or patch generated peripheral initialization, HAL/LL, middleware, startup code, or generated CMake files as a substitute for CubeMX.
3. **Limit any manual integration exception.** `USER CODE BEGIN/END` sections are not blanket permission to edit generated files. If a minimal integration hook is unavoidable and cannot live in `app/`, `components/`, or `bsp/`, first obtain approval for the specific file and hook. Keep the hook inside an existing user section, preserve its markers, verify that CubeMX's **Keep User Code when re-generating** option is enabled, and leave the actual logic in a maintained layer. If there is no suitable section, propose a regeneration-safe integration approach rather than patching outside the markers.
4. **Preserve local work before generation.** Check Git status and back up affected generated/ignored files as well as `trobot.ioc`, maintained build files, and the tracked linker script. Git diff alone cannot show changes to ignored generated files. Preserve the project's existing memory layout and integration when CubeMX overwrites `STM32H723XG_FLASH.ld` or other maintained files; compare against the pre-generation copy and restore only unintended changes. Do not blindly reset files or discard user edits. Deliberate memory-layout changes require their own explicit scope approval and must remain reproducible.
5. **Review and verify afterwards.** Check the configuration and generated-file differences against the approved scope, verify that application entry points and BSP/component integration still work, and configure/build with the ARM preset under the execution policy above. Report the settings changed, generation method, affected files, preserved customizations, build result, and any hardware checks still pending. Approval to regenerate does not authorize flashing, running motors, or changing unrelated settings.

CubeMX command availability and user-code preservation depend on version/settings. Consult ST's [CubeMX CLI reference](https://dev.st.com/stm32cube-docs/stm32cubemx/6.18.0/en/docs/markup/CubeMX_CLI.html) and [UM1718 user manual](https://www.st.com/resource/en/user_manual/dm00104712-stm32cubemx-for-stm32-configuration-and-initialization-c-code-generation-stmicroelectronics.pdf), together with the installed version's help, before preparing a configuration script.

## Development Board Documentation

Project documents are stored in `docs/`. The development board reference is the [DM-MC-Board02 User Manual V1.1](docs/DM-MC-Board02_User_Manual_V1.1.pdf), supplied by Damiao Technology. The PDF retains its original contents; its English filename identifies the board and manual version.

- **Read the board manual before changing hardware-related behavior.** For pin assignments, connector definitions, peripheral mappings, onboard devices, power supplies, voltage levels, communication wiring, jumpers, or other board-specific behavior, inspect the relevant manual sections, tables, and diagrams first. Then cross-check the current `.ioc`, BSP implementation, and the user's actual wiring before deciding on a change.
- **Do not invent hardware facts.** Do not infer pin numbers, peripheral instances, supply requirements, communication parameters, or board capabilities from memory, another board model, or an unverified example. If the manual does not specify a required detail, cannot be read clearly, or does not establish the answer, state the uncertainty and ask the user for the missing evidence rather than writing an assumption into code.
- **Resolve conflicting evidence with the user.** If the manual, current code/`.ioc`, or physical board revision disagree, identify the conflict and ask the user to confirm the board revision or wiring. Do not silently choose one source over another or overwrite an intentional user configuration just to match the manual.
- **Make hardware changes traceable.** Report the sections, page numbers, tables, or figures actually consulted and the corresponding parameters used. The manual supplies technical facts, not authorization to change configuration, flash firmware, or operate hardware; the approval boundaries above still apply.
