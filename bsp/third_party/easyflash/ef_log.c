/*
 * This file is part of the EasyFlash Library.
 *
 * Copyright (c) 2015-2019, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Save logs to flash.
 * Created on: 2015-06-04
 */

#include <easyflash.h>

#ifdef EF_USING_LOG

#if defined(EF_USING_LOG) && !defined(LOG_AREA_SIZE)
#error "Please configure log area size (in ef_cfg.h)"
#endif

/* magic code on every sector header. 'EF' is 0xEF30EF30 */
#define LOG_SECTOR_MAGIC               0xEF30EF30
/* sector header size, includes the sector magic code and status magic code */
#define LOG_SECTOR_HEADER_SIZE         12
/* sector header word size,what is equivalent to the total number of sectors header index */
#define LOG_SECTOR_HEADER_WORD_SIZE    3

/**
 * Sector status magic code
 * The sector status is 8B after LOG_SECTOR_MAGIC at every sector header.
 * ==============================================
 * |           header(12B)            | status |
 * ----------------------------------------------
 * | 0xEF30EF30 0xFFFFFFFF 0xFFFFFFFF |  empty |
 * | 0xEF30EF30 0xFEFEFEFE 0xFFFFFFFF |  using |
 * | 0xEF30EF30 0xFEFEFEFE 0xFCFCFCFC |  full  |
 * ==============================================
 *
 * State transition relationship: empty->using->full
 * The FULL status will change to EMPTY after sector clean.
 */
#define SECTOR_STATUS_MAGIC_EMPUT     0xFFFFFFFF
#define SECTOR_STATUS_MAGIC_USING     0xFEFEFEFE
#define SECTOR_STATUS_MAGIC_FULL      0xFCFCFCFC

typedef enum {
    SECTOR_STATUS_EMPUT,
    SECTOR_STATUS_USING,
    SECTOR_STATUS_FULL,
    SECTOR_STATUS_HEADER_ERROR,
    SECTOR_STATUS_IO_ERROR,
} SectorStatus;

typedef enum {
    SECTOR_HEADER_MAGIC_INDEX,
    SECTOR_HEADER_USING_INDEX,
    SECTOR_HEADER_FULL_INDEX,
} SectorHeaderIndex;

/* the stored logs start address and end address. It's like a ring buffer implemented on flash. */
static uint32_t log_start_addr = 0, log_end_addr = 0;
/* saved log area address for flash */
static uint32_t log_area_start_addr = 0;
/* initialize OK flag */
static bool init_ok = false;

#define LOG_SECTOR_COUNT (LOG_AREA_SIZE / EF_ERASE_MIN_SIZE)
static uint8_t sector_status_cache[LOG_SECTOR_COUNT];

static EfErrCode find_start_and_end_addr(bool readonly);
static EfErrCode format_log(void);
static bool log_readonly;
static uint32_t get_next_flash_sec_addr(uint32_t cur_addr);

/**
 * The flash save log function initialize.
 *
 * @return result
 */
EfErrCode ef_log_init(void) {
    return ef_log_open(false);
}

EfErrCode ef_log_open(bool readonly) {
    EfErrCode result = EF_NO_ERR;

    EF_ASSERT(LOG_AREA_SIZE);
    EF_ASSERT(EF_ERASE_MIN_SIZE);
    /* the log area size must be an integral multiple of erase minimum size. */
    EF_ASSERT(LOG_AREA_SIZE % EF_ERASE_MIN_SIZE == 0);
    /* the log area size must be more than twice of EF_ERASE_MIN_SIZE */
    EF_ASSERT(LOG_AREA_SIZE / EF_ERASE_MIN_SIZE >= 2);

#ifdef EF_USING_ENV
    log_area_start_addr = EF_START_ADDR + ENV_AREA_SIZE;
#else
    log_area_start_addr = EF_START_ADDR;
#endif

    /* find the log store start address and end address */
    init_ok = false;
    log_readonly = readonly;
    result = find_start_and_end_addr(readonly);
    if (!readonly && result == EF_ENV_INIT_FAILED) result = format_log();
    if (result != EF_NO_ERR) {
        return result;
    }
    /* initialize OK */
    init_ok = true;

    return result;
}

/**
 * Get flash sector current status.
 *
 * @param addr sector address, this function will auto calculate the sector header address by this address.
 *
 * @return the flash sector current status
 */
