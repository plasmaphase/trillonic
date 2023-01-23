#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "wave.h"

#define MOUNT_POINT "/sdcard"
static const char *TAG = "sdmmc";
static sdmmc_card_t *card;

#define SDIO_SCLK GPIO_NUM_33
#define SDIO_MOSI_CMD GPIO_NUM_38
#define SDIO_MISO_DAT0 GPIO_NUM_1
#define SDIO_DAT1 GPIO_NUM_3
#define SDIO_DAT2 GPIO_NUM_7
#define SDIO_CS_DAT3 GPIO_NUM_10

#define I2S_WS GPIO_NUM_6
#define I2S_BCLK GPIO_NUM_12
#define I2S_DIN GPIO_NUM_14

static i2s_chan_handle_t rx_chan; // I2S rx channel handler
#define EXAMPLE_BUFF_SIZE 2048

static void sd_card_setup(void)
{
    esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = (1024 * 1024 * 1024)};
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    slot_config.width = 4;

    slot_config.clk = SDIO_SCLK;
    slot_config.cmd = SDIO_MOSI_CMD;
    slot_config.d0 = SDIO_MISO_DAT0;
    slot_config.d1 = SDIO_DAT1;
    slot_config.d2 = SDIO_DAT2;
    slot_config.d3 = SDIO_CS_DAT3;

    // Enable internal pullups on enabled pins. The internal pullups
    // are insufficient however, please make sure 10k external pullups are
    // connected on the bus. This is for debug / example purpose only.
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem");

    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
};

static void i2s_example_init(void)
{
    i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

    i2s_std_clk_config_t clk_cfg = {
        .clk_src = I2S_CLK_SRC_DEFAULT,
        .sample_rate_hz = 16000,
        .mclk_multiple = I2S_MCLK_MULTIPLE_512};

    i2s_std_slot_config_t slot_cfg = {
        .data_bit_width = I2S_DATA_BIT_WIDTH_24BIT,
        .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
        .slot_mode = I2S_SLOT_MODE_STEREO,
        .slot_mask = I2S_STD_SLOT_BOTH,
        .ws_width = I2S_DATA_BIT_WIDTH_24BIT,
        .ws_pol = false,
        .bit_shift = true,
        .left_align = false,
        .big_endian = false,
        .bit_order_lsb = false};

    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = clk_cfg,
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, // some codecs may require mclk signal, this example doesn't need it
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    rx_std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &rx_std_cfg));
}

static void i2s_example_read_task(void *args)
{
    wavHdr_t wHdr;
    wavDataHdr_t wDataHdr;
    uint8_t *r_buf = (uint8_t *)calloc(1, EXAMPLE_BUFF_SIZE);
    assert(r_buf); // Check if r_buf allocation success
    size_t r_bytes = 0;
    const char *file_audio = MOUNT_POINT "/wave.wav";
    ESP_LOGI(TAG, "Opening file %s", file_audio);
    FILE *f = fopen(file_audio, "wb");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }

    cfgWave(&wHdr, STEREO, SR_16K, BD_16);
    ESP_LOGI(TAG, "Wave Header Configured");

    fwrite(&wHdr, sizeof(wHdr), 1, f);
    ESP_LOGI(TAG, "Wrote Header to file");

    while (1)
    {
        /* Read i2s data */
        if (i2s_channel_read(rx_chan, r_buf, EXAMPLE_BUFF_SIZE, &r_bytes, 100) == ESP_OK)
        {
            wDataHdr.data_bytes = r_bytes;
            strcpy(&wDataHdr.dataHdr[0], "data");
            fwrite(&wDataHdr, sizeof(wDataHdr), 1, f);
            fwrite(r_buf, r_bytes, 1, f);

            // Get the current position of the file pointer
            int end_pos = ftell(f);
            // Calculate the total size of the file
            int totalSize = end_pos;
            ESP_LOGI(TAG, "RX: %d bytes, FSize: %d", r_bytes, totalSize);
            // Update the chunkSize field in the RIFF header
            wHdr.fileSize = totalSize - 8;
            // Seek back to the beginning of the file
            fseek(f, 0, SEEK_SET);
            // Write the updated header
            //TODO: Should I only write the size, or the entire header every time?
            fwrite(&wHdr, sizeof(wHdr), 1, f);
            // Seek back to the end of the file
            fseek(f, end_pos, SEEK_SET);
            fflush(f);
            fsync(fileno(f));
        }
        else
        {
            printf("Read Task: i2s read failed\n");
        }
        // vTaskDelay(pdMS_TO_TICKS(200));
    }
    free(r_buf);
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // All done, unmount partition and disable SDMMC peripheral
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    ESP_LOGI(TAG, "Card unmounted");

    vTaskDelete(NULL);
}

void app_main(void)
{
    sd_card_setup();

    i2s_example_init();
    ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));

    xTaskCreate(i2s_example_read_task, "i2s_example_read_task", 4096, NULL, 5, NULL);
}
