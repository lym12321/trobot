#include "bsp/time.h"
#include "bsp/uart.h"
#include "utils/logger.h"
#include "utils/os.h"

[[noreturn]] void example_task(void *) {
    logger::info("Example task started.");
    for (;;) {
        // logger::info("ts: %d", bsp_time_get_ms());
        os::task::sleep(1);
    }
}