static SectorStatus get_sector_status(uint32_t addr) {
    uint32_t header_buf[LOG_SECTOR_HEADER_WORD_SIZE] = {0}, header_addr = 0;
    uint32_t sector_header_magic = 0;
    uint32_t status_full_magic = 0, status_use_magic = 0;

    /* calculate the sector header address */
    header_addr = addr & (~(EF_ERASE_MIN_SIZE - 1));

    if (ef_port_read(header_addr, header_buf, sizeof(header_buf)) == EF_NO_ERR) {
        sector_header_magic = header_buf[SECTOR_HEADER_MAGIC_INDEX];
        status_use_magic = header_buf[SECTOR_HEADER_USING_INDEX];
        status_full_magic = header_buf[SECTOR_HEADER_FULL_INDEX];
    } else {
        EF_DEBUG("Error: Read sector header data error.\n");
        return SECTOR_STATUS_IO_ERROR;
    }

    /* compare header magic code */
    if(sector_header_magic == LOG_SECTOR_MAGIC){
        if((status_use_magic == SECTOR_STATUS_MAGIC_EMPUT) && (status_full_magic == SECTOR_STATUS_MAGIC_EMPUT)) {
            return SECTOR_STATUS_EMPUT;
        } else if((status_use_magic == SECTOR_STATUS_MAGIC_USING) && (status_full_magic == SECTOR_STATUS_MAGIC_EMPUT)) {
             return SECTOR_STATUS_USING;
        } else if (status_use_magic == SECTOR_STATUS_MAGIC_USING &&
                (status_full_magic & SECTOR_STATUS_MAGIC_FULL) == SECTOR_STATUS_MAGIC_FULL) {
             return SECTOR_STATUS_FULL;
        } else {
            return SECTOR_STATUS_HEADER_ERROR;
        }
    } else if (header_buf[SECTOR_HEADER_MAGIC_INDEX] == 0xFFFFFFFF &&
            status_use_magic == 0xFFFFFFFF && status_full_magic == 0xFFFFFFFF) {
        return SECTOR_STATUS_EMPUT;
    } else {
        return SECTOR_STATUS_HEADER_ERROR;
    }

}

/**
 * Write flash sector current status.
 *
 * @param addr sector address, this function will auto calculate the sector header address by this address.
 * @param status sector cur status
 *
 * @return result
 */
static EfErrCode write_sector_status(uint32_t addr, SectorStatus status) {
    uint32_t header, header_addr = 0;

    /* calculate the sector header address */
    header_addr = addr & (~(EF_ERASE_MIN_SIZE - 1));

    /* calculate the sector staus magic */
    switch (status) {
    case SECTOR_STATUS_EMPUT: {
        header = LOG_SECTOR_MAGIC;
        return ef_port_write(header_addr, &header, sizeof(header));
    }
    case SECTOR_STATUS_USING: {
        header = SECTOR_STATUS_MAGIC_USING;
        return ef_port_write(header_addr + sizeof(header), &header, sizeof(header));
    }
    case SECTOR_STATUS_FULL: {
        header = SECTOR_STATUS_MAGIC_FULL;
        return ef_port_write(header_addr + sizeof(header) * 2, &header, sizeof(header));
    }
    default:
        return EF_WRITE_ERR;
    }
}

/**
 * Find the current flash sector using end address by continuous 0xFF.
 *
 * @param addr sector address
 *
 * @return current flash sector using end address
 */
static EfErrCode find_sec_using_end_addr(uint32_t sector_addr, uint32_t *end_addr) {
    uint32_t buf[8];
    uint32_t addr = sector_addr + EF_ERASE_MIN_SIZE;
    const uint32_t start = sector_addr + LOG_SECTOR_HEADER_SIZE;
    while (addr > start) {
        size_t size = addr - start < sizeof(buf) ? addr - start : sizeof(buf);
        addr -= size;
        if (ef_port_read(addr, buf, size) != EF_NO_ERR) return EF_READ_ERR;
        for (size_t i = size / sizeof(*buf); i > 0; i--) {
            if (buf[i - 1] != UINT32_MAX) {
                *end_addr = addr + i * sizeof(*buf);
                return EF_NO_ERR;
            }
        }
    }
    *end_addr = start;
    return EF_NO_ERR;
}

/**
 * Find the log store start address and end address.
 * It's like a ring buffer implemented on flash.
 * The flash log area can be in two states depending on start address and end address:
 *                       state 1                                state 2
 *                   |============|                         |============|
 * log area start--> |############| <-- start address       |############| <-- end address
 *                   |############|                         |   empty    |
 *                   |------------|                         |------------|
 *                   |############|                         |############| <-- start address
 *                   |############|                         |############|
 *                   |------------|                         |------------|
 *                   |     .      |                         |     .      |
 *                   |     .      |                         |     .      |
 *                   |     .      |                         |     .      |
 *                   |------------|                         |------------|
 *                   |############| <-- end address         |############|
 *                   |   empty    |                         |############|
 *  log area end --> |============|                         |============|
 *
 *  LOG_AREA_SIZE = log area end - log area star
 *
 */
