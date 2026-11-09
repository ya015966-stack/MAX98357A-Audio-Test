#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"

#define SAMPLE_RATE 16000
#define AMP_BCLK GPIO_NUM_1
#define AMP_LRCK GPIO_NUM_2
#define AMP_DIN  GPIO_NUM_21
#define MIC_BCLK GPIO_NUM_38
#define MIC_WS   GPIO_NUM_39
#define MIC_DOUT GPIO_NUM_40

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

static esp_err_t init_speaker(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (err != ESP_OK) return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AMP_BCLK,
            .ws = AMP_LRCK,
            .dout = AMP_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false }
        }
    };
    err = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (err != ESP_OK) return err;
    return i2s_channel_enable(tx_handle);
}

static void play_test_tone(void)
{
    int16_t samples[256];
    const float two_pi = 6.28318530718f;
    const int total_samples = SAMPLE_RATE / 2; // 0.5 second
    int written_samples = 0;

    while (written_samples < total_samples) {
        int count = total_samples - written_samples;
        if (count > 256) count = 256;

        for (int i = 0; i < count; ++i) {
            float phase = two_pi * 440.0f * (float)(written_samples + i) / SAMPLE_RATE;
            samples[i] = (int16_t)(2500.0f * sinf(phase));
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(tx_handle, samples, count * sizeof(int16_t),
                                          &bytes_written, portMAX_DELAY);
        if (err != ESP_OK || bytes_written == 0) {
            printf("SPEAKER_WRITE_ERROR: %s\n", esp_err_to_name(err));
            return;
        }
        written_samples += bytes_written / sizeof(int16_t);
    }
    printf("SPEAKER_TONE_SENT\n");
}

static esp_err_t init_microphone(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (err != ESP_OK) return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK,
            .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_DOUT,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false }
        }
    };
    err = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (err != ESP_OK) return err;
    return i2s_channel_enable(rx_handle);
}

static void microphone_monitor(void)
{
    int32_t samples[512];
    while (1) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(rx_handle, samples, sizeof(samples),
                                         &bytes_read, 1000);
        if (err != ESP_OK) {
            printf("MIC_READ_ERROR: %s\n", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        size_t count = bytes_read / sizeof(int32_t);
        uint64_t sum_sq = 0;
        for (size_t i = 0; i < count; ++i) {
            int32_t value = samples[i] >> 8; // INMP441 24-bit sample in 32-bit slot
            int64_t v = value;
            sum_sq += (uint64_t)(v * v);
        }

        float rms = count ? sqrtf((float)sum_sq / (float)count) : 0.0f;
        printf("MIC_RMS: %.1f | bytes: %u\n", rms, (unsigned)bytes_read);
    }
}

void app_main(void)
{
    printf("\n===============================\n");
    printf("RAVA AUDIO DIAGNOSTIC\n");
    printf("ESP32-S3 + MAX98357A + INMP441\n");
    printf("===============================\n");

    printf("PSRAM: %u bytes\n", (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));

    printf("SPEAKER_INIT...\n");
    esp_err_t err = init_speaker();
    if (err == ESP_OK) {
        printf("SPEAKER_INIT_OK\n");
        play_test_tone();
    } else {
        printf("SPEAKER_INIT_FAIL: %s\n", esp_err_to_name(err));
    }

    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }

    printf("MIC_INIT...\n");
    err = init_microphone();
    if (err == ESP_OK) {
        printf("MIC_INIT_OK - speak or make a gentle sound near the mic\n");
        microphone_monitor();
    } else {
        printf("MIC_INIT_FAIL: %s\n", esp_err_to_name(err));
    }

    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
