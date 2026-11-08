# Repository Guidelines

Direct requirements and prohibitions are hard constraints. **SHOULD** and `Prefer` indicate project defaults; deviate only when the current requirement or measured evidence justifies it, and state the reason.

## Project Scope and Ownership

TRobot is an STM32H723 firmware framework built with CMake, FreeRTOS, STM32 HAL, and C++ components. CubeMX-generated code stays in `Core/`, `Drivers/`, `Middlewares/`, `USB_DEVICE/`, and `cmake/stm32cubemx/`. The generated FreeRTOS `entrance` thread calls C++ `app_entrance()` in `app/main/main.cc`; keep application task orchestration and robot behavior under `app/`.

Hardware ownership belongs in `bsp/`: public C headers are in `bsp/include/bsp/`, implementations in `bsp/src/`, private helpers in `bsp/internal/`, and bundled third-party code is in `bsp/third_party/`. Reusable modules live in `components/<name>/` with `include/<name>/`, `src/`, optional `internal/`, and a `CMakeLists.txt` exporting `components::<name>`.

## Hard Modification Boundaries

Treat `trobot.ioc` as the source for peripheral, clock, DMA, NVIC, and FreeRTOS configuration. Keep custom logic out of generated files unless inside an existing `USER CODE` section. BSP drivers should expose narrow C wrappers and hide HAL handles, buffers, filters, and callbacks in `bsp/src/`.

Changes that require CubeMX-generated content — including `FreeRTOSConfig.h`, NVIC priorities, pin assignments, the clock tree, DMA streams, linker memory regions, or any `.ioc`-managed setting — MUST NOT be made directly. Describe the required change and ask the user to apply it through CubeMX. Direct edits to `trobot.ioc`, generated `Core/` code outside `USER CODE` sections, `Drivers/`, `Middlewares/`, `USB_DEVICE/`, or `startup_stm32h723xx.s` are prohibited.

Do not apply project-wide naming, formatting, or cleanup rules to generated or third-party code. Modify bundled third-party sources only when the task explicitly targets them or a wrapper cannot solve the current problem; keep any necessary patch narrow and compatible with upstream structure.

## Build and Development Commands

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

## MCU Design Priorities

This project targets STM32-class MCUs, where Flash size, RAM, stack usage, execution cycles, interrupt latency, and task-switch overhead are first-class design constraints. Among correct designs, prefer the smallest direct solution that satisfies the current requirement.

Unless the user explicitly specifies otherwise, assume a single-core MCU. Analyze concurrency as single-core task/ISR interleaving, use the existing critical-section and RTOS primitives, and do not introduce multicore synchronization, desktop-oriented concurrency patterns, or lock-free machinery for hypothetical parallel execution. If a requested design depends on multicore behavior or a different target environment, state that assumption explicitly before proceeding.

Prefer compile-time or fixed-capacity storage when sizes and lifetimes are known. Runtime allocation, a new task, a new queue, or a new persistent buffer requires a current need and a capacity or stack size derived from protocol, hardware, timing, or measured load. Do not add capacity only for hypothetical future use.

The firmware toolchain disables exceptions, RTTI, and thread-safe local-static guards. Prefer `constexpr`, `constinit`, and zero initialization. Avoid global constructors with side effects; function-local `static` objects must be constant-initialized or fully initialized before concurrent access. Initialize hardware, RTOS objects, and module state explicitly.

Do not use recursion unless its maximum depth and stack cost are strictly bounded and justified. Keep large buffers and temporary objects off ISR and task stacks. Derive each task stack from its worst-case call path and, when hardware verification is available, confirm margin with the FreeRTOS stack high-water mark.

## Communication and Concurrency Decisions

Choose the lightest mechanism that satisfies the current ownership and timing requirements:

1. Use a direct call or typed state access for synchronous behavior with a clear owner. Continuously sampled device feedback, such as motor feedback, should normally remain component state rather than become a topic.
2. Use a callback when one producer must notify a fixed user-facing consumer. Invocation may be direct or explicitly deferred; its context, latency, and worst-case work must be part of the contract.
3. Use a task notification to wake a task, especially from an ISR, when buffering or payload ownership is unnecessary.
4. Use a queue only when events must be buffered, ordered, transferred between contexts, or protected from producer bursts.
5. Use a topic only when the producer must be independent of its consumers, multiple independent subscribers are required, or cross-module fan-out is a current requirement.

