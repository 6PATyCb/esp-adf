/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2023 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "board.h"
#include "audio_mem.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_log.h"
#include "periph_button.h"
#include "periph_lcd.h"
#include "periph_sdcard.h"
#include "tca9554.h"

static const char *TAG = "AUDIO_BOARD";

static audio_board_handle_t board_handle = 0;

audio_board_handle_t audio_board_init(void)
{
    ESP_LOGW(TAG, "begin audio_board_init!!!");
    if (board_handle) {
        ESP_LOGW(TAG, "The board has already been initialized!");
        return board_handle;
    }
    board_handle = (audio_board_handle_t)audio_calloc(1, sizeof(struct audio_board_handle));
    AUDIO_MEM_CHECK(TAG, board_handle, return NULL);
    //  board_handle->audio_hal = audio_board_codec_init();
    // board_handle->adc_hal = audio_board_adc_init();
    return board_handle;
}

audio_hal_handle_t audio_board_adc_init(void)
{
    audio_hal_codec_config_t audio_codec_cfg = AUDIO_CODEC_DEFAULT_CONFIG();
    audio_codec_cfg.codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE;
    audio_hal_handle_t adc_hal = NULL;
    adc_hal = audio_hal_init(&audio_codec_cfg, &AUDIO_CODEC_ES7148_DEFAULT_HANDLE);
    AUDIO_NULL_CHECK(TAG, adc_hal, return NULL);
    return adc_hal;
}

audio_hal_handle_t audio_board_codec_init(void)
{
    if (PA_ENABLE_GPIO >= 0) {
        gpio_config_t bk_gpio_config = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PA_ENABLE_GPIO};
        gpio_config(&bk_gpio_config);
        gpio_set_level(PA_ENABLE_GPIO, true);
    }
    audio_hal_codec_config_t audio_codec_cfg = AUDIO_CODEC_DEFAULT_CONFIG();
    audio_hal_handle_t codec_hal = audio_hal_init(&audio_codec_cfg, &AUDIO_CODEC_ES7148_DEFAULT_HANDLE);
    AUDIO_NULL_CHECK(TAG, codec_hal, return NULL);
    return codec_hal;
}

static esp_err_t _get_lcd_io_bus(void *bus, esp_lcd_panel_io_spi_config_t *io_config,
                                 esp_lcd_panel_io_handle_t *out_panel_io)
{
    ESP_LOGW(TAG, "begin _get_lcd_io_bus!!!");
    return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)bus, io_config, out_panel_io);
}

static void test_draw_bitmap(esp_lcd_panel_handle_t panel_handle)
{

    uint16_t row_line = ((LCD_V_RES / LCD_BIT_PER_PIXEL) << 1) >> 1;
    uint8_t byte_per_pixel = LCD_BIT_PER_PIXEL / 8;
    uint8_t *color = (uint8_t *)heap_caps_calloc(1, row_line * LCD_H_RES * byte_per_pixel, MALLOC_CAP_DMA);

    for (int j = 0; j < LCD_BIT_PER_PIXEL; j++) {
        for (int i = 0; i < row_line * LCD_H_RES; i++) {
            for (int k = 0; k < byte_per_pixel; k++) {
                color[i * byte_per_pixel + k] = (SPI_SWAP_DATA_TX(BIT(j), LCD_BIT_PER_PIXEL) >> (k * 8)) & 0xff;
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, j * row_line, LCD_H_RES, (j + 1) * row_line, color));
    }
    free(color);
}

