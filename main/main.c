#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

void app_main(void)
{
    printf("\n================================\n");
    printf("RAVA BOOT OK\n");
    printf("ESP32-S3 PSRAM diagnostic\n");
    printf("================================\n");

    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    printf("PSRAM total: %u bytes\n", (unsigned)psram_total);
    printf("PSRAM free:  %u bytes\n", (unsigned)psram_free);

    if (psram_total > 0) {
        unsigned char *test = (unsigned char *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        if (test != NULL) {
            for (size_t i = 0; i < 4096; ++i) {
                test[i] = (unsigned char)(i ^ 0xA5);
            }

            int ok = 1;
            for (size_t i = 0; i < 4096; ++i) {
                if (test[i] != (unsigned char)(i ^ 0xA5)) {
                    ok = 0;
                    break;
                }
            }

            printf("PSRAM 4KB read/write test: %s\n", ok ? "PASS" : "FAIL");
            heap_caps_free(test);
        } else {
            printf("PSRAM 4KB allocation: FAIL\n");
        }
    } else {
        printf("PSRAM: NOT DETECTED\n");
    }

    while (1) {
        printf("RAVA PSRAM TEST ALIVE\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
