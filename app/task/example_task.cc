//
// Created by fish on 2026/1/24.
//

#include "bsp/uart.h"
#include "ins/ins.h"
#include "utils/os.h"

#include "robot/chassis/omni.h"
#include "utils/vofa.h"

motor::dji m0("m0", motor::dji::M3508, {.id = 1, .port = E_CAN_1, .mode = motor::dji::CURRENT});
motor::dji m1("m1", motor::dji::M3508, {.id = 2, .port = E_CAN_1, .mode = motor::dji::CURRENT});
motor::dji m2("m2", motor::dji::M3508, {.id = 3, .port = E_CAN_1, .mode = motor::dji::CURRENT});
motor::dji m3("m3", motor::dji::M3508, {.id = 4, .port = E_CAN_1, .mode = motor::dji::CURRENT});

robot::chassis::omni chassis(m0, m1, m2, m3, (controller::pid::pid_para_t) {
    .kp = 1.f,
    .ki = 0.f,
    .kd = 0.f,
    .int_limit = 1000,
    .out_limit = 16384
},{ .wheel_radius = 1.f, .rotate_radius = 1.f, .offline_timeout_ms = 100 });

[[noreturn]] void example_task(void *args) {

    // chassis.init();
    for (;;) {
        // Do something
        // chassis.update(0, 0, 1);
        // bsp_uart_printf_async(E_UART_1, "man what can i say\r\n");
        const auto imu = ins::state();
        vofa::send(E_UART_1, imu.accel[0], imu.accel[1], imu.accel[2], imu.gyro[0], imu.gyro[1], imu.gyro[2], imu.roll, imu.pitch, imu.yaw);
        os::task::sleep(1);
    }
}