Do not introduce messaging infrastructure merely for API symmetry. State whether delivery is synchronous or deferred, which context executes user code, whether events may be dropped or overwritten, and who owns referenced data.

`std::function` may be used for user-facing callbacks that need type erasure. Bind or replace it during initialization, keep captures small, and do not assume it is allocation-free. Arbitrary `std::function` callbacks MUST NOT run in ISR context; defer them to a task. A callback that must run directly in an ISR needs an explicit bounded, non-blocking, allocation-free contract.

Callback and subscription objects must outlive their registration. Keep registration topology fixed after initialization unless runtime mutation is a current requirement with explicit synchronization and failure behavior.

For every shared object, identify the producer and consumer contexts. `volatile` does not provide synchronization. Protect ISR/task shared state with the critical-section helpers in `bsp/sys.h`, keep critical sections minimal, never block in an ISR, and use ISR-safe FreeRTOS APIs where required. Bitfield updates are read-modify-write operations and require protection when shared between ISR and task contexts.

Before calling a FreeRTOS `...FromISR` API, confirm that the configured NVIC preemption priority is numerically greater than or equal to `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`. A numerically lower priority value is above the syscall ceiling on Cortex-M and MUST NOT call FreeRTOS, including the critical-section helpers in `bsp/sys.h`. `configMAX_SYSCALL_INTERRUPT_PRIORITY` is the encoded BASEPRI value, not a logical NVIC priority to compare directly. If the priority must change, ask the user to update it through CubeMX rather than editing generated configuration.

Initialize board hardware through `bsp_hw_init()` before starting application tasks. Create tasks from `app_entrance()` using `os::task::static_create()` or FreeRTOS APIs, and keep watchdog refresh paths explicit. Use `os::task::sleep()` or `vTaskDelayUntil()` for periodic work; reserve busy microsecond waits for short hardware timing.

Keep ISRs and HAL callbacks short. Prefer updating bounded state and waking a task over performing application work in interrupt context. DMA-visible buffers must be placed in a DMA-accessible RAM domain; review lifetime, alignment, ownership, and cache behavior before changing UART, CAN, SPI, USB, IMU, or flash paths.

## Coding and Naming Rules

Use C17 for BSP/generated C and C++23 for application/components. Concrete implementations MUST prioritize runtime and memory efficiency while remaining as direct and concise as correctness permits. Minimize data copies, indirection, unnecessary branches, and critical-section duration. Do not add abstraction or layering unless it reduces current complexity or serves current reuse. Keep code at a level a human reviewer can follow easily. If a feature can be implemented clearly as a sequential flow in one function body, keep it there rather than extracting another function merely to call it; split it only when the logic is genuinely complex. Keep four-space indentation and concise functions.

Names MUST be concise, domain-accurate, and unambiguous; elegance comes from using scope to remove repetition, not from inventing abbreviations. Use `snake_case` for functions and variables, `*_t` for value or struct types, and `*_e` for enum types when consistent with the surrounding module; classes and namespaces remain short domain nouns. Do not repeat information already expressed by a namespace, class, or type (`msg::info_t`, not `msg::message_info_t`; `rx`, not `uart_rx_receiver`). Prefer established abbreviations such as `msg`, `rx`, `tx`, `buf`, `len`, `idx`, `cfg`, `cb`, and `ctx`; do not invent abbreviations a reviewer must decode. Use one-letter names only for conventional loop indices, mathematical terms, or extremely small scopes. Boolean names should read as states or predicates (`ready`, `enabled`, `pending`), paired names should be symmetric (`src`/`dst`, `head`/`tail`), and units must remain explicit when ambiguous (`timeout_ms`, `angle_rad`). Words such as `data`, `object`, `instance`, `manager`, `handler`, `value`, `get`, or `set` are acceptable when they express the domain; avoid them only when a more precise noun or verb exists, and do not rename established APIs merely to remove them. Prefer the shortest name that remains clear at both its declaration and call sites. BSP APIs use `bsp_*` and C enums such as `E_CAN_1` or `E_UART_1`; keep HAL, FreeRTOS, SEGGER, CubeMX, and third-party names unchanged. Components use namespaces such as `os`, `motor`, `rc::dr16`, `ins`, and `robot::chassis`, with small classes or structs for typed configuration.

