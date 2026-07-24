//
// Created by fish on 2026/1/24.
//

#include "utils/os.h"

[[noreturn]] void example_task(void *args) {
    for (;;) {
        // Do something
        os::task::sleep(1);
    }
}
