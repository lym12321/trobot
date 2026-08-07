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
#include "utils/logger.h"

namespace debug_mode {
namespace {

constexpr std::size_t frame_header_size = 24;
constexpr std::size_t frame_crc_offset = 20;
constexpr std::size_t request_queue_depth = 4;
static_assert(BSP_USB_WINUSB_REQUEST_SIZE >= frame_header_size + 32U);
constexpr std::uint8_t frame_version = 1;
constexpr std::uint8_t log_service = 1;
constexpr std::uint8_t debug_service = 2;

enum class log_command_e : std::uint8_t {
    INFO = 1,
    BEGIN_EXPORT = 2,
    READ = 3,
    END_EXPORT = 4,
    CLEAR = 5,
};

enum class debug_command_e : std::uint8_t {
    CONNECT = 1,
    DISCONNECT = 2,
};

enum class status_e : std::uint16_t {
    OK = 0,
    BAD_FRAME = 1,
    UNSUPPORTED_VERSION = 2,
    UNKNOWN_SERVICE = 3,
    UNKNOWN_COMMAND = 4,
    INVALID_ARGUMENT = 5,
    BUSY = 6,
    NO_SESSION = 7,
    STORAGE_ERROR = 8,
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
TickType_t last_host_activity;

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

void write_u16(std::uint8_t *data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value);
    data[1] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::uint8_t *data, std::uint32_t value) {
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

std::size_t build_frame(
    std::uint8_t *frame,
    std::uint8_t service,
    std::uint8_t command,
    status_e status,
    std::uint32_t request_id,
    const std::uint8_t *payload = nullptr,
    std::size_t payload_size = 0
) {
    std::memcpy(frame, "TDBG", 4);
    frame[4] = frame_version;
    frame[5] = service;
    frame[6] = command;
    frame[7] = 0;
    write_u16(frame + 8, static_cast<std::uint16_t>(status));
    write_u16(frame + 10, 0);
    write_u32(frame + 12, request_id);
    write_u32(frame + 16, static_cast<std::uint32_t>(payload_size));
    if (payload_size > 0 && payload != frame + frame_header_size) {
        std::memmove(frame + frame_header_size, payload, payload_size);
    }

    std::uint32_t checksum = crc32(0, frame, frame_crc_offset);
    checksum = crc32(checksum, frame + frame_header_size, payload_size);
    write_u32(frame + frame_crc_offset, checksum);
    return frame_header_size + payload_size;
}

void send_frame(
    std::uint8_t service,
    std::uint8_t command,
    status_e status,
    std::uint32_t request_id,
    const std::uint8_t *payload = nullptr,
    std::size_t payload_size = 0
) {
    const std::size_t response_size = build_frame(
        response_buffer.data(), service, command, status, request_id, payload, payload_size
    );
    (void) bsp_usb_winusb_send(
        response_buffer.data(), static_cast<std::uint16_t>(response_size)
    );
}

status_e status_from_flash(logger::flash::result_e result) {
    switch (result) {
        case logger::flash::result_e::OK: return status_e::OK;
        case logger::flash::result_e::INVALID_ARGUMENT: return status_e::INVALID_ARGUMENT;
        case logger::flash::result_e::BUSY: return status_e::BUSY;
        case logger::flash::result_e::NO_SESSION: return status_e::NO_SESSION;
        case logger::flash::result_e::STORAGE_ERROR: return status_e::STORAGE_ERROR;
    }
    return status_e::STORAGE_ERROR;
}

void send_info(std::uint8_t command, std::uint32_t request_id) {
    logger::flash::info_t info {};
    const status_e status = status_from_flash(logger::flash::get_info(info));
    if (status != status_e::OK) {
        send_frame(log_service, command, status, request_id);
        return;
    }

    std::uint8_t payload[32];
    write_u32(payload, 1);
    write_u32(
        payload + 4,
        (info.storage_ready ? 1U : 0U) | (info.export_active ? 2U : 0U)
    );
    write_u32(payload + 8, info.total_size);
    write_u32(payload + 12, info.used_size);
    write_u32(payload + 16, info.snapshot_size);
    write_u32(payload + 20, info.boot_id);
    write_u32(payload + 24, info.next_sequence);
    write_u32(payload + 28, info.queued_records);
    send_frame(log_service, command, status_e::OK, request_id, payload, sizeof(payload));
}

void begin_export(std::uint8_t command, std::uint32_t request_id) {
    logger::flash::export_t snapshot {};
    const status_e status = status_from_flash(logger::flash::begin_export(snapshot));
    if (status != status_e::OK) {
        send_frame(log_service, command, status, request_id);
        return;
    }

    std::uint8_t payload[8];
    write_u32(payload, snapshot.session);
    write_u32(payload + 4, snapshot.size);
    send_frame(log_service, command, status_e::OK, request_id, payload, sizeof(payload));
}

void read_export(
    std::uint8_t command,
    std::uint32_t request_id,
    const std::uint8_t *payload,
    std::size_t payload_size
) {
    if (payload_size != 12) {
        send_frame(log_service, command, status_e::INVALID_ARGUMENT, request_id);
        return;
    }

    const std::uint32_t session = read_u32(payload);
    const std::uint32_t offset = read_u32(payload + 4);
    const std::uint32_t requested_size = read_u32(payload + 8);
    // Keep the FS transfer below a full response buffer so it ends on a short packet.
    constexpr std::size_t max_read_size =
        BSP_USB_WINUSB_RESPONSE_SIZE - frame_header_size - 8U - 4U;
    if (requested_size == 0 || requested_size > max_read_size ||
        requested_size % 4U != 0U || offset % 4U != 0U) {
        send_frame(log_service, command, status_e::INVALID_ARGUMENT, request_id);
        return;
    }

    write_u32(response_buffer.data() + frame_header_size, session);
    write_u32(response_buffer.data() + frame_header_size + 4, offset);
    std::size_t read_size = 0;
    const status_e status = status_from_flash(logger::flash::read_export(
        session, offset, response_buffer.data() + frame_header_size + 8,
        requested_size, read_size
    ));
    if (status != status_e::OK) {
        send_frame(log_service, command, status, request_id);
        return;
    }

    const std::size_t response_size = build_frame(
        response_buffer.data(), log_service, command, status_e::OK, request_id,
        response_buffer.data() + frame_header_size, 8U + read_size
    );
    (void) bsp_usb_winusb_send(
        response_buffer.data(), static_cast<std::uint16_t>(response_size)
    );
}

void end_export(
    std::uint8_t command,
    std::uint32_t request_id,
    const std::uint8_t *payload,
    std::size_t payload_size
) {
    if (payload_size != 4) {
        send_frame(log_service, command, status_e::INVALID_ARGUMENT, request_id);
        return;
    }
    send_frame(
        log_service, command,
        status_from_flash(logger::flash::end_export(read_u32(payload))), request_id
    );
}

void clear_logs(
    std::uint8_t command,
    std::uint32_t request_id,
    const std::uint8_t *payload,
    std::size_t payload_size
) {
    if (payload_size != 4 || std::memcmp(payload, "CLR!", 4) != 0) {
        send_frame(log_service, command, status_e::INVALID_ARGUMENT, request_id);
        return;
    }
    send_frame(
        log_service, command, status_from_flash(logger::flash::clear()), request_id
    );
}

void process_log_command(
    std::uint8_t command,
    std::uint32_t request_id,
    const std::uint8_t *payload,
    std::size_t payload_size
) {
    switch (static_cast<log_command_e>(command)) {
        case log_command_e::INFO:
            if (payload_size == 0) send_info(command, request_id);
            else send_frame(log_service, command, status_e::INVALID_ARGUMENT, request_id);
            break;
        case log_command_e::BEGIN_EXPORT:
            if (payload_size == 0) begin_export(command, request_id);
            else send_frame(log_service, command, status_e::INVALID_ARGUMENT, request_id);
            break;
        case log_command_e::READ:
            read_export(command, request_id, payload, payload_size);
            break;
        case log_command_e::END_EXPORT:
            end_export(command, request_id, payload, payload_size);
            break;
        case log_command_e::CLEAR:
            clear_logs(command, request_id, payload, payload_size);
            break;
        default:
            send_frame(log_service, command, status_e::UNKNOWN_COMMAND, request_id);
            break;
    }
}

void process_debug_command(
    std::uint8_t command,
    std::uint32_t request_id,
    std::size_t payload_size
) {
    if (payload_size != 0) {
        send_frame(debug_service, command, status_e::INVALID_ARGUMENT, request_id);
        return;
    }

    switch (static_cast<debug_command_e>(command)) {
        case debug_command_e::CONNECT:
            host_connected = true;
            last_host_activity = xTaskGetTickCount();
            send_frame(debug_service, command, status_e::OK, request_id);
            break;
        case debug_command_e::DISCONNECT:
            send_frame(debug_service, command, status_e::OK, request_id);
            host_connected = false;
            break;
        default:
            send_frame(debug_service, command, status_e::UNKNOWN_COMMAND, request_id);
            break;
    }
}

void update_led(bool ready) {
    if (!ready) return;

    const TickType_t now = xTaskGetTickCount();
    if (!bsp_usb_is_configured() ||
        (host_connected && now - last_host_activity >= pdMS_TO_TICKS(5000))) {
        host_connected = false;
    }

    const std::uint32_t phase = bsp_time_get_ms() % 2000U;
    const float ramp = phase < 1000U
        ? static_cast<float>(phase) / 1000.0F
        : static_cast<float>(2000U - phase) / 1000.0F;
    bsp_led_set_hsv(host_connected ? 1.0F / 3.0F : 2.0F / 3.0F, 1.0F, 0.2F * ramp);
}

void process_request(const request_packet_t &packet) {
    const std::uint8_t service = packet.size > 5 ? packet.data[5] : 0;
    const std::uint8_t command = packet.size > 6 ? packet.data[6] : 0;
    const std::uint32_t request_id = packet.size >= 16 ? read_u32(packet.data.data() + 12) : 0;
    if (packet.size < frame_header_size || std::memcmp(packet.data.data(), "TDBG", 4) != 0) {
        send_frame(service, command, status_e::BAD_FRAME, request_id);
        return;
    }
    if (packet.data[4] != frame_version) {
        send_frame(service, command, status_e::UNSUPPORTED_VERSION, request_id);
        return;
    }

    const std::size_t payload_size = read_u32(packet.data.data() + 16);
    if (payload_size != packet.size - frame_header_size || packet.data[7] != 0 ||
        read_u16(packet.data.data() + 8) != 0 || read_u16(packet.data.data() + 10) != 0) {
        send_frame(service, command, status_e::BAD_FRAME, request_id);
        return;
    }
    std::uint32_t checksum = crc32(0, packet.data.data(), frame_crc_offset);
    checksum = crc32(checksum, packet.data.data() + frame_header_size, payload_size);
    if (read_u32(packet.data.data() + frame_crc_offset) != checksum) {
        send_frame(service, command, status_e::BAD_FRAME, request_id);
        return;
    }

    if (service == debug_service) {
        process_debug_command(command, request_id, payload_size);
        return;
    }
    if (service == log_service) {
        process_log_command(
            command, request_id, packet.data.data() + frame_header_size, payload_size
        );
        if (host_connected) last_host_activity = xTaskGetTickCount();
        return;
    }
    send_frame(service, command, status_e::UNKNOWN_SERVICE, request_id);
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

bool init() {
    request_queue = xQueueCreateStatic(
        request_queue_depth, sizeof(request_packet_t), request_queue_storage.data(),
        &request_queue_buffer
    );
    return bsp_usb_winusb_set_callback(winusb_receive_callback) == BSP_STATUS_OK;
}

[[noreturn]] void run(bool ready) {
    request_packet_t packet;
    update_led(ready);
    for (;;) {
        if (request_queue == nullptr) {
            bsp_iwdg_refresh();
            update_led(ready);
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
        update_led(ready);
        bsp_iwdg_refresh();
    }
}

}