void *audio_board_lcd_init(esp_periph_set_handle_t set, void *cb)
{
   // ESP_LOGW(TAG, "begin audio_board_lcd_init!!!");
    if (LCD_CLK_GPIO >= 0) {
        gpio_config_t bk_gpio_config = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << LCD_CLK_GPIO};
        gpio_config(&bk_gpio_config);
        gpio_set_level(LCD_CLK_GPIO, false);
    }
    st77916_vendor_config_t vendor_config = {
        // .init_cmds = lcd_init_cmds,         // Uncomment these line if use custom initialization commands
        // .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(ST77916_lcd_init_cmd_t),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = (void *)&vendor_config,
    };

    ESP_LOGW(TAG, "Initialize QSPI bus!!!");
    const spi_bus_config_t buscfg = ST77916_PANEL_BUS_QSPI_CONFIG(GPIO_NUM_9, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13,
                                                                  GPIO_NUM_14, LCD_H_RES * 80 * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // ESP_LOGW(TAG, "Install panel IO!!!");
    const esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(LCD_CS_GPIO, NULL, NULL);
    periph_lcd_cfg_t cfg = {.io_bus = (void *)SPI2_HOST,
                            .new_panel_io = _get_lcd_io_bus,
                            .lcd_io_cfg = &io_config,
                            .new_lcd_panel = esp_lcd_new_panel_st77916,
                            .lcd_dev_cfg = &panel_config,
                            .rest_cb = NULL,
                            .rest_cb_ctx = NULL,
                            .lcd_swap_xy = LCD_SWAP_XY,
                            .lcd_mirror_x = LCD_MIRROR_X,
                            .lcd_mirror_y = LCD_MIRROR_Y,
                            .lcd_color_invert = LCD_COLOR_INV,
   //                         .vendor_init = esp_lcd_panel_init
    };
    ESP_LOGW(TAG, "before periph_lcd_init!!!");
    esp_periph_handle_t periph_lcd = periph_lcd_init(&cfg);
    AUDIO_NULL_CHECK(TAG, periph_lcd, return NULL);
    ESP_LOGW(TAG, "before esp_periph_start!!!");
    ESP_ERROR_CHECK(esp_periph_start(set, periph_lcd));
    ESP_LOGW(TAG, "before periph_lcd_get_panel_handle!!!");
    return (void *)periph_lcd_get_panel_handle(periph_lcd);
}

esp_err_t audio_board_key_init(esp_periph_set_handle_t set)
{
    periph_button_cfg_t btn_cfg = {
        .gpio_mask = (1ULL << get_input_rec_id()) | (1ULL << get_input_play_id()), // REC BTN & PLAY BTN
    };
    esp_periph_handle_t button_handle = periph_button_init(&btn_cfg);
    AUDIO_NULL_CHECK(TAG, button_handle, return ESP_ERR_ADF_MEMORY_LACK);
    return esp_periph_start(set, button_handle);
}

esp_err_t audio_board_sdcard_init(esp_periph_set_handle_t set, periph_sdcard_mode_t mode)
{
    if (mode != SD_MODE_1_LINE && mode != SD_MODE_4_LINE) {
        ESP_LOGE(TAG, "Current board only support 1-line and 4-line SD mode!");
        return ESP_FAIL;
    }
    periph_sdcard_cfg_t sdcard_cfg = {.root = "/sdcard", .card_detect_pin = get_sdcard_intr_gpio(), .mode = mode};

    // Enable SDCard power
    if (get_sdcard_power_ctrl_gpio() >= 0) {
        gpio_config_t gpio_cfg = {.mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << get_sdcard_power_ctrl_gpio()};
        gpio_config(&gpio_cfg);
        gpio_set_level(get_sdcard_power_ctrl_gpio(), 0);
    }

    esp_periph_handle_t sdcard_handle = periph_sdcard_init(&sdcard_cfg);
    esp_err_t ret = esp_periph_start(set, sdcard_handle);
    int retry_time = 5;
    bool mount_flag = false;
    while (retry_time--) {
        if (periph_sdcard_is_mounted(sdcard_handle)) {
            mount_flag = true;
            break;
        } else {
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
    if (mount_flag == false) {
        ESP_LOGE(TAG, "Sdcard mount failed");
        return ESP_FAIL;
    }
    return ret;
}

audio_board_handle_t audio_board_get_handle(void)
{
    return board_handle;
}

esp_err_t audio_board_deinit(audio_board_handle_t audio_board)
{
    AUDIO_NULL_CHECK(TAG, audio_board, return ESP_FAIL);
    esp_err_t ret = ESP_OK;
    ret |= audio_hal_deinit(audio_board->audio_hal);
    ret |= audio_hal_deinit(audio_board->adc_hal);
    audio_free(audio_board);
    board_handle = NULL;
    return ret;
}
