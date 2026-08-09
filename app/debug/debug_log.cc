#include "debug_internal.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "utils/logger.h"

namespace debug_mode::internal {
namespace {

enum class command_e : std::uint8_t {
    INFO = 1,
    BEGIN_EXPORT = 2,
    READ = 3,
    END_EXPORT = 4,
    CLEAR = 5,
};

bool storage_ready;

status_e status_from_flash(logger::flash::result_e result) {
    switch (result) {
        case logger::flash::result_e::OK: return status_e::OK;
        case logger::flash::result_e::INVALID_ARGUMENT:
            return status_e::INVALID_ARGUMENT;
        case logger::flash::result_e::BUSY: return status_e::BUSY;
        case logger::flash::result_e::NO_SESSION: return status_e::NO_SESSION;
        case logger::flash::result_e::STORAGE_ERROR: return status_e::STORAGE_ERROR;
    }
    return status_e::STORAGE_ERROR;
}

status_e info(response_t &response) {
    logger::flash::info_t value {};
    const status_e status = status_from_flash(logger::flash::get_info(value));
    if (status != status_e::OK) return status;

    response.write_u32(1);
    response.write_u32(
        (value.storage_ready ? 1U : 0U) | (value.export_active ? 2U : 0U)
    );
    response.write_u32(value.total_size);
    response.write_u32(value.used_size);
    response.write_u32(value.snapshot_size);
    response.write_u32(value.boot_id);
    response.write_u32(value.next_sequence);
    response.write_u32(value.queued_records);
    return status_e::OK;
}

status_e begin_export(response_t &response) {
    logger::flash::export_t snapshot {};
    const status_e status = status_from_flash(logger::flash::begin_export(snapshot));
    if (status != status_e::OK) return status;
    response.write_u32(snapshot.session);
    response.write_u32(snapshot.size);
    return status_e::OK;
}

status_e read_export(const request_t &request, response_t &response) {
    if (request.payload_size != 12) return status_e::INVALID_ARGUMENT;

    const std::uint32_t session = read_u32(request.payload);
    const std::uint32_t offset = read_u32(request.payload + 4);
    const std::uint32_t requested_size = read_u32(request.payload + 8);
    // Keep the full-speed transfer below the buffer size so it ends on a short packet.
    const std::size_t max_read_size = response.remaining() - 8U - 4U;
    if (requested_size == 0 || requested_size > max_read_size ||
        requested_size % 4U != 0U || offset % 4U != 0U) {
        return status_e::INVALID_ARGUMENT;
    }

    response.write_u32(session);
    response.write_u32(offset);
    std::size_t read_size = 0;
    const status_e status = status_from_flash(logger::flash::read_export(
        session, offset, response.current(), requested_size, read_size
    ));
    if (status != status_e::OK) return status;
    response.advance(read_size);
    return status_e::OK;
}

}

bool log_init(bool flash_ready) {
    storage_ready = flash_ready && logger::init_flash(logger::NONE);
    return storage_ready;
}

status_e process_log(const request_t &request, response_t &response) {
    if (!storage_ready) return status_e::NOT_READY;

    switch (static_cast<command_e>(request.command)) {
        case command_e::INFO:
            return request.payload_size == 0
                ? info(response) : status_e::INVALID_ARGUMENT;
        case command_e::BEGIN_EXPORT:
            return request.payload_size == 0
                ? begin_export(response) : status_e::INVALID_ARGUMENT;
        case command_e::READ:
            return read_export(request, response);
        case command_e::END_EXPORT:
            if (request.payload_size != 4) return status_e::INVALID_ARGUMENT;
            return status_from_flash(
                logger::flash::end_export(read_u32(request.payload))
            );
        case command_e::CLEAR:
            if (request.payload_size != 4 ||
                std::memcmp(request.payload, "CLR!", 4) != 0) {
                return status_e::INVALID_ARGUMENT;
            }
            return status_from_flash(logger::flash::clear());
        default:
            return status_e::UNKNOWN_COMMAND;
    }
}

}
