#include "debug_mode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "bsp/bsp.h"
#include "bsp/led.h"
#include "bsp/time.h"
#include "bsp/usb.h"
#include "debug_internal.h"

namespace debug_mode {
namespace {

using internal::request_t;
using internal::response_t;
using internal::status_e;

constexpr std::size_t request_queue_depth = 4;
constexpr std::uint8_t frame_version = 1;
constexpr std::uint32_t protocol_version = 1;
static_assert(BSP_USB_WINUSB_REQUEST_SIZE >= internal::frame_header_size);
constexpr std::uint32_t service_mask =
    1U << internal::LOG_SERVICE |
    1U << internal::DEBUG_SERVICE |
    1U << internal::CAN_SERVICE |
    1U << internal::IMU_SERVICE;

enum class debug_command_e : std::uint8_t {
    CONNECT = 1,
    DISCONNECT = 2,
};

struct request_packet_t {
    std::uint16_t size;
    std::array<std::uint8_t, BSP_USB_WINUSB_REQUEST_SIZE> data;
};

StaticQueue_t request_queue_buffer;
QueueHandle_t request_queue;
alignas(StaticQueue_t) std::array<
    std::uint8_t, request_queue_depth * sizeof(request_packet_t)
> request_queue_storage;
alignas(4) std::array<std::uint8_t, BSP_USB_WINUSB_RESPONSE_SIZE> response_buffer;
bool host_connected;
bool transport_ready;
TickType_t last_host_activity;
std::uint32_t hardware_failures;

constexpr std::array<std::uint32_t, 256> make_crc32_table() {
    std::array<std::uint32_t, 256> table {};
    for (std::uint32_t index = 0; index < table.size(); ++index) {
        std::uint32_t value = index;
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (value & 1U);
            value = (value >> 1U) ^ (0xEDB88320U & mask);
        }
        table[index] = value;
    }
    return table;
}

constexpr auto crc32_table = make_crc32_table();

void store_u16(std::uint8_t *data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8U);
}

void store_u32(std::uint8_t *data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8U);
    data[2] = static_cast<std::uint8_t>(value >> 16U);
    data[3] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t crc32(std::uint32_t crc, const std::uint8_t *data, std::size_t size) {
    crc = ~crc;
    const std::uint32_t *table = crc32_table.data();
    while (size-- > 0) {
        crc = table[(crc ^ *data++) & 0xFFU] ^ (crc >> 8U);
    }
    return ~crc;
}

void send_frame(const request_t &request, status_e status, response_t &response) {
    const std::size_t payload_size = status == status_e::OK ? response.size() : 0;
    std::uint8_t *frame = response_buffer.data();
    std::memcpy(frame, "TDBG", 4);
    frame[4] = frame_version;
    frame[5] = request.service;
    frame[6] = request.command;
    frame[7] = 0;
    store_u16(frame + 8, static_cast<std::uint16_t>(status));
    store_u16(frame + 10, 0);
    store_u32(frame + 12, request.request_id);
    store_u32(frame + 16, static_cast<std::uint32_t>(payload_size));

    std::uint32_t checksum = crc32(0, frame, internal::frame_crc_offset);
    checksum = crc32(
        checksum, frame + internal::frame_header_size, payload_size
    );
    store_u32(frame + internal::frame_crc_offset, checksum);
    (void) bsp_usb_winusb_send(
        frame, static_cast<std::uint16_t>(internal::frame_header_size + payload_size)
    );
}

status_e process_debug(const request_t &request, response_t &response) {
    if (request.payload_size != 0) return status_e::INVALID_ARGUMENT;

    switch (static_cast<debug_command_e>(request.command)) {
        case debug_command_e::CONNECT:
            if (!response.write_u32(protocol_version) ||
                !response.write_u32(hardware_failures) ||
                !response.write_u32(service_mask)) {
                return status_e::IO_ERROR;
            }
            host_connected = true;
            return status_e::OK;
        case debug_command_e::DISCONNECT:
            internal::can_stop();
            return status_e::OK;
        default:
            return status_e::UNKNOWN_COMMAND;
    }
}

