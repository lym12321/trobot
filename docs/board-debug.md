# TRobot 板上调试

## 烧录固件或连接 GDB

接好 ST-Link，在仓库根目录运行以下命令，烧录并校验 Debug 固件。

```sh
cmake --build --preset Debug --target flash_and_verify
```

调试板上已有的固件时，先确认 `build/Debug/trobot.elf` 与板上版本一致。按实际探针启动 OpenOCD，两个命令选一个。

```sh
openocd -f stlink.cfg     # ST-Link
openocd -f daplink.cfg    # DAPLink 或 CMSIS-DAP
```

在另一个终端运行 `arm-none-eabi-gdb build/Debug/trobot.elf`。以下 GDB 命令会在复位后停到应用入口。

```gdb
target extended-remote localhost:3333
monitor reset halt
break app_entrance
continue
```

结束调试时，在 GDB 提示符下恢复运行，再断开连接。

```gdb
monitor resume
detach
```

<details>
<summary>在 CLion 中配置构建和烧录</summary>

1. 在 CMake 设置中停用默认 profile，启用 **Debug preset**。[CMake 配置截图](../assets/Cmake.png)
2. 打开 **Run/Debug Configurations**，添加 **OpenOCD Download & Run**。[打开配置窗口](../assets/OpenOCD_1.png) · [选择配置类型](../assets/OpenOCD_2.png)
3. 选择构建目标 `trobot`，并选用与探针匹配的 `.cfg` 文件。[目标和探针配置截图](../assets/OpenOCD_3.png)

CubeMX 重新生成后，如果覆盖了项目的链接脚本，检查差异并恢复项目所需的内存布局。[恢复链接脚本示例](../assets/rollback.png)

</details>

## 从板上复制日志

按住板载 **USER** 键上电，或按住 **USER** 再按 **RESET**。电脑识别出 `TROBOT LOGS` 只读磁盘后，复制根目录的 `LOG.TXT`。空日志只有列名，损坏的片段标作 `[damaged log data]`。复制完成后弹出磁盘，松开 **USER**，再按 **RESET** 恢复应用。

如果盘中只有 **`ERROR.TXT`**，固件可能读不到 Flash、无法确定日志扇区的顺序，或没有足够的内存导出。需要保留现场时，先保持维护模式，备份板载 W25Q64 的原始日志区，再恢复普通启动。维护模式只读，普通启动在无法恢复扇区顺序时可能重新格式化整个 1 MiB 日志区。

## 选择 USB 接口

普通启动调用 `bsp_hw_init()`，默认提供一个 CDC 串口和 WinUSB。通过 CMake 的 `TROBOT_USB_CDC_COUNT`（0 到 3）和 `TROBOT_USB_WINUSB`（`ON` 或 `OFF`）选择构建配置。应用也可以在启动时从已编入的功能中选择组合。以下写法替换原有的 `bsp_hw_init()` 调用，只启用一个 CDC 串口。

```cpp
const bsp_usb_config_t usb{.cdc_count = 1, .winusb = false, .disk = nullptr};
uint32_t hw_failed = bsp_hw_init_with_usb(&usb);
```

USB 配置在复位前固定。维护启动始终提供 CDC 0 和只读日志盘，应用可在枚举前注册 CDC 接收回调。接入自定义磁盘时，磁盘描述和上下文需在连接和重连期间保持有效，由一个任务周期调用 `bsp_usb_poll()`，每次最多读取一个 512 字节扇区。

默认组合的 WinUSB 端点是 OUT `0x05` 和 IN `0x84`。`bsp_usb_winusb_send()` 成功后会借用 4 字节对齐的发送缓冲区。等 `bsp_usb_winusb_tx_busy()` 返回 `false` 后再修改或释放它。主机单独发出 `SET_CONFIGURATION(0)` 后，缓冲区可能仍被占用，直到下次有效配置或总线复位。

## 在串口或 USB CDC 上查看告警

默认固件将 `WARNING` 和 `ERROR` 写入板载 Flash。要同时查看实时输出，在 `app_entrance()` 中启用串口日志，将下面这行放在现有的 `ins::init()` 之前。使用硬件串口时，把两个示例中的 `E_USB_CDC_0` 都换成 `E_UART_1`。[应用入口](../app/main/main.cc)

```cpp
logger::init(E_USB_CDC_0, logger::INFO);
```

在 `ins::init()` 之后启用终端并注册需要的命令。CDC 终端可以在电脑枚举串口前初始化。

```cpp
terminal::init(E_USB_CDC_0);
logger::register_terminal_commands();
ins::register_terminal_commands();
```

在终端运行 `log tail`，可查看 Flash 末尾 512 字节内的完整记录，包括上次启动保留的告警。要使用音乐命令，先调用 `music::init()`，再调用 `music::register_terminal_commands()`。

## 校准 IMU

先停用依赖姿态反馈的控制任务。校准期间 `ins::ready()` 仍为真，姿态数据会暂停更新。将板放稳后，在终端运行 `imu calibrate`，保持连续静止 30 秒。移动会让采样重新计时。

采样时可按 **Ctrl+C** 取消。断开 CDC 后校准仍会继续，重连后用 `imu` 查看进度和保存结果。

## 通过 RTT 输出短消息

要从探针观察任务是否运行到某处，可以在那里输出一条短消息，在 RTT 上行通道 0 查看。同一通道由一个任务写入。

```c
#include "bsp/rtt.h"

bsp_rtt_printf("entered example task\n");
```

这个调用最多写入 255 字节。通道空间不足时会立即返回 `BSP_STATUS_BUSY`，适合允许丢失的调试输出。启用 `TROBOT_ENABLE_SYSTEMVIEW=ON` 后，SystemView 事件使用上行通道 1。