Use named constants only when a value is reused, defines a protocol/hardware/storage invariant, or determines static memory layout. Keep one-off values at their use site; do not add file-scope constants as speculative configuration. Prefer file-local `static` helpers when extraction is justified.

Prefer bitfields for related internal flags or compact state when they materially reduce storage without making access more expensive or unsafe. Use an explicit unsigned base type and verify the enclosing type size when layout matters. For serialized protocols, persistent formats, or new MMIO definitions, follow the stricter representation rules below; `static_assert(sizeof(...))` alone does not verify bit order.

Keep comments minimal — only for hardware timing, units, protocol layouts, calibration, concurrency, or DMA/cache constraints. Do not add section-header banners, ASCII art, "Created by" blocks, or explanatory comments for self-documenting code. When in doubt, omit the comment.

## Data, Protocol, and Time Rules

At CAN, UART, USB, flash, file, and other byte-stream boundaries, validate the complete length before reading fields. Wire and storage fields use fixed-width integer types and explicit byte order; local buffer lengths use `size_t`. Never alias a byte buffer directly as a protocol struct. Prefer explicit shifts or `memcpy` decoding. A bitfield or `packed` protocol view is allowed only when the GCC/ARM ABI is fixed, it materially simplifies the current protocol, bytes are copied into an object rather than aliased, every multi-byte field is naturally aligned or copied with `memcpy`, and both size and representative bit patterns are verified. `packed` controls layout but does not make unaligned access safe.

Use unsigned subtraction for wrap-safe elapsed-time checks (`now - start >= timeout`), not absolute deadline comparison (`now >= start + timeout`). Keep ticks, milliseconds, microseconds, and hardware cycles distinct, make conversions explicit, and consider `pdMS_TO_TICKS()` rounding for short delays.

## Error and Resource Management

Use `BSP_ASSERT` only for initialization-time programmer errors or configuration invariants that make safe startup impossible. Runtime conditions such as timeouts, invalid frames, queue pressure, disconnects, or peripheral faults MUST use explicit status, retained validity state, counters, recovery, or a documented drop policy rather than assertion failure.

Avoid runtime heap allocation. If dynamic allocation is required, confine it to initialization where possible and document ownership, lifetime, failure behavior, and why static storage is unsuitable. Do not introduce exceptions or RTTI dependencies into modules that currently avoid them.

Buffers and referenced messages need explicit capacity, ownership, lifetime, and overflow behavior. Avoid hidden copies; pass small values directly and larger immutable data by reference only when its lifetime is guaranteed across the complete call or deferred-delivery interval.

Check returned status values. Intentionally ignored results must use an explicit `(void)` cast only when failure is known to be harmless. Keep formatted I/O, floating-point formatting, logging, and diagnostic aggregation out of ISRs and high-frequency control paths unless their worst-case cost is measured and accepted.

## Verification by Change Type

Before build verification, every agent MUST determine whether it is running as Codex. A Codex agent MUST inspect the active permission profile. With Full Access (`danger-full-access` or equivalent), it MUST run the applicable build below. If sandboxed, restricted, or unable to confirm the permission level, it MUST NOT run firmware builds, `make`, or CMake/Ninja commands and MUST report that compilation was skipped because Full Access is unavailable. Non-Codex agents MUST run the required Debug build after every modification and verify zero errors.

- Documentation-only changes require final-diff review and `git diff --check`. Codex agents do not need a firmware build for documentation-only changes; non-Codex agents still follow the build rule above.
- Firmware changes require a clean Debug build with `cmake --build --preset Debug --clean-first`. Run `cmake --preset Debug` first when configuration inputs changed or the build tree is unavailable.
- Changes whose goal or likely material effect is Flash size, RAM use, stack use, or runtime performance MUST additionally build the Release preset; other changes involving static buffers, task stacks, templates, virtual dispatch, allocation, or formatted I/O SHOULD do so when the impact is non-trivial. Run `cmake --preset Release` first when configuration inputs changed or the Release build tree is unavailable. Use the Release ELF and map file for resource conclusions; Debug is built with `-O0` and is not a representative size baseline. Compare before and after when a baseline is available.
- Changes affecting ISR/task interaction, DMA, peripherals, or control behavior require a stated hardware-verification plan and a report of what was or was not performed.

No standalone automated test harness is present. Before creating a test directory or supporting test framework, obtain explicit human confirmation. Tests and their support code must address a current risk and remain minimal.