static EfErrCode payload_blank(uint32_t addr, bool *blank) {
    uint32_t buf[64];
    *blank = true;
    for (size_t pos = LOG_SECTOR_HEADER_SIZE; pos < EF_ERASE_MIN_SIZE;) {
        const size_t remaining = EF_ERASE_MIN_SIZE - pos;
        const size_t len = remaining < sizeof(buf) ? remaining : sizeof(buf);
        if (ef_port_read(addr + pos, buf, len) != EF_NO_ERR) return EF_READ_ERR;
        for (size_t i = 0; i < len / sizeof(*buf); ++i) {
            if (buf[i] != UINT32_MAX) { *blank = false; return EF_NO_ERR; }
        }
        pos += len;
    }
    return EF_NO_ERR;
}

static EfErrCode find_start_and_end_addr(bool readonly) {
    size_t active = LOG_SECTOR_COUNT, predecessor = LOG_SECTOR_COUNT, bad = LOG_SECTOR_COUNT;
    size_t using_count = 0, full_count = 0, bad_count = 0;
    for (size_t i = 0; i < LOG_SECTOR_COUNT; ++i) {
        const SectorStatus status = get_sector_status(log_area_start_addr + i * EF_ERASE_MIN_SIZE);
        if (status == SECTOR_STATUS_IO_ERROR) return EF_READ_ERR;
        sector_status_cache[i] = (uint8_t) status;
        if (status == SECTOR_STATUS_FULL) ++full_count;
        if (status == SECTOR_STATUS_USING) { active = i; ++using_count; }
        if (status == SECTOR_STATUS_HEADER_ERROR) { bad = i; ++bad_count; }
    }
    if (using_count > 2 || bad_count > 1) return EF_ENV_INIT_FAILED;

    if (using_count == 2) {
        /* The successor is marked USING before sealing its predecessor, and
         * receives no payload until that seal succeeds. */
        for (size_t i = 0; i < LOG_SECTOR_COUNT; ++i) {
            const size_t next = (i + 1) % LOG_SECTOR_COUNT;
            if (sector_status_cache[i] != SECTOR_STATUS_USING ||
                sector_status_cache[next] != SECTOR_STATUS_USING) continue;
            uint32_t addr = log_area_start_addr + next * EF_ERASE_MIN_SIZE;
            bool blank;
            if (payload_blank(addr, &blank) != EF_NO_ERR) return EF_READ_ERR;
            if (!blank) continue;
            if (predecessor != LOG_SECTOR_COUNT) return EF_ENV_INIT_FAILED;
            predecessor = i;
            active = next;
        }
        if (predecessor == LOG_SECTOR_COUNT || bad_count) return EF_ENV_INIT_FAILED;
        sector_status_cache[predecessor] = SECTOR_STATUS_FULL;
        ++full_count;
    } else if (!using_count) {
        if (full_count == LOG_SECTOR_COUNT) return EF_ENV_INIT_FAILED;
        if (full_count) {
            /* The older writer sealed the previous sector before opening the next. */
            for (size_t i = 0; i < LOG_SECTOR_COUNT; ++i) {
                const size_t next = (i + 1) % LOG_SECTOR_COUNT;
                if (sector_status_cache[i] != SECTOR_STATUS_FULL ||
                    sector_status_cache[next] == SECTOR_STATUS_FULL) continue;
                if (active != LOG_SECTOR_COUNT) return EF_ENV_INIT_FAILED;
                active = next;
            }
        } else {
            active = 0;
        }
        if (active == LOG_SECTOR_COUNT || (bad_count && bad != active)) return EF_ENV_INIT_FAILED;
    }

    size_t first = active, preceding = 0;
    while (preceding < LOG_SECTOR_COUNT - 1) {
        size_t prev = (first + LOG_SECTOR_COUNT - 1) % LOG_SECTOR_COUNT;
        if (using_count == 1 && bad_count && prev == bad && prev != (active + 1) % LOG_SECTOR_COUNT) {
            /* Preserve payload in a damaged history sector until rollover. */
            ++full_count;
        } else if (sector_status_cache[prev] != SECTOR_STATUS_FULL) {
            break;
        }
        first = prev;
        ++preceding;
    }
    if (preceding != full_count) return EF_ENV_INIT_FAILED;
    const uint32_t addr = log_area_start_addr + active * EF_ERASE_MIN_SIZE;
    const uint32_t start = addr + LOG_SECTOR_HEADER_SIZE;
    bool erase_active = false;
    if (using_count) {
        if (find_sec_using_end_addr(addr, &log_end_addr) != EF_NO_ERR) return EF_READ_ERR;
        if (bad_count && bad == (active + 1) % LOG_SECTOR_COUNT &&
            preceding == LOG_SECTOR_COUNT - 2 && log_end_addr != addr + EF_ERASE_MIN_SIZE) {
            /* Partial active sector: its successor is the oldest member of a full ring. */
            first = bad;
        }
        /* A read-only record scanner can validate an FF-ending record by its CRC. */
        if (readonly && using_count == 1) log_end_addr = addr + EF_ERASE_MIN_SIZE;
    } else {
        log_end_addr = start;
        bool blank;
        if (payload_blank(addr, &blank) != EF_NO_ERR) return EF_READ_ERR;
        erase_active = !blank || bad_count != 0;
        if (!full_count) {
            /* An all-empty header array can also be an interrupted partition erase. */
            for (size_t i = 1; i < LOG_SECTOR_COUNT; ++i) {
                if (payload_blank(log_area_start_addr + i * EF_ERASE_MIN_SIZE, &blank) != EF_NO_ERR)
                    return EF_READ_ERR;
                if (!blank) return EF_ENV_INIT_FAILED;
            }
            if (readonly && erase_active) return EF_ENV_INIT_FAILED;
        }
    }
    log_start_addr = log_area_start_addr + first * EF_ERASE_MIN_SIZE;
    if (readonly) return EF_NO_ERR;
    if (using_count == 2)
        return write_sector_status(log_area_start_addr + predecessor * EF_ERASE_MIN_SIZE, SECTOR_STATUS_FULL);
    if (using_count == 1) {
        /* Leave a torn rollover target until the writer erases it on its next rotation. */
        return EF_NO_ERR;
    }
    if (erase_active && ef_port_erase(addr, EF_ERASE_MIN_SIZE) != EF_NO_ERR) return EF_ERASE_ERR;
    EfErrCode result = write_sector_status(addr, SECTOR_STATUS_EMPUT);
    return result == EF_NO_ERR ? write_sector_status(addr, SECTOR_STATUS_USING) : result;
}