void process_request(const request_packet_t &packet) {
    request_t request {
        .service = packet.size > 5
            ? packet.data[5] : static_cast<std::uint8_t>(0),
        .command = packet.size > 6
            ? packet.data[6] : static_cast<std::uint8_t>(0),
        .request_id = packet.size >= 16
            ? internal::read_u32(packet.data.data() + 12) : 0,
        .payload = nullptr,
        .payload_size = 0,
    };
    response_t response(
        response_buffer.data() + internal::frame_header_size,
        response_buffer.size() - internal::frame_header_size
    );

    if (packet.size < internal::frame_header_size ||
        std::memcmp(packet.data.data(), "TDBG", 4) != 0) {
        send_frame(request, status_e::BAD_FRAME, response);
        return;
    }
    if (packet.data[4] != frame_version) {
        send_frame(request, status_e::UNSUPPORTED_VERSION, response);
        return;
    }

    request.payload_size = internal::read_u32(packet.data.data() + 16);
    request.payload = packet.data.data() + internal::frame_header_size;
    if (request.payload_size != packet.size - internal::frame_header_size ||
        packet.data[7] != 0 ||
        internal::read_u16(packet.data.data() + 8) != 0 ||
        internal::read_u16(packet.data.data() + 10) != 0) {
        send_frame(request, status_e::BAD_FRAME, response);
        return;
    }

    std::uint32_t checksum = crc32(0, packet.data.data(), internal::frame_crc_offset);
    checksum = crc32(checksum, request.payload, request.payload_size);
    if (internal::read_u32(packet.data.data() + internal::frame_crc_offset) != checksum) {
        send_frame(request, status_e::BAD_FRAME, response);
        return;
    }

    last_host_activity = xTaskGetTickCount();
    status_e status;
    switch (request.service) {
        case internal::DEBUG_SERVICE:
            status = process_debug(request, response);
            break;
        case internal::LOG_SERVICE:
            status = internal::process_log(request, response);
            break;
        case internal::CAN_SERVICE:
            status = internal::process_can(request, response);
            break;
        case internal::IMU_SERVICE:
            status = internal::process_imu(request, response);
            break;
        default:
            status = status_e::UNKNOWN_SERVICE;
            break;
    }
    send_frame(request, status, response);
    if (request.service == internal::DEBUG_SERVICE &&
        request.command == static_cast<std::uint8_t>(debug_command_e::DISCONNECT) &&
        status == status_e::OK) {
        host_connected = false;
    }
}

void update_led() {
    if (!transport_ready) {
        bsp_led_set(50, 0, 0);
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    if (!bsp_usb_is_configured() ||
        (host_connected && now - last_host_activity >= pdMS_TO_TICKS(5000))) {
        if (host_connected) internal::can_stop();
        host_connected = false;
    }

    const std::uint32_t phase = bsp_time_get_ms() % 2000U;
    const float ramp = phase < 1000U
        ? static_cast<float>(phase) / 1000.0F
        : static_cast<float>(2000U - phase) / 1000.0F;
    bsp_led_set_hsv(
        host_connected ? 1.0F / 3.0F : 2.0F / 3.0F, 1.0F, 0.2F * ramp
    );
}

void winusb_receive_callback(const std::uint8_t *data, std::size_t size) {
    request_packet_t packet {};
    packet.size = static_cast<std::uint16_t>(size);
    std::memcpy(packet.data.data(), data, size);
    BaseType_t task_woken = pdFALSE;
    (void) xQueueSendFromISR(request_queue, &packet, &task_woken);
    portYIELD_FROM_ISR(task_woken);
}

}

namespace internal {

std::uint16_t read_u16(const std::uint8_t *data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(data[1]) << 8U;
}

std::uint32_t read_u32(const std::uint8_t *data) {
    return static_cast<std::uint32_t>(data[0]) |
           static_cast<std::uint32_t>(data[1]) << 8U |
           static_cast<std::uint32_t>(data[2]) << 16U |
           static_cast<std::uint32_t>(data[3]) << 24U;
}

bool response_t::write_u8(std::uint8_t value) {
    return write(&value, sizeof value);
}

bool response_t::write_u16(std::uint16_t value) {
    std::uint8_t data[2];
    store_u16(data, value);
    return write(data, sizeof data);
}

bool response_t::write_u32(std::uint32_t value) {
    std::uint8_t data[4];
    store_u32(data, value);
    return write(data, sizeof data);
}

bool response_t::write_float(float value) {
    static_assert(sizeof value == sizeof(std::uint32_t));
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return write_u32(bits);
}

bool response_t::write(const void *data, std::size_t size) {
    if (size > remaining()) return false;
    std::memcpy(current(), data, size);
    size_ += size;
    return true;
}

bool response_t::set_u32(std::size_t offset, std::uint32_t value) {
    if (offset > size_ || size_ - offset < sizeof value) return false;
    store_u32(data_ + offset, value);
    return true;
}

bool response_t::advance(std::size_t size) {
    if (size > remaining()) return false;
    size_ += size;
    return true;
}

}

[[noreturn]] void run(std::uint32_t hw_failed) {
    request_queue = xQueueCreateStatic(
        request_queue_depth, sizeof(request_packet_t), request_queue_storage.data(),
        &request_queue_buffer
    );
    transport_ready = (hw_failed & BSP_HW_USB) == 0 &&
        request_queue != nullptr &&
        bsp_usb_winusb_set_callback(winusb_receive_callback) == BSP_STATUS_OK;
    if (!transport_ready) hw_failed |= BSP_HW_USB;
    if (!internal::log_init((hw_failed & BSP_HW_FLASH) == 0)) {
        hw_failed |= BSP_HW_FLASH;
    }
    hw_failed |= internal::can_init();
    hw_failed |= internal::imu_init();
    hardware_failures = hw_failed;

    request_packet_t packet;
    update_led();
    for (;;) {
        if (!transport_ready) {
            bsp_iwdg_refresh();
            update_led();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (xQueueReceive(request_queue, &packet, pdMS_TO_TICKS(50)) == pdTRUE) {
            while (bsp_usb_winusb_tx_busy()) {
                bsp_iwdg_refresh();
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            process_request(packet);
        }
        update_led();
        bsp_iwdg_refresh();
    }
}

}