Build permission does not authorize OpenOCD, flashing, `load`, `monitor program`, or hardware verify targets; those require an explicit user request. PRs should include a summary, affected modules, build results, hardware verification status, and linked issues when applicable.

## Repository Workflow

### Branch and Commit Policy

- Development work MUST be performed on the local `dev` branch, which tracks `origin/dev`. Do not create commits directly on `main`.
- Before creating any commit, verify that the current branch is `dev`. Preserve unrelated user changes and do not discard or stash them without explicit approval.
- Before any `git push`, inspect the local commits that are not yet on the remote and decide whether they should be consolidated. If it is unclear whether consolidation is appropriate, obtain explicit human confirmation before pushing.
- Changes may be merged from `dev` into `main` only after validation and human review have completed.
- A request to commit or merge into `main` requires a second explicit confirmation immediately before the operation. The initial request to make or merge the change does not count as that confirmation.
- Never push `main` or perform a `dev`-to-`main` merge without that second confirmation.

### Modification Discipline

Do not rewrite or restructure code speculatively. Follow the existing intent, naming, formatting, and architectural patterns of every touched file. Before changing more than a few localized lines, or changing an interface, ownership, concurrency, memory layout, module boundary, user-authored comment, symbol name, or abstraction, ask whether the change is desired unless that exact change was already explicitly requested or approved in a plan. Preserve user-authored comments unless removing or updating them is necessary for the requested change.

### Local Tooling

- Before using a local Python environment or any other language-specific environment, obtain explicit human confirmation on how that environment should be used.

### Submodule Synchronization

- **URL protocol**: Submodule URLs in `.gitmodules` MUST use HTTPS, never SSH.
- **Local iteration**: commit only the submodule (`components/<name>`); do NOT sync the trobot superproject. Avoids noise commits like `chore(<name>): update submodule`.
- **Remote push**: after pushing the submodule, MUST update the submodule pointer in trobot (`git add components/<name>`, commit, push). The remote depends on the pointer to resolve the submodule version.

### Default Configuration in main.cc

`logger::init` and `terminal::init` in `app/main/main.cc` are **commented out by default**. Uncomment them during debugging, but keep them commented when committing. Prevents debug output from occupying the serial port in production firmware.

### Commit Attribution

Commit messages MUST be written in Chinese with `feat`, `fix`, `chore`, or `doc` and the format `<type>(<scope>): <中文简述>`, e.g. `fix(bsp): 修复 CAN 发送临界区过长问题`. The agent marker goes on a separate line after the body and MUST include the provider, exact model variant, and reasoning effort whenever the runtime exposes them. Use `[agent: <provider>/<model>, reasoning=<effort>]`; do not shorten the model to a generic family name or guess unavailable fields. For example:

```
fix(bsp): 替换运行时 BSP_ASSERT 为错误返回

[agent: openai/gpt-5.6-sol, reasoning=xhigh]
```

### Modification Review

After every modification, the agent MUST review the changes for correctness before yielding. The review scope includes but is not limited to:
- Code logic and invariants
- API contract consistency (callers updated, signatures match)
- Execution-context and concurrency correctness (ISR/task shared state, blocking, critical sections, IRQ syscall priority)
- Memory safety (buffer sizes, stack use, protocol access, DMA alignment, RAM domain placement)
- MCU cost (Flash, RAM, stack, copies, cycles, and task or queue overhead where relevant)
- Runtime `BSP_ASSERT` misuse (assert only for init-time errors, never runtime)
- Generated-code and CubeMX boundary compliance
- Code minimality: inspect the final diff line by line and remove unused includes, symbols, fields, branches, wrappers, options, diagnostics, compatibility layers, duplicated logic, and unrelated cleanup. Code retained only for possible future use is prohibited; every addition MUST serve a current requirement, caller, or invariant.

## Application Development and Board Reference

Agents developing robot applications with TRobot MUST also read and follow [`docs/agent-development.md`](docs/agent-development.md). The guide defines `app/`/`bsp/`/`components/` ownership, submodule boundaries, CubeMX regeneration procedures, and board documentation checks. This repository targets the DM-MC-Board02 development board; use the included [`DM-MC-Board02 User Manual V1.1`](docs/DM-MC-Board02_User_Manual_V1.1.pdf) for hardware-specific work and cross-check its revision and the current `.ioc`/BSP configuration. Do not assume undocumented pinouts or electrical parameters.
