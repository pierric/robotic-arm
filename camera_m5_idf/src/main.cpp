#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

extern esp_err_t initWifi(void);
extern bool wifiConnected(void);

extern "C" void app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(initWifi());
}