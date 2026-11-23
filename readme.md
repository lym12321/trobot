# TRobot

**T**GU **R**obot: The next robot embedded development framework.

## 构建与烧录

构建工具包括 Git、STM32CubeMX、CMake、Ninja 和 Arm GNU Toolchain。

```sh
git clone --branch dev --recurse-submodules https://github.com/lym12321/trobot.git
cd trobot
```

在 STM32CubeMX 中打开 `trobot.ioc`，执行 **Generate Code**。生成后检查链接脚本 `STM32H723XG_FLASH.ld`，保留项目的内存布局。

```sh
cmake --preset Debug
cmake --build --preset Debug
```

调试固件输出至 `build/Debug/trobot.elf`。优化构建使用 `Release` 预设。

连接 ST-Link 后，通过 OpenOCD 烧录并校验固件。

```sh
cmake --build --preset Debug --target flash_and_verify
```

串口终端、GDB 调试及板载日志导出见[板上调试指南](docs/board-debug.md)。

## 项目结构

- `app/` 实现机器人行为，在 `app/main/main.cc` 的 `app_entrance()` 中创建应用任务。
- `components/` 提供可复用的驱动和算法，各组件导出 `components::<name>` CMake 目标。
- `bsp/` 封装板载外设，提供 `bsp_*` C 接口。

<img src="assets/architecture.png" width="640" alt="trobot 分层架构">

## 可选组件

CMSIS-DSP 和 RoboMaster 裁判系统通信组件可分别安装。

```sh
git submodule add -f https://github.com/lym12321/tr_comp_cmsis_dsp.git components/cmsis-dsp
git submodule add -f https://github.com/lym12321/tr_comp_robomaster.git components/robomaster
```
