#include "debug_internal.h"

#include <cstdint>

#include "bsp/bsp.h"
#include "bsp/imu.h"
#include "bsp/time.h"
#include "ins/ins.h"

namespace debug_mode::internal {
namespace {

enum class command_e : std::uint8_t {
    STATUS = 1,
    READ_STATE = 2,
    START_CALIBRATION = 3,
    CANCEL_CALIBRATION = 4,
};

bool hardware_ready;

bool write_vector(response_t &response, const float *values, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (!response.write_float(values[i])) return false;
    }
    return true;
}

status_e write_status(response_t &response) {
    const bool ready = hardware_ready && ins::ready();
    const auto calibration = ins::calibration_status();
    const std::uint32_t flags =
        (hardware_ready ? 1U : 0U) |
        (ready ? 2U : 0U) |
        (calibration.valid ? 4U : 0U) |
        (calibration.persisted ? 8U : 0U);
    return response.write_u32(1) && response.write_u32(flags) &&
        response.write_u32(static_cast<std::uint32_t>(calibration.state)) &&
        response.write_u32(static_cast<std::uint32_t>(calibration.error)) &&
        response.write_u32(calibration.progress_ms) &&
        response.write_u32(calibration.duration_ms) &&
        write_vector(response, calibration.gyro_corr.data(), 3)
        ? status_e::OK : status_e::IO_ERROR;
}

status_e write_state(response_t &response) {
    if (!hardware_ready || !ins::ready()) return status_e::NOT_READY;
    const auto state = ins::state();
    if (!response.write_u32(1) || !response.write_u32(bsp_time_get_ms()) ||
        !write_vector(response, state.raw.accel, 3) ||
        !write_vector(response, state.raw.gyro, 3) ||
        !response.write_float(state.raw.temp)) {
        return status_e::IO_ERROR;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!response.write_float(state.accel[axis])) return status_e::IO_ERROR;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!response.write_float(state.gyro[axis])) return status_e::IO_ERROR;
    }
    for (int index = 0; index < 4; ++index) {
        if (!response.write_float(state.q[index])) return status_e::IO_ERROR;
    }
    return response.write_float(state.roll) &&
        response.write_float(state.pitch) &&
        response.write_float(state.yaw) &&
        response.write_float(state.yaw_total_angle) &&
        response.write_u32(
            (state.converged ? 1U : 0U) | (state.stationary ? 2U : 0U)
        )
        ? status_e::OK : status_e::IO_ERROR;
}

}

std::uint32_t imu_init() {
    hardware_ready = bsp_imu_init() == BSP_STATUS_OK;
    if (!hardware_ready) return BSP_HW_IMU;
    ins::init();
    return 0;
}

status_e process_imu(const request_t &request, response_t &response) {
    switch (static_cast<command_e>(request.command)) {
        case command_e::STATUS:
            return request.payload_size == 0
                ? write_status(response) : status_e::INVALID_ARGUMENT;
        case command_e::READ_STATE:
            return request.payload_size == 0
                ? write_state(response) : status_e::INVALID_ARGUMENT;
        case command_e::START_CALIBRATION:
            if (request.payload_size != 0) return status_e::INVALID_ARGUMENT;
            switch (ins::start_calibration()) {
                case ins::calibration_result_e::OK: return status_e::OK;
                case ins::calibration_result_e::NOT_READY: return status_e::NOT_READY;
                case ins::calibration_result_e::BUSY: return status_e::BUSY;
            }
            return status_e::IO_ERROR;
        case command_e::CANCEL_CALIBRATION:
            if (request.payload_size != 0) return status_e::INVALID_ARGUMENT;
            if (!hardware_ready || !ins::ready()) return status_e::NOT_READY;
            return ins::cancel_calibration()
                ? status_e::OK : status_e::INVALID_ARGUMENT;
        default:
            return status_e::UNKNOWN_COMMAND;
    }
}

}
