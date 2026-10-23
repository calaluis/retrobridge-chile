#include "sd_card_driver.h"

#include <stdlib.h>

#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

struct rb_sd_card {
    sdmmc_card_t* card;
    bool spi;
    int spi_host;
};

/* One card at most: storage for the handle, no allocation. */
static struct rb_sd_card s_card;

static esp_vfs_fat_mount_config_t mount_config(int max_files)
{
    esp_vfs_fat_mount_config_t config = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    config.format_if_mount_failed = false; /* never format a user's card */
    config.max_files = max_files;
    return config;
}

esp_err_t rb_sd_mount_spi(const char* base_path, int sck, int mosi, int miso, int cs, int card_detect,
                          int max_files, rb_sd_card_t** out_card)
{
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus = {
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .sclk_io_num = sck,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t err = spi_bus_initialize((spi_host_device_t)host.slot, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = (spi_host_device_t)host.slot;
    slot.gpio_cs = (gpio_num_t)cs;
    slot.gpio_cd = card_detect < 0 ? SDSPI_SLOT_NO_CD : (gpio_num_t)card_detect;

    const esp_vfs_fat_mount_config_t config = mount_config(max_files);
    sdmmc_card_t* card = NULL;
    err = esp_vfs_fat_sdspi_mount(base_path, &host, &slot, &config, &card);
    if (err != ESP_OK) {
        spi_bus_free((spi_host_device_t)host.slot);
        return err;
    }
    s_card.card = card;
    s_card.spi = true;
    s_card.spi_host = host.slot;
    *out_card = &s_card;
    return ESP_OK;
}

esp_err_t rb_sd_mount_sdmmc(const char* base_path, int width, int max_files, rb_sd_card_t** out_card)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = (uint8_t)width;

    const esp_vfs_fat_mount_config_t config = mount_config(max_files);
    sdmmc_card_t* card = NULL;
    const esp_err_t err = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot, &config, &card);
    if (err != ESP_OK) {
        return err;
    }
    s_card.card = card;
    s_card.spi = false;
    s_card.spi_host = -1;
    *out_card = &s_card;
    return ESP_OK;
}

esp_err_t rb_sd_unmount(const char* base_path, rb_sd_card_t* card)
{
    const esp_err_t err = esp_vfs_fat_sdcard_unmount(base_path, card->card);
    if (card->spi) {
        spi_bus_free((spi_host_device_t)card->spi_host);
    }
    card->card = NULL;
    return err;
}
