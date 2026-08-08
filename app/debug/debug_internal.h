#pragma once

#include <cstddef>
#include <cstdint>

namespace debug_mode::internal {

constexpr std::size_t frame_header_size = 24;
constexpr std::size_t frame_crc_offset = 20;

enum service_e : std::uint8_t {
    LOG_SERVICE = 1,
    DEBUG_SERVICE = 2,
    CAN_SERVICE = 3,
    IMU_SERVICE = 4,
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
    NOT_READY = 9,
    IO_ERROR = 10,
};

struct request_t {
    std::uint8_t service;
    std::uint8_t command;
    std::uint32_t request_id;
    const std::uint8_t *payload;
    std::size_t payload_size;
};

class response_t {
public:
    response_t(std::uint8_t *data, std::size_t capacity)
        : data_(data), capacity_(capacity) {}

    bool write_u8(std::uint8_t value);
    bool write_u16(std::uint16_t value);
    bool write_u32(std::uint32_t value);
    bool write_float(float value);
    bool write(const void *data, std::size_t size);
    bool set_u32(std::size_t offset, std::uint32_t value);
    bool advance(std::size_t size);

    [[nodiscard]] std::uint8_t *current() const { return data_ + size_; }
    [[nodiscard]] std::size_t remaining() const { return capacity_ - size_; }
    [[nodiscard]] std::size_t size() const { return size_; }

private:
    std::uint8_t *data_;
    std::size_t capacity_;
    std::size_t size_ = 0;
};

std::uint16_t read_u16(const std::uint8_t *data);
std::uint32_t read_u32(const std::uint8_t *data);

bool log_init(bool flash_ready);
status_e process_log(const request_t &request, response_t &response);

std::uint32_t can_init();
void can_stop();
status_e process_can(const request_t &request, response_t &response);

std::uint32_t imu_init();
status_e process_imu(const request_t &request, response_t &response);

}