/**
 * Get log used flash total size.
 *
 * @return log used flash total size. @note NOT contain sector headers
 */
size_t ef_log_get_used_size(void) {
    size_t header_total_num = 0, physical_size = 0;
    /* must be call this function after initialize OK */
    if (!init_ok) {
        return 0;
    }

    if (log_start_addr < log_end_addr) {
        physical_size = log_end_addr - log_start_addr;
    } else {
        physical_size = LOG_AREA_SIZE - (log_start_addr - log_end_addr);
    }

    header_total_num = (physical_size + EF_ERASE_MIN_SIZE - 1) / EF_ERASE_MIN_SIZE;

    return physical_size - header_total_num * LOG_SECTOR_HEADER_SIZE;
}

/**
 * Get log flash total size.
 *
 * @return log flash total size. @note NOT contain sector headers
 */
size_t ef_log_get_total_size(void) {
    size_t header_total_num = 0;
    /* must be call this function after initialize OK */
    if (!init_ok) {
        return 0;
    }

    header_total_num = LOG_AREA_SIZE / EF_ERASE_MIN_SIZE;

    return LOG_AREA_SIZE - header_total_num * LOG_SECTOR_HEADER_SIZE;
}

bool ef_log_confirm_end(size_t used, size_t extra) {
    const size_t offset = log_end_addr % EF_ERASE_MIN_SIZE;
    if (!init_ok || log_readonly || !offset || extra % 4 ||
        extra > EF_ERASE_MIN_SIZE - offset || used != ef_log_get_used_size()) return false;
    log_end_addr += extra;
    return true;
}

/* Logical indices count payload bytes, starting at the oldest sector. */
EfErrCode ef_log_read(size_t index, void *log, size_t size) {
    if (!init_ok) return EF_ENV_INIT_FAILED;
    const size_t used = ef_log_get_used_size();
    if (index > used || size > used - index) return EF_READ_ERR;
    const size_t payload = EF_ERASE_MIN_SIZE - LOG_SECTOR_HEADER_SIZE;
    uint8_t *dst = log;
    while (size) {
        size_t pos = (log_start_addr - log_area_start_addr + index / payload * EF_ERASE_MIN_SIZE) % LOG_AREA_SIZE;
        size_t offset = index % payload;
        size_t len = size < payload - offset ? size : payload - offset;
        if (ef_port_read(log_area_start_addr + pos + LOG_SECTOR_HEADER_SIZE + offset, dst, len) != EF_NO_ERR) {
            return EF_READ_ERR;
        }
        dst += len;
        index += len;
        size -= len;
    }
    return EF_NO_ERR;
}

