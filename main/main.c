#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

void app_main(void)
{
    printf("\n================================\n");
    printf("RAVA BOOT OK\n");
    printf("ESP32-S3 diagnostic firmware\n");
    printf("PSRAM: disabled in this test\n");
    printf("================================\n");

    while (1) {
        printf("RAVA ALIVE - uptime test running\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
