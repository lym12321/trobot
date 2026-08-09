#include "utils/os.h"

[[noreturn]] void example_task(void *) {
    for (;;) {
        os::task::sleep(1);
    }
}
