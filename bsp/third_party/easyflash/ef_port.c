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
 * Function: Portable interface for each platform.
 * Created on: 2015-01-16
 */

#include <easyflash.h>
#include <stdarg.h>

#include "bsp/bsp.h"
#include "bsp/flash_internal.h"
#include "w25q64.h"

#define EASYFLASH_AREA_END (EF_START_ADDR + ENV_AREA_SIZE + LOG_AREA_SIZE)
#define FLASH_BLOCK_ERASE_SIZE (64U * 1024U)

static bool flash_range_valid(uint32_t addr, size_t size) {
    return size > 0 && addr < EASYFLASH_AREA_END &&
           size <= EASYFLASH_AREA_END - addr &&
           EASYFLASH_AREA_END <= W25Qxx_FlashSize;
}

/* default environment variables set for user */
static const ef_env default_env_set[] = {
    {"ver", "20260106", 8}
};

/**
 * Flash port for hardware initialize.
 *
 * @param default_env default ENV set for user
 * @param default_env_size default ENV size
 *
 * @return result
 */
EfErrCode ef_port_init(ef_env const **default_env, size_t *default_env_size) {
    EfErrCode result = EF_NO_ERR;

    *default_env = default_env_set;
    *default_env_size = sizeof(default_env_set) / sizeof(default_env_set[0]);

    if (OSPI_W25Qxx_Init() != OSPI_W25Qxx_OK) {
        result = EF_ENV_INIT_FAILED;
    }

    return result;
}

/**
 * Read data from flash.
 * @note This operation's units is word.
 *
 * @param addr flash address
 * @param buf buffer to store read data
 * @param size read bytes size
 *
 * @return result
 */
EfErrCode ef_port_read(uint32_t addr, uint32_t *buf, size_t size) {
    if (buf == NULL || !flash_range_valid(addr, size)) return EF_READ_ERR;

    return OSPI_W25Qxx_ReadBuffer((uint8_t *) buf, addr, size) == OSPI_W25Qxx_OK ?
        EF_NO_ERR : EF_READ_ERR;
}

/**
 * Erase data on flash.
 * @note This operation is irreversible.
 * @note This operation's units is different which on many chips.
 *
 * @param addr flash address
 * @param size erase bytes size
 *
 * @return result
 */
EfErrCode ef_port_erase(uint32_t addr, size_t size) {
    if (!flash_range_valid(addr, size) || addr % EF_ERASE_MIN_SIZE != 0 ||
        size % EF_ERASE_MIN_SIZE != 0) {
        return EF_ERASE_ERR;
    }

    uint32_t erase_addr = addr;
    size_t remaining = size;
    while (remaining > 0) {
        size_t erase_size = EF_ERASE_MIN_SIZE;
        int8_t result;
        if (erase_addr % FLASH_BLOCK_ERASE_SIZE == 0 &&
            remaining >= FLASH_BLOCK_ERASE_SIZE) {
            result = OSPI_W25Qxx_BlockErase_64K(erase_addr);
            erase_size = FLASH_BLOCK_ERASE_SIZE;
        } else {
            result = OSPI_W25Qxx_SectorErase(erase_addr);
        }
        if (result != OSPI_W25Qxx_OK) return EF_ERASE_ERR;

        erase_addr += erase_size;
        remaining -= erase_size;
        bsp_iwdg_refresh();
    }

    return EF_NO_ERR;
}
/**
 * Write data to flash.
 * @note This operation's units is word.
 * @note This operation must after erase. @see flash_erase.
 *
 * @param addr flash address
 * @param buf the write data buffer
 * @param size write bytes size
 *
 * @return result
 */
EfErrCode ef_port_write(uint32_t addr, const uint32_t *buf, size_t size) {
    if (buf == NULL || !flash_range_valid(addr, size)) return EF_WRITE_ERR;

    return OSPI_W25Qxx_WriteBuffer((uint8_t *) buf, addr, size) == OSPI_W25Qxx_OK ?
        EF_NO_ERR : EF_WRITE_ERR;
}

/**
 * lock the ENV ram cache
 */
void ef_port_env_lock(void) {
    bsp_flash_internal_lock();
}

/**
 * unlock the ENV ram cache
 */
void ef_port_env_unlock(void) {
    bsp_flash_internal_unlock();
}


/**
 * This function is print flash debug info.
 *
 * @param file the file which has call this function
 * @param line the line number which has call this function
 * @param format output format
 * @param ... args
 *
 */
void ef_log_debug(const char *file, const long line, const char *format, ...) {

#ifdef PRINT_DEBUG

    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);

    /* You can add your code under here. */
    
    va_end(args);

#endif

}

/**
 * This function is print flash routine info.
 *
 * @param format output format
 * @param ... args
 */
void ef_log_info(const char *format, ...) {
    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);

    /* You can add your code under here. */
    
    va_end(args);
}
/**
 * This function is print flash non-package info.
 *
 * @param format output format
 * @param ... args
 */
void ef_print(const char *format, ...) {
    va_list args;

    /* args point to the first variable parameter */
    va_start(args, format);

    /* You can add your code under here. */
    
    va_end(args);
}
