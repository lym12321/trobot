#include "debug_internal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp/bsp.h"
#include "bsp/can.h"
#include "bsp/sys.h"

namespace debug_mode::internal {
namespace {

constexpr std::size_t tracked_id_limit = 16;
constexpr std::uint32_t bitrate = 1000000;

enum class command_e : std::uint8_t {
    STATUS = 1,
    START = 2,
    READ = 3,
    STOP = 4,
};

struct sample_t {
    std::uint32_t id;
    std::uint32_t last_timestamp_ms;
    std::uint32_t count;
    std::uint32_t recent_count;
    std::uint8_t len;
    bool occupied;
    std::array<std::uint8_t, BSP_CAN_BUFFER_SIZE> data;
};

std::array<std::array<sample_t, tracked_id_limit>, BSP_CAN_DEVICE_COUNT> samples;
volatile std::uint32_t active_mask;
volatile std::uint32_t captured[BSP_CAN_DEVICE_COUNT];
volatile std::uint32_t missed[BSP_CAN_DEVICE_COUNT];
std::uint32_t available_mask;
std::uint32_t capture_started_ms;
std::uint32_t capture_elapsed_ms;
std::uint32_t read_sampled_elapsed_ms;
std::uint32_t status_sampled_elapsed_ms;
std::array<std::uint32_t, BSP_CAN_DEVICE_COUNT> status_captured_base;
std::array<bsp_can_stats_t, BSP_CAN_DEVICE_COUNT> stats_base;

std::uint32_t now_ms() {
    return static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

std::uint32_t rate_millihz(std::uint32_t count, std::uint32_t elapsed_ms) {
    if (count == 0 || elapsed_ms == 0) return 0;
    const auto value = static_cast<std::uint64_t>(count) * 1000000U / elapsed_ms;
    return value <= UINT32_MAX ? static_cast<std::uint32_t>(value) : UINT32_MAX;
}

void receive(
    bsp_can_e device,
    std::uint32_t id,
    const std::uint8_t *data,
    std::size_t len
) {
    const std::uint32_t device_mask = 1U << static_cast<std::uint32_t>(device);
    if ((active_mask & device_mask) == 0 || len > BSP_CAN_BUFFER_SIZE) return;

    const std::uint32_t timestamp_ms = static_cast<std::uint32_t>(
        xTaskGetTickCountFromISR() * portTICK_PERIOD_MS
    );
    captured[device] = captured[device] + 1U;

    sample_t *empty = nullptr;
    sample_t *sample = nullptr;
    for (auto &candidate : samples[device]) {
        if (candidate.occupied && candidate.id == id) {
            sample = &candidate;
            break;
        }
        if (!candidate.occupied && empty == nullptr) empty = &candidate;
    }
    if (sample == nullptr) sample = empty;
    if (sample == nullptr) {
        missed[device] = missed[device] + 1U;
        return;
    }

    if (!sample->occupied) {
        sample->id = id;
        sample->count = 0;
        sample->recent_count = 0;
        sample->occupied = true;
    }
    sample->last_timestamp_ms = timestamp_ms;
    sample->count++;
    sample->recent_count++;
    sample->len = static_cast<std::uint8_t>(len);
    std::memcpy(sample->data.data(), data, len);
}

status_e write_status(response_t &response) {
    std::array<std::uint32_t, BSP_CAN_DEVICE_COUNT> captured_copy;
    std::array<std::uint32_t, BSP_CAN_DEVICE_COUNT> missed_copy;
    std::array<std::uint32_t, BSP_CAN_DEVICE_COUNT> recent_copy;
    const unsigned long critical = bsp_sys_enter_critical();
    const std::uint32_t active = active_mask;
    const std::uint32_t elapsed = active != 0
        ? now_ms() - capture_started_ms : capture_elapsed_ms;
    const std::uint32_t sample_elapsed = elapsed - status_sampled_elapsed_ms;
    for (std::size_t i = 0; i < BSP_CAN_DEVICE_COUNT; ++i) {
        captured_copy[i] = captured[i];
        missed_copy[i] = missed[i];
        recent_copy[i] = sample_elapsed != 0
            ? captured[i] - status_captured_base[i] : 0;
        if (sample_elapsed != 0) status_captured_base[i] = captured[i];
    }
    if (sample_elapsed != 0) status_sampled_elapsed_ms = elapsed;
    bsp_sys_exit_critical(critical);

    if (!response.write_u32(1) || !response.write_u32(bitrate) ||
        !response.write_u32(available_mask) || !response.write_u32(active) ||
        !response.write_u32(tracked_id_limit) ||
        !response.write_u32(sample_elapsed)) {
        return status_e::IO_ERROR;
    }

    for (std::size_t i = 0; i < BSP_CAN_DEVICE_COUNT; ++i) {
        const auto stats = bsp_can_stats(static_cast<bsp_can_e>(i));
        if (!response.write_u32(captured_copy[i]) ||
            !response.write_u32(missed_copy[i]) ||
            !response.write_u32(rate_millihz(recent_copy[i], sample_elapsed)) ||
            !response.write_u32(
                stats.rx_fifo_lost_count - stats_base[i].rx_fifo_lost_count
            ) ||
            !response.write_u32(
                stats.tx_error_count - stats_base[i].tx_error_count
            ) ||
            !response.write_u32(
                stats.rx_error_count - stats_base[i].rx_error_count
            ) ||
            !response.write_u32(
                stats.bus_off_count - stats_base[i].bus_off_count
            ) ||
            !response.write_u32(static_cast<std::uint32_t>(stats.last_status))) {
            return status_e::IO_ERROR;
        }
    }
    return status_e::OK;
}

status_e start(const request_t &request) {
    if (request.payload_size != 4) return status_e::INVALID_ARGUMENT;
    const std::uint32_t mask = read_u32(request.payload);
    if (mask == 0 || (mask & ~0x7U) != 0 || (mask & ~available_mask) != 0) {
        return status_e::INVALID_ARGUMENT;
    }

    unsigned long critical = bsp_sys_enter_critical();
    active_mask = 0;
    for (std::size_t i = 0; i < BSP_CAN_DEVICE_COUNT; ++i) {
        captured[i] = 0;
        missed[i] = 0;
        for (auto &sample : samples[i]) sample.occupied = false;
    }
    capture_started_ms = now_ms();
    capture_elapsed_ms = 0;
    read_sampled_elapsed_ms = 0;
    status_sampled_elapsed_ms = 0;
    status_captured_base.fill(0);
    bsp_sys_exit_critical(critical);

    for (std::size_t i = 0; i < BSP_CAN_DEVICE_COUNT; ++i) {
        stats_base[i] = bsp_can_stats(static_cast<bsp_can_e>(i));
    }
    critical = bsp_sys_enter_critical();
    active_mask = mask;
    bsp_sys_exit_critical(critical);
    return status_e::OK;
}

status_e read(response_t &response) {
    const unsigned long critical = bsp_sys_enter_critical();
    const std::uint32_t active = active_mask;
    const std::uint32_t elapsed = active != 0
        ? now_ms() - capture_started_ms : capture_elapsed_ms;
    const std::uint32_t sample_elapsed = elapsed - read_sampled_elapsed_ms;
    if (sample_elapsed != 0) read_sampled_elapsed_ms = elapsed;
    std::array<std::uint32_t, BSP_CAN_DEVICE_COUNT> missed_copy;
    for (std::size_t i = 0; i < BSP_CAN_DEVICE_COUNT; ++i) {
        missed_copy[i] = missed[i];
    }
    bsp_sys_exit_critical(critical);

    if (!response.write_u32(1)) return status_e::IO_ERROR;
    const std::size_t count_offset = response.size();
    if (!response.write_u32(0) || !response.write_u32(sample_elapsed)) {
        return status_e::IO_ERROR;
    }
    for (std::size_t i = 0; i < BSP_CAN_DEVICE_COUNT; ++i) {
        if (!response.write_u32(missed_copy[i])) return status_e::IO_ERROR;
    }

    std::uint32_t count = 0;
    for (std::size_t device = 0; device < BSP_CAN_DEVICE_COUNT; ++device) {
        for (auto &source : samples[device]) {
            sample_t sample;
            const unsigned long sample_critical = bsp_sys_enter_critical();
            const bool occupied = source.occupied;
            if (occupied) {
                sample = source;
                if (sample_elapsed != 0) source.recent_count = 0;
            }
            bsp_sys_exit_critical(sample_critical);
            if (!occupied) continue;

            const std::uint32_t frequency = rate_millihz(
                sample.recent_count, sample_elapsed
            );
            if (!response.write_u8(static_cast<std::uint8_t>(device)) ||
                !response.write_u8(sample.len) || !response.write_u16(0) ||
                !response.write_u32(sample.id) ||
                !response.write_u32(sample.last_timestamp_ms) ||
                !response.write_u32(sample.count) ||
                !response.write_u32(frequency) ||
                !response.write(sample.data.data(), sample.len)) {
                return status_e::IO_ERROR;
            }
            count++;
        }
    }
    return response.set_u32(count_offset, count)
        ? status_e::OK : status_e::IO_ERROR;
}

}

std::uint32_t can_init() {
    std::uint32_t failures = 0;
    for (std::uint32_t i = 0; i < BSP_CAN_DEVICE_COUNT; ++i) {
        const auto device = static_cast<bsp_can_e>(i);
        if (bsp_can_set_mask_callback(device, 0, 0, receive) == BSP_STATUS_OK &&
            bsp_can_init(device) == BSP_STATUS_OK) {
            available_mask |= 1U << i;
        } else {
            failures |= BSP_HW_CAN_1 << i;
        }
    }
    return failures;
}

void can_stop() {
    const unsigned long critical = bsp_sys_enter_critical();
    if (active_mask != 0) capture_elapsed_ms = now_ms() - capture_started_ms;
    active_mask = 0;
    bsp_sys_exit_critical(critical);
}

status_e process_can(const request_t &request, response_t &response) {
    switch (static_cast<command_e>(request.command)) {
        case command_e::STATUS:
            return request.payload_size == 0
                ? write_status(response) : status_e::INVALID_ARGUMENT;
        case command_e::START:
            return start(request);
        case command_e::READ:
            return request.payload_size == 0
                ? read(response) : status_e::INVALID_ARGUMENT;
        case command_e::STOP:
            if (request.payload_size != 0) return status_e::INVALID_ARGUMENT;
            can_stop();
            return status_e::OK;
        default:
            return status_e::UNKNOWN_COMMAND;
    }
}

}
