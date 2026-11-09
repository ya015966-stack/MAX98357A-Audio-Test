#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "driver/i2s_std.h"

#define SAMPLE_RATE 16000
#define MAX_SECONDS 10
#define MAX_SAMPLES (SAMPLE_RATE * MAX_SECONDS)
#define MIC_BCLK GPIO_NUM_38
#define MIC_WS GPIO_NUM_39
#define MIC_DOUT GPIO_NUM_40
#define AP_SSID "RAVA-AUDIO"
#define AP_PASSWORD "ravaaudio"

static const char *TAG = "RAVA_WAV";
static i2s_chan_handle_t rx_handle;
static int16_t *record_buffer;
static volatile size_t recorded_samples;
static volatile bool recording;
static SemaphoreHandle_t record_lock;

static void put_u16(uint8_t *p, uint16_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff;
}

static void make_wav_header(uint8_t h[44], uint32_t data_size) {
    memset(h, 0, 44);
    memcpy(h, "RIFF", 4); put_u32(h + 4, 36 + data_size); memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4); put_u32(h + 16, 16); put_u16(h + 20, 1);
    put_u16(h + 22, 1); put_u32(h + 24, SAMPLE_RATE); put_u32(h + 28, SAMPLE_RATE * 2);
    put_u16(h + 32, 2); put_u16(h + 34, 16);
    memcpy(h + 36, "data", 4); put_u32(h + 40, data_size);
}

static esp_err_t init_microphone(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &rx_handle), TAG, "I2S channel");
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, .bclk = MIC_BCLK, .ws = MIC_WS,
            .dout = I2S_GPIO_UNUSED, .din = MIC_DOUT,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false }
        }
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_handle, &std_cfg), TAG, "I2S std mode");
    return i2s_channel_enable(rx_handle);
}

static void recording_task(void *arg) {
    int32_t raw[512];
    while (recording && recorded_samples < MAX_SAMPLES) {
        size_t bytes = 0;
        esp_err_t err = i2s_channel_read(rx_handle, raw, sizeof(raw), &bytes, pdMS_TO_TICKS(250));
        if (err != ESP_OK || bytes == 0) continue;
        size_t n = bytes / sizeof(int32_t);
        if (n > MAX_SAMPLES - recorded_samples) n = MAX_SAMPLES - recorded_samples;
        for (size_t i = 0; i < n; ++i) {
            // INMP441: signed 24-bit audio is left-aligned in a 32-bit I2S slot.
            record_buffer[recorded_samples + i] = (int16_t)(raw[i] >> 16);
        }
        recorded_samples += n;
    }
    recording = false;
    ESP_LOGI(TAG, "Recording complete: %u samples", (unsigned)recorded_samples);
    vTaskDelete(NULL);
}

static esp_err_t root_handler(httpd_req_t *req) {
    const char page[] =
        "<!doctype html><html lang='fa' dir='rtl'><meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta charset='utf-8'><title>RAVA Audio</title><body style='font:18px sans-serif;max-width:520px;margin:40px auto;padding:16px'>"
        "<h2>ضبط صدای RAVA</h2><p>میکروفون INMP441 | حداکثر ۱۰ ثانیه</p>"
        "<button style='padding:14px' onclick=\"fetch('/start',{method:'POST'}).then(r=>r.text()).then(alert)\">شروع ضبط</button> "
        "<button style='padding:14px' onclick=\"fetch('/stop',{method:'POST'}).then(r=>r.text()).then(alert)\">توقف ضبط</button>"
        "<p><a href='/download'>دریافت فایل WAV</a></p><p id='s'>برای ضبط، ابتدا شروع را بزنید.</p>"
        "<script>setInterval(()=>fetch('/status').then(r=>r.text()).then(t=>document.getElementById('s').innerText=t),1000)</script>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t start_handler(httpd_req_t *req) {
    if (recording) return httpd_resp_sendstr(req, "ضبط در حال انجام است.");
    if (xSemaphoreTake(record_lock, pdMS_TO_TICKS(100)) != pdTRUE)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Busy");
    recorded_samples = 0;
    recording = true;
    xSemaphoreGive(record_lock);
    if (xTaskCreate(recording_task, "rava_record", 4096, NULL, 5, NULL) != pdPASS) {
        recording = false;
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Task create failed");
    }
    return httpd_resp_sendstr(req, "ضبط شروع شد؛ حداکثر ۱۰ ثانیه.");
}

static esp_err_t stop_handler(httpd_req_t *req) {
    recording = false;
    return httpd_resp_sendstr(req, "درخواست توقف ثبت شد.");
}

static esp_err_t status_handler(httpd_req_t *req) {
    char msg[96];
    snprintf(msg, sizeof(msg), "%s | مدت: %.1f ثانیه | حجم صدا: %u بایت",
             recording ? "در حال ضبط" : "آماده", (double)recorded_samples / SAMPLE_RATE,
             (unsigned)(recorded_samples * sizeof(int16_t)));
    return httpd_resp_sendstr(req, msg);
}

static esp_err_t download_handler(httpd_req_t *req) {
    if (recording) return httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "ابتدا ضبط را متوقف کنید");
    if (recorded_samples == 0) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "هنوز صدایی ضبط نشده است");
    uint32_t data_size = (uint32_t)(recorded_samples * sizeof(int16_t));
    uint8_t header[44];
    make_wav_header(header, data_size);
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=rava-recording.wav");
    ESP_RETURN_ON_ERROR(httpd_resp_send_chunk(req, (const char *)header, sizeof(header)), TAG, "WAV header");
    esp_err_t err = httpd_resp_send_chunk(req, (const char *)record_buffer, data_size);
    if (err != ESP_OK) return err;
    return httpd_resp_send_chunk(req, NULL, 0);
}

static void start_web_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    httpd_uri_t root = { .uri="/", .method=HTTP_GET, .handler=root_handler };
    httpd_uri_t start = { .uri="/start", .method=HTTP_POST, .handler=start_handler };
    httpd_uri_t stop = { .uri="/stop", .method=HTTP_POST, .handler=stop_handler };
    httpd_uri_t status = { .uri="/status", .method=HTTP_GET, .handler=status_handler };
    httpd_uri_t download = { .uri="/download", .method=HTTP_GET, .handler=download_handler };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &start);
    httpd_register_uri_handler(server, &stop);
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &download);
}

static void start_wifi_ap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t ap = { .ap = {
        .ssid = AP_SSID, .ssid_len = sizeof(AP_SSID) - 1,
        .channel = 1, .password = AP_PASSWORD, .max_connection = 2,
        .authmode = WIFI_AUTH_WPA2_PSK
    }};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi AP ready: %s / 192.168.4.1", AP_SSID);
}

void app_main(void) {
    ESP_LOGI(TAG, "RAVA Wi-Fi WAV recorder starting");
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);
    record_lock = xSemaphoreCreateMutex();
    record_buffer = heap_caps_malloc(MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!record_buffer) record_buffer = heap_caps_malloc(MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_8BIT);
    if (!record_buffer) ESP_LOGE(TAG, "Not enough memory for recording buffer");
    else {
        ESP_ERROR_CHECK(init_microphone());
        start_wifi_ap();
        start_web_server();
        ESP_LOGI(TAG, "Connect phone to %s, then open http://192.168.4.1", AP_SSID);
    }
}
