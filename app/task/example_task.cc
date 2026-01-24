//
// Created by fish on 2026/1/24.
//

#include "bsp/uart.h"
#include "utils/os.h"

[[noreturn]] void example_task(void *args) {
    for (;;) {
        // Do something
        bsp_uart_printf_async(E_UART_1, "man what can i say\r\n");
        os::task::sleep(1);
    }
}
