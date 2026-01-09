//
// Created by fish on 2025/9/13.
//

#include <rc/dr16.h>

#include "bsp/bsp.h"
#include "bsp/led.h"
#include "bsp/time.h"

#include "bsp/buzzer.h"
#include "ins/ins.h"
#include "utils/logger.h"
#include "utils/vofa.h"
#include "utils/terminal.h"
#include "utils/os.h"

#include "motor/dm.h"

motor::DM test_dm_motor("test_dm_motor", {
    .slave_id = 0x01,
    .master_id = 0x02,
    .port = E_CAN_1,
    .mode = motor::DM::MIT,
    .p_max = 120.f,
    .v_max = 10.f,
    .t_max = 10.f
});

void test(void *args) {
    test_dm_motor.init();
    // const auto ins = ins::data();
    const auto rc = rc::dr16::data();
    for (;;) {
        // vofa::send(E_UART_1, ins->roll, ins->pitch, ins->yaw, ins->raw.accel[0], ins->raw.accel[1], ins->raw.accel[2]);
        // vofa::send(E_UART_1, ins->roll, ins->pitch, ins->yaw);
        vofa::send(E_UART_1, rc->s_l, rc->s_r, rc->timestamp);
        bsp_time_delay(1);
    }
}


extern "C" [[noreturn]] void app_entrance(void *args) {
    bsp_hw_init();

    bsp_led_set(0, 50, 0);
    bsp_buzzer_flash(4500, 0.2f, 100);

    bsp_time_delay(100);

    // logger::init(E_UART_1, logger::INFO);
    // terminal::init(E_UART_1, 921600);

    ins::init();
    while (!ins::inited) bsp_iwdg_refresh(), bsp_time_delay(10);

    rc::dr16::init(E_UART_5);

    bsp_buzzer_flash(4500, 0.2f, 75);
    bsp_time_delay(50);
    bsp_buzzer_flash(4500, 0.2f, 75);

    os::task test_task;
    test_task.create(test, nullptr, "test", 1024, os::task::Priority::MEDIUM);

    for (;;) {
        bsp_led_set_hsv(static_cast<float>(bsp_time_get_ms() % 3000) / 3000.0f, 1.0f, 0.3f);
        bsp_iwdg_refresh();
        bsp_time_delay(5);
    }
}
