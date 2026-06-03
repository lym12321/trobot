# TRobot Agent Guide

This file records project-specific maintenance rules for AI coding agents.
Read it before editing code in this repository.

## Project Context

- This is an STM32H723 embedded robot framework built with CMake.
- The project uses FreeRTOS. Prefer clear task, queue, semaphore, timer, and event-group based designs when they simplify concurrency.
- MCU resources are relatively abundant. Do not optimize aggressively for code size unless a concrete limit is involved.
- Favor concise, readable, maintainable code over dense micro-optimizations.

## Coding Style

- Use `snake_case` for identifiers whenever possible.
- Exceptions:
  - Preprocessor macros may keep `UPPER_SNAKE_CASE`.
  - Enum constants may keep the existing enum style.
  - Third-party, STM32 HAL, CubeMX, FreeRTOS, SEGGER, or component API names should not be renamed just for style.
- Keep names descriptive but not verbose.
- Prefer small, direct functions with clear ownership over clever abstractions.
- Add comments only when they explain non-obvious hardware behavior, timing constraints, concurrency contracts, or protocol details.

## RTOS Guidelines

- Use RTOS primitives deliberately instead of ad hoc polling when coordination is needed.
- Prefer blocking waits with timeouts over busy loops.
- Keep ISR code short. Defer work to tasks using queues, notifications, semaphores, or other RTOS-safe mechanisms.
- Make task responsibilities explicit. Avoid sharing mutable state across tasks unless ownership and synchronization are clear.
- When adding a task, document its purpose, wake-up source, and priority rationale near the task setup if the relationship is not obvious.

## Embedded Design Preferences

- Clarity is more important than minimizing every byte.
- Avoid hidden global coupling. If a module owns hardware state, expose a narrow API around that ownership.
- Treat hardware timing, DMA buffers, interrupt callbacks, and cache coherency as high-risk areas. Review these changes carefully.
- Prefer typed configuration and local constants over scattered magic numbers.
- Preserve generated-code boundaries. Keep user logic in application, BSP, or component areas rather than editing CubeMX generated sections unnecessarily.

## Repository Layout

- `app/`: application entry points and RTOS task code.
- `bsp/`: board support package and hardware-facing drivers.
- `components/`: reusable components and optional submodules.
- `Core/`, `Drivers/`, `Middlewares/`, `USB_DEVICE/`, `cmake/stm32cubemx/`: STM32CubeMX, HAL, middleware, and generated integration areas.
- `assets/`: documentation images and other non-firmware assets.

## Build And Verification

- The project is built through CMake presets, commonly from CLion.
- `app` is compiled with `-Werror`; keep warnings clean.
- When changing firmware code, prefer at least a local build check when toolchains are available.
- For hardware-dependent behavior, state what was verified locally and what still needs board testing.
- Flashing is available through the `flash_and_verify` CMake target with OpenOCD configuration in `stlink.cfg`.

## CubeMX Notes

- The `.ioc` file is the source for STM32CubeMX regeneration.
- FreeRTOS and MCU peripheral/clock/NVIC/DMA configuration changes should be made through the `.ioc` file when they are configuration-level changes.
- Edit the `.ioc` file directly only when the intended CubeMX setting and its generated-code effect are fully understood. If there is any uncertainty, stop and ask the user to make or confirm the CubeMX change.
- CubeMX may overwrite generated files. Keep custom logic out of generated regions unless the file explicitly supports user sections.
- After CubeMX regeneration, check linker script changes carefully. The project README notes that the `.ld` file may need to be restored.

## Git And Submodules

- `components/` may contain Git submodules. Do not reset, clean, or rewrite submodule state unless explicitly requested.
- If the worktree already has unrelated changes, leave them intact and scope edits to the requested task.
- When adding optional components, use documented submodule commands from `readme.md` unless the user requests another source.

## Agent Operating Rules

- Before editing, inspect the nearby code and follow existing local patterns.
- Keep changes narrowly scoped to the request.
- Do not rename public APIs or move module boundaries unless that is part of the requested change.
- Prefer `rg` for searching.
- Do not guess when project behavior, hardware configuration, CubeMX settings, or user intent is uncertain. Stop and ask the user immediately.
- Maintain this file when project conventions change, especially style, RTOS usage, build, or verification rules.
