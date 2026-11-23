# ST7789 显示

在 DM-MC-Board02 的一个应用任务中调用 `show_ready()`，即可在黑底上显示白色的 READY。先由 `app_entrance()` 完成 `bsp_hw_init()`。使用板级适配头的目标需要链接 `components::display` 和 `bsp`，项目的 `app` 已满足这一条件。

```cpp
#include "display/dm_mc_board02.h"

display::status_e show_ready() {
    using display::dm_mc_board02::lcd_port;
    static constinit display::st7789 screen{lcd_port, display::panel_240x280};
    auto status = screen.init();
    if (status != display::status_e::ok) return status;
    return screen.text(8, 8, "READY");
}
```

正常运行时只需初始化一次。初始化会复位、唤醒、配置并清黑整屏，成功后点亮背光。初始化返回错误时背光保持关闭，可在同一实例上重试。画图函数同步返回，应用用一个任务独占整块屏，包括初始化和背光控制。

## 填色、图片和文字

`fill({x,y,width,height}, color)` 填满矩形，`blit(area, pixels)` 写入逐行排列的 RGB565 像素。像素数必须等于面积，绘制区域须完全在可视范围内。调用期间借用输入缓冲，函数返回后即可复用。驱动每次最多传输 128 字节像素，整段文字共用一个绘制窗口。

可用 `pixel`、`line`、`rect` 和 `circle` 画轮廓，颜色可用 `display::rgb565(255, 128, 0)` 构造。`text(x, y, chars, fg, bg, scale, font)` 支持 1 到 3 倍缩放、`font_e::classic` 和 `font_e::block`。字库覆盖英文字母、数字、空格和部分标点，其他字符显示为问号。

## 在另一块 ST7789 屏上使用

链接 `components::display`，包含 `display/st7789.h`。用 `port_t` 提供端口上下文、硬复位、毫秒延时、背光开关及两种同步写入操作。`command` 在同一次片选内发送操作码及其所有参数，等操作码发完再切换 D/C。`data` 一次发送一块像素字节。两项操作都须在返回前完成传输并释放片选。端口没有独立背光时可提供空的背光函数。

用 `panel_t` 描述该面板的可视宽高、显存偏移、MADCTL、反色和面板厂商的初始化参数。`setup` 的每条记录依次是一个命令字节、一个长度字节和对应数量的参数字节。驱动负责休眠退出、MADCTL、RGB565、反色、开屏和清屏。端口、面板描述和参数表必须比屏实例活得更久。

随附的 `panel_240x280` 对应本项目的 240×280 面板，Y 偏移为 20。DM-MC-Board02 V1.1 手册第 14 节（第 9–10 页）给出了扩展口的 SPI1 和 PA5 按键连线。板级适配使用项目 CubeMX 配置的片选、复位、背光和 D/C 引脚，面板偏移与寄存器参数沿用原驱动。

## 从 LCD 接口迁移

| 原接口（`bsp_lcd_*` 或 `lcd_*`） | 当前调用 |
| --- | --- |
| `init`、`clear`、`set_backlight` | `screen.init()`、`screen.clear(color)`、`bsp_display_backlight(enabled)` |
| `fill`、`blit_rgb565` | `screen.fill(area, color)`、`screen.blit(area, pixels)` |
| `draw_pixel/line/rect/circle` | `screen.pixel/line/rect/circle`（矩形传 `rect_t`） |
| `draw_ascii*` | `screen.text(...)`，单字符可传 `std::string_view{&ch, 1}` |
| `set_key_callback`、`process_keys` | 包含 `bsp/key.h`，调用 `bsp_key_set_callback`、`bsp_key_poll` |

五向键可单独使用。确认 `bsp_hw_init()` 返回值未置 `BSP_HW_ADC` 后，先注册按键回调，再由一个任务每隔至多 10 ms 调用 `bsp_key_poll()`。回调同步运行在此任务，注册的上下文须在轮询期间保持有效。按键消抖 10 ms，长按 800 ms。注册了双击的键会等待第二次按下，等待窗口为 500 ms。只注册单击时，松键即触发。
