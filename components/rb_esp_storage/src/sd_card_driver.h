#pragma once

/* C glue over the ESP-IDF microSD and VFS FAT APIs (H3b). Called only by
 * SdCardStorage after validate_for_storage() succeeded: never with an
 * unconfirmed board. Every mount uses format_if_mount_failed = false: a card is
 * never formatted (H3-5). ESP-IDF v6.1 has no read-only mount for SD cards, so
 * read-only use is enforced above: images are only opened with "rb". */

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rb_sd_card rb_sd_card_t;  /* opaque: the mounted card */

/* SPI controller selector, mapped explicitly to SPI2_HOST / SPI3_HOST. One value
 * drives the bus, the SDSPI host and the device so they always agree. */
typedef enum {
    RB_SD_SPI2 = 2,
    RB_SD_SPI3 = 3,
} rb_sd_spi_host_t;

/* ESP_ERR_INVALID_ARG for any other host value. */
esp_err_t rb_sd_mount_spi(const char* base_path, rb_sd_spi_host_t spi_host, int sck, int mosi, int miso, int cs,
                          int card_detect, int max_files, rb_sd_card_t** out_card);

esp_err_t rb_sd_mount_sdmmc(const char* base_path, int width, int max_files, rb_sd_card_t** out_card);

esp_err_t rb_sd_unmount(const char* base_path, rb_sd_card_t* card);

#ifdef __cplusplus
}
#endif