/**
 * Write log to flash.
 *
 * @param log the log which will be write to flash
 * @param size write bytes size
 *
 * @return result
 */
EfErrCode ef_log_write(const uint32_t *log, size_t size) {
    if (!init_ok || log_readonly) return EF_ENV_INIT_FAILED;
    if (size % sizeof(*log)) return EF_WRITE_ERR;
    while (size) {
        if (log_end_addr % EF_ERASE_MIN_SIZE == 0) {
            uint32_t prev = log_end_addr - sizeof(*log);
            uint32_t next = get_next_flash_sec_addr(prev);
            /* Keep the previous USING marker until its successor is durable. */
            EfErrCode result = ef_port_erase(next, EF_ERASE_MIN_SIZE);
            if (result == EF_NO_ERR) result = write_sector_status(next, SECTOR_STATUS_EMPUT);
            if (result == EF_NO_ERR) result = write_sector_status(next, SECTOR_STATUS_USING);
            if (result == EF_NO_ERR) result = write_sector_status(prev, SECTOR_STATUS_FULL);
            if (result != EF_NO_ERR) {
                init_ok = false;
                return result;
            }
            if (log_start_addr == next) log_start_addr = get_next_flash_sec_addr(next);
            log_end_addr = next + LOG_SECTOR_HEADER_SIZE;
        }
        size_t len = EF_ERASE_MIN_SIZE - log_end_addr % EF_ERASE_MIN_SIZE;
        if (len > size) len = size;
        EfErrCode result = ef_port_write(log_end_addr, log, len);
        if (result != EF_NO_ERR) {
            init_ok = false;
            return result;
        }
        log_end_addr += len;
        log += len / sizeof(*log);
        size -= len;
    }
    return EF_NO_ERR;
}

/**
 * Get next flash sector address.The log total sector like ring buffer which implement by flash.
 *
 * @param cur_addr cur flash address
 *
 * @return next flash sector address
 */
static uint32_t get_next_flash_sec_addr(uint32_t cur_addr) {
    size_t cur_sec_id = (cur_addr - log_area_start_addr) / EF_ERASE_MIN_SIZE;
    size_t sec_total_num = LOG_AREA_SIZE / EF_ERASE_MIN_SIZE;

    if (cur_sec_id + 1 >= sec_total_num) {
        /* return to ring head */
        return log_area_start_addr;
    } else {
        return log_area_start_addr + (cur_sec_id + 1) * EF_ERASE_MIN_SIZE;
    }
}

/**
 * Clean all log which in flash.
 *
 * @return result
 */
EfErrCode ef_log_clean(void) {
    if (!init_ok || log_readonly) return EF_ENV_INIT_FAILED;
    return format_log();
}

static EfErrCode format_log(void) {
    EfErrCode result = EF_NO_ERR;
    uint32_t write_addr = log_area_start_addr;

    /* clean address */
    log_start_addr = log_area_start_addr;
    log_end_addr = log_start_addr + LOG_SECTOR_HEADER_SIZE;
    /* erase log flash area */
    result = ef_port_erase(log_area_start_addr, LOG_AREA_SIZE);
    if (result != EF_NO_ERR) {
        goto exit;
    }
    /* setting first sector is EMPTY to USING */
    result = write_sector_status(write_addr, SECTOR_STATUS_EMPUT);
    if (result != EF_NO_ERR) {
        goto exit;
    }
    result = write_sector_status(write_addr, SECTOR_STATUS_USING);
    if (result != EF_NO_ERR) {
        goto exit;
    }
    write_addr += EF_ERASE_MIN_SIZE;
    /* add sector header */
    while (true) {
        result = write_sector_status(write_addr, SECTOR_STATUS_EMPUT);
        if (result != EF_NO_ERR) {
            goto exit;
        }
        write_addr += EF_ERASE_MIN_SIZE;
        if (write_addr >= log_area_start_addr + LOG_AREA_SIZE) {
            break;
        }
    }

exit:
    if (result != EF_NO_ERR) init_ok = false;
    return result;
}

#endif /* EF_USING_LOG */
