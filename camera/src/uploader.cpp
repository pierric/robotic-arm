#include <stdio.h>
#include <cstring>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <esp_http_client.h>
#include <picojson.h>
#include <base64.h>

#include "http.h"

static const char * TAG = "uploader";

static constexpr size_t buffer_max = 128 * 1024;
static esp_http_client_handle_t http_client;

using namespace std;

static int _filter(const struct dirent *item)
{
    size_t flen = strlen(item->d_name);
    if (flen <= 4) return 0;
    
    const char *postfix = item->d_name + max((size_t)0, flen-4);
    return !strncasecmp(postfix, ".jpg", 4);
}

static esp_err_t _send(esp_http_client_handle_t http_client, double timestamp, const string& image)
{
    picojson::object obj;
    obj["time_stamp"] = picojson::value(timestamp);
    obj["image"] = picojson::value(Base64::encode(image));

    std::string payload = picojson::value(obj).serialize();
    esp_http_client_set_post_field(http_client, payload.c_str(), payload.length());
    esp_err_t err = esp_http_client_perform(http_client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        return err;
    }
    else {
        int rc = esp_http_client_get_status_code(http_client);
        if (rc != 200 && rc != 201) {
            ESP_LOGW(TAG, "HTTP POST request failed [%d]: %s", rc, getHttpContent(http_client).c_str());
            return rc;
        }
    }
    return ESP_OK;
}

void uploadTask(void * pvParameters)
{
    struct stat st;
    // allocate a buffer for reading files
    // will never free it as this task runs for ever.
    char *buffer = (char *)malloc(buffer_max);
    char fileFullPath[264];
    http_client = initHttpClient();
    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_method(http_client, HTTP_METHOD_POST);
    esp_http_client_set_url(http_client, "/camera");

    while(true) {
        struct dirent **namelist;
        int cnt = scandir("/sdcard", &namelist, _filter, alphasort);

        if (cnt == 0) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "Found %d jpg items.", cnt);
        for (int i=0; i<cnt; ++i) {
            sprintf(fileFullPath, "/sdcard/%s", namelist[i]->d_name);
            
            if (0 != stat(fileFullPath, &st)) {
                ESP_LOGW(TAG, "Failed to stat file: '%s'", fileFullPath);
                continue;
            }
            if (st.st_size > buffer_max) {
                ESP_LOGW(TAG, "Image file is too big: '%s' %lu bytes", fileFullPath, st.st_size);
                continue;
            }
            FILE *fp = fopen(fileFullPath, "r");
            size_t num_read = fread(buffer, buffer_max, 1, fp);
            fclose(fp);

            double timestamp = 0;
            if (sscanf(namelist[i]->d_name, "%lf", &timestamp) < 1) {
                ESP_LOGW(TAG, "Failed to read the timestamp from the image file name: %s", namelist[i]->d_name);
                continue;
            };

            if (ESP_OK == _send(http_client, timestamp, std::string(buffer, num_read))) {
                unlink(fileFullPath);
            }

            if (i % 5 == 0) {
                ESP_LOGI(TAG, "%d / %d uploaded", i, cnt);
            }
        }
        free(namelist);
    }
}