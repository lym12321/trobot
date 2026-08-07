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

static EfErrCode find_start_and_end_addr(void);
static uint32_t get_next_flash_sec_addr(uint32_t cur_addr);

/**
 * The flash save log function initialize.
 *
 * @return result
 */
EfErrCode ef_log_init(void) {
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
    result = find_start_and_end_addr();
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
        } else if((status_use_magic == SECTOR_STATUS_MAGIC_USING) && (status_full_magic == SECTOR_STATUS_MAGIC_FULL)) {
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
static uint32_t find_sec_using_end_addr(uint32_t addr) {
/* read section data buffer size */
#define READ_BUF_SIZE                32

    uint32_t sector_start = addr, data_start = addr, continue_ff = 0, read_buf_size = 0, i;
    uint8_t buf[READ_BUF_SIZE];

    EF_ASSERT(READ_BUF_SIZE % 4 == 0);
    /* calculate the sector start and data start address */
    sector_start = addr & (~(EF_ERASE_MIN_SIZE - 1));
    data_start = sector_start + LOG_SECTOR_HEADER_SIZE;

    /* counts continuous 0xFF which is end of sector */
    while (data_start < sector_start + EF_ERASE_MIN_SIZE) {
        if (data_start + READ_BUF_SIZE < sector_start + EF_ERASE_MIN_SIZE) {
            read_buf_size = READ_BUF_SIZE;
        } else {
            read_buf_size = sector_start + EF_ERASE_MIN_SIZE - data_start;
        }
        ef_port_read(data_start, (uint32_t *)buf, read_buf_size);
        for (i = 0; i < read_buf_size; i++) {
            if (buf[i] == 0xFF) {
                continue_ff++;
            } else {
                continue_ff = 0;
            }
        }
        data_start += read_buf_size;
    }
    /* calculate current flash sector using end address */
    if (continue_ff >= EF_ERASE_MIN_SIZE - LOG_SECTOR_HEADER_SIZE) {
        /* from 0 to sec_size all sector is 0xFF, so the sector is empty */
        return sector_start + LOG_SECTOR_HEADER_SIZE;
    } else if (continue_ff >= 4) {
        /* form end_addr - 4 to sec_size length all area is 0xFF, so it's used part of the sector.
         * the address must be word alignment. */
        if (continue_ff % 4 != 0) {
            continue_ff = continue_ff / 4 * 4;
        }
        return sector_start + EF_ERASE_MIN_SIZE - continue_ff;
    } else {
        /* all sector not has continuous 0xFF, so the sector is full */
        return sector_start + EF_ERASE_MIN_SIZE;
    }
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
static EfErrCode find_start_and_end_addr(void) {
    size_t active_sector = LOG_SECTOR_COUNT;
    size_t using_sector_count = 0;

    for (size_t index = 0; index < LOG_SECTOR_COUNT; index++) {
        const uint32_t sector_addr =
            log_area_start_addr + index * EF_ERASE_MIN_SIZE;
        SectorStatus status = get_sector_status(sector_addr);
        if (status == SECTOR_STATUS_IO_ERROR) {
            return EF_READ_ERR;
        }
        if (status == SECTOR_STATUS_HEADER_ERROR) {
            if (ef_port_erase(sector_addr, EF_ERASE_MIN_SIZE) != EF_NO_ERR ||
                    write_sector_status(sector_addr, SECTOR_STATUS_EMPUT) != EF_NO_ERR) {
                return EF_ERASE_ERR;
            }
            status = SECTOR_STATUS_EMPUT;
        }
        sector_status_cache[index] = (uint8_t) status;
        if (status == SECTOR_STATUS_USING) {
            active_sector = index;
            using_sector_count++;
        }
    }

    if (using_sector_count == 0) {
        active_sector = LOG_SECTOR_COUNT;
        for (size_t index = 0; index < LOG_SECTOR_COUNT; index++) {
            const size_t previous =
                (index + LOG_SECTOR_COUNT - 1) % LOG_SECTOR_COUNT;
            if (sector_status_cache[index] == SECTOR_STATUS_EMPUT &&
                    sector_status_cache[previous] == SECTOR_STATUS_FULL) {
                active_sector = index;
                break;
            }
        }
        if (active_sector == LOG_SECTOR_COUNT) {
            for (size_t index = 0; index < LOG_SECTOR_COUNT; index++) {
                if (sector_status_cache[index] == SECTOR_STATUS_EMPUT) {
                    active_sector = index;
                    break;
                }
            }
        }
        if (active_sector == LOG_SECTOR_COUNT) {
            active_sector = 0;
            if (ef_port_erase(log_area_start_addr, EF_ERASE_MIN_SIZE) != EF_NO_ERR) {
                return EF_ERASE_ERR;
            }
        }

        const uint32_t active_addr =
            log_area_start_addr + active_sector * EF_ERASE_MIN_SIZE;
        if (write_sector_status(active_addr, SECTOR_STATUS_EMPUT) != EF_NO_ERR ||
                write_sector_status(active_addr, SECTOR_STATUS_USING) != EF_NO_ERR) {
            return EF_WRITE_ERR;
        }
        sector_status_cache[active_sector] = SECTOR_STATUS_USING;
    } else if (using_sector_count > 1) {
        for (size_t index = 0; index < LOG_SECTOR_COUNT; index++) {
            const size_t next = (index + 1) % LOG_SECTOR_COUNT;
            if (sector_status_cache[index] == SECTOR_STATUS_USING &&
                    sector_status_cache[next] == SECTOR_STATUS_EMPUT) {
                active_sector = index;
                break;
            }
        }
        for (size_t index = 0; index < LOG_SECTOR_COUNT; index++) {
            if (index == active_sector ||
                    sector_status_cache[index] != SECTOR_STATUS_USING) {
                continue;
            }
            const uint32_t sector_addr =
                log_area_start_addr + index * EF_ERASE_MIN_SIZE;
            if (write_sector_status(sector_addr, SECTOR_STATUS_FULL) != EF_NO_ERR) {
                return EF_WRITE_ERR;
            }
            sector_status_cache[index] = SECTOR_STATUS_FULL;
        }
    }

    size_t start_sector = active_sector;
    for (size_t count = 0; count + 1 < LOG_SECTOR_COUNT; count++) {
        const size_t previous =
            (start_sector + LOG_SECTOR_COUNT - 1) % LOG_SECTOR_COUNT;
        if (sector_status_cache[previous] != SECTOR_STATUS_FULL) break;
        start_sector = previous;
    }

    log_start_addr = log_area_start_addr + start_sector * EF_ERASE_MIN_SIZE;
    log_end_addr = find_sec_using_end_addr(
        log_area_start_addr + active_sector * EF_ERASE_MIN_SIZE
    );
    return EF_NO_ERR;
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

    header_total_num = physical_size / EF_ERASE_MIN_SIZE + 1;

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

/**
 * Sequential reading log data. It will ignore sector headers.
 *
 * @param addr address
 * @param log log buffer
 * @param size log size, not contain sector headers.
 *
 * @return result
 */
static EfErrCode log_seq_read(uint32_t addr, uint32_t *log, size_t size) {
    EfErrCode result = EF_NO_ERR;
    size_t read_size = 0;

    while (size) {
        /* move to sector data address */
        if (addr % EF_ERASE_MIN_SIZE == 0) {
            addr += LOG_SECTOR_HEADER_SIZE;
        }
        /* calculate current sector last data size */
        size_t read_size_temp = EF_ERASE_MIN_SIZE - (addr % EF_ERASE_MIN_SIZE);
        if (size < read_size_temp) {
            read_size_temp = size;
        }
        result = ef_port_read(addr, log + read_size / 4, read_size_temp);
        if (result != EF_NO_ERR) {
            return result;
        }
        addr += read_size_temp;
        read_size += read_size_temp;
        size -= read_size_temp;
    }

    return result;
}

/**
 * Calculate flash physical address by log index.
 *
 * @param index log index
 *
 * @return flash physical address
 */
static uint32_t log_index2addr(size_t index) {
    size_t header_total_offset = 0;
    /* total include sector number */
    size_t sector_num = index / (EF_ERASE_MIN_SIZE - LOG_SECTOR_HEADER_SIZE) + 1;

    header_total_offset = sector_num * LOG_SECTOR_HEADER_SIZE;
    uint32_t virtual_addr = log_start_addr + index + header_total_offset;
    if (log_start_addr < log_end_addr) {
        return virtual_addr;
    } else {
        if (log_start_addr + index + header_total_offset < log_area_start_addr + LOG_AREA_SIZE) {
            return virtual_addr;
        } else {
            // the address will restart from the first sector address.
            return virtual_addr - (log_area_start_addr + LOG_AREA_SIZE) + log_area_start_addr;
        }
    }
}

/**
 * Read log from flash.
 *
 * @param index index for saved log.
 *        Minimum index is 0.
 *        Maximum index is ef_log_get_used_size() - 1.
 * @param log the log which will read from flash
 * @param size read bytes size
 *
 * @return result
 */
EfErrCode ef_log_read(size_t index, uint32_t *log, size_t size) {
    EfErrCode result = EF_NO_ERR;
    size_t cur_using_size = ef_log_get_used_size();

    if (!size) {
        return result;
    }

    if (size % 4 != 0) {
        EF_DEBUG("Error: size must be word aligned.");
        return EF_READ_ERR;
    }
    if (index >= cur_using_size) {
        EF_DEBUG("Error: index out of ranges, current using size is %d", cur_using_size);
        return EF_READ_ERR;
    }

    if (index + size > cur_using_size) {
        EF_DEBUG("Warning: Log read size out of bound. Cut read size.\n");
        size = cur_using_size - index;
    }
    /* must be call this function after initialize OK */
    if (!init_ok) {
        return EF_ENV_INIT_FAILED;
    }

    if (log_start_addr < log_end_addr) {
        result = log_seq_read(log_index2addr(index), log, size);
    } else {
        const uint32_t read_addr = log_index2addr(index);
        const size_t physical_size = log_area_start_addr + LOG_AREA_SIZE - read_addr;
        const size_t first_size =
            physical_size - physical_size / EF_ERASE_MIN_SIZE * LOG_SECTOR_HEADER_SIZE;
        if (size <= first_size) {
            result = log_seq_read(read_addr, log, size);
        } else {
            result = log_seq_read(read_addr, log, first_size);
            if (result == EF_NO_ERR) {
                result = log_seq_read(
                    log_area_start_addr, log + first_size / 4, size - first_size
                );
            }
        }
    }

    return result;
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
    EfErrCode result = EF_NO_ERR;
    size_t write_size = 0, writable_size = 0;
    uint32_t write_addr = log_end_addr, erase_addr;
    SectorStatus sector_status;

    EF_ASSERT(size % 4 == 0);
    /* must be call this function after initialize OK */
    if (!init_ok) {
        return EF_ENV_INIT_FAILED;
    }

    sector_status = get_sector_status(write_addr);
    if (sector_status == SECTOR_STATUS_HEADER_ERROR ||
            sector_status == SECTOR_STATUS_IO_ERROR) {
        return EF_WRITE_ERR;
    }
    /* write some log when current sector status is USING and EMPTY */
    if ((sector_status == SECTOR_STATUS_USING) || (sector_status == SECTOR_STATUS_EMPUT)) {
        /* write the already erased but not used area */
        writable_size = EF_ERASE_MIN_SIZE - ((write_addr - log_area_start_addr) % EF_ERASE_MIN_SIZE);
        if (size >= writable_size) {
            result = ef_port_write(write_addr, log, writable_size);
            if (result != EF_NO_ERR) {
                goto exit;
            }
            /* change the current sector status to FULL */
            result = write_sector_status(write_addr, SECTOR_STATUS_FULL);
            if (result != EF_NO_ERR) {
                goto exit;
            }
            write_size += writable_size;
            log_end_addr = write_addr + writable_size;
        } else {
            result = ef_port_write(write_addr, log, size);
            log_end_addr = write_addr + size;
            goto exit;
        }
    }
    /* erase and write remain log */
    while (true) {
        /* calculate next available sector address */
        erase_addr = write_addr = get_next_flash_sec_addr(write_addr - 4);
        /* move the flash log start address to next available sector address */
        if (log_start_addr == erase_addr) {
            log_start_addr = get_next_flash_sec_addr(log_start_addr);
        }
        /* erase sector */
        result = ef_port_erase(erase_addr, EF_ERASE_MIN_SIZE);
        if (result != EF_NO_ERR) {
            goto exit;
        }
        /* change the sector status to EMPTY and USING when write begin sector start address */
        result = write_sector_status(write_addr, SECTOR_STATUS_EMPUT);
        if (result != EF_NO_ERR) {
            goto exit;
        }
        result = write_sector_status(write_addr, SECTOR_STATUS_USING);
        if (result == EF_NO_ERR) {
            write_addr += LOG_SECTOR_HEADER_SIZE;
        } else {
            goto exit;
        }
        if (write_size == size) {
            log_end_addr = write_addr;
            break;
        }
        /* calculate current sector writable data size */
        writable_size = EF_ERASE_MIN_SIZE - LOG_SECTOR_HEADER_SIZE;
        if (size - write_size >= writable_size) {
            result = ef_port_write(write_addr, log + write_size / 4, writable_size);
            if (result != EF_NO_ERR) {
                goto exit;
            }
            /* change the current sector status to FULL */
            result = write_sector_status(write_addr, SECTOR_STATUS_FULL);
            if (result != EF_NO_ERR) {
                goto exit;
            }
            log_end_addr = write_addr + writable_size;
            write_size += writable_size;
            write_addr += writable_size;
        } else {
            result = ef_port_write(write_addr, log + write_size / 4, size - write_size);
            if (result != EF_NO_ERR) {
                goto exit;
            }
            log_end_addr = write_addr + (size - write_size);
            break;
        }
    }

exit:
    return result;
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
    return result;
}

#endif /* EF_USING_LOG */
