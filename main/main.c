#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "driver/i2s_std.h"

#define MIC_BCLK   GPIO_NUM_38
#define MIC_WS     GPIO_NUM_39
#define MIC_DOUT   GPIO_NUM_40

#define AMP_BCLK   GPIO_NUM_1
#define AMP_LRC    GPIO_NUM_2
#define AMP_DIN    GPIO_NUM_21

#define SAMPLE_RATE 16000
#define BLOCK_SAMPLES 256
#define PI_F 3.14159265358979323846f

static i2s_chan_handle_t mic_rx;
static i2s_chan_handle_t amp_tx;

static void check_err(esp_err_t err, const char *what)
{
    if (err != ESP_OK) {
        printf("ERROR: %s: %s\n", what, esp_err_to_name(err));
        abort();
    }
}

static void init_microphone(void)
{
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = BLOCK_SAMPLES;

    check_err(i2s_new_channel(&chan_cfg, NULL, &mic_rx), "create mic channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK,
            .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_DOUT,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    check_err(i2s_channel_init_std_mode(mic_rx, &std_cfg), "init mic I2S");
    check_err(i2s_channel_enable(mic_rx), "enable mic I2S");
}

static void init_amplifier(void)
{
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = BLOCK_SAMPLES;

    check_err(i2s_new_channel(&chan_cfg, &amp_tx, NULL), "create amp channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = AMP_BCLK,
            .ws = AMP_LRC,
            .dout = AMP_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    check_err(i2s_channel_init_std_mode(amp_tx, &std_cfg), "init amp I2S");
    check_err(i2s_channel_enable(amp_tx), "enable amp I2S");
}

static void microphone_task(void *arg)
{
    int32_t raw[BLOCK_SAMPLES];
    size_t bytes_read = 0;
    uint32_t report_count = 0;

    while (1) {
        esp_err_t err = i2s_channel_read(
            mic_rx, raw, sizeof(raw), &bytes_read, pdMS_TO_TICKS(250));

        if (err == ESP_OK && bytes_read >= sizeof(int32_t)) {
            size_t count = bytes_read / sizeof(int32_t);
            double sum_sq = 0.0;

            for (size_t i = 0; i < count; i++) {
                // INMP441-style microphones place 24-bit audio in a 32-bit slot.
                int32_t sample16 = raw[i] >> 14;
                sum_sq += (double)sample16 * (double)sample16;
            }

            double rms = sqrt(sum_sq / (double)count);
            report_count++;
            if (report_count >= 16) {
                printf("MIC RMS: %.0f\n", rms);
                report_count = 0;
            }
        } else if (err != ESP_ERR_TIMEOUT) {
            printf("MIC read error: %s\n", esp_err_to_name(err));
        }
    }
}

static void speaker_task(void *arg)
{
    int16_t samples[BLOCK_SAMPLES];
    uint32_t phase_index = 0;
    const float frequency = 440.0f;
    const float phase_step = 2.0f * PI_F * frequency / (float)SAMPLE_RATE;
    const int16_t amplitude = 1400; // quiet initial test level

    while (1) {
        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            samples[i] = (int16_t)(sinf(phase_step * (float)phase_index) * amplitude);
            phase_index++;
            if (phase_index >= SAMPLE_RATE) {
                phase_index = 0;
            }
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            amp_tx, samples, sizeof(samples), &bytes_written, pdMS_TO_TICKS(250));

        if (err != ESP_OK) {
            printf("AMP write error: %s\n", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void app_main(void)
{
    printf("\nRavatronix audio hardware test\n");
    printf("Mic: GPIO38/39/40 | MAX98357A: GPIO1/2/21\n");
    printf("A quiet 440 Hz tone will play. MIC RMS is printed periodically.\n");

    init_microphone();
    init_amplifier();

    xTaskCreate(microphone_task, "microphone_task", 4096, NULL, 5, NULL);
    xTaskCreate(speaker_task, "speaker_task", 4096, NULL, 5, NULL);
}
