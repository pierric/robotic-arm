#include <memory>
#include <ctime>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_timer.h>
#include <esp_camera.h>

#include "manipulator.h"

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static const int SERVER_PORT = 80;
static const char * TAG = "server"; 
static const int FPS = 30;
static constexpr size_t buffer_max_len = 128*1024;

static httpd_handle_t stream_httpd = NULL;

#define ESP_ERROR_CHECK_OR(x, fail) ({                                               \
        esp_err_t err_rc_ = (x);                                                    \
        if (unlikely(err_rc_ != ESP_OK)) {                                          \
            _esp_error_check_failed_without_abort(err_rc_, __FILE__, __LINE__,      \
                                                  __ASSERT_FUNC, #x);               \
            fail;                                                                    \
        }                                                                           \
        err_rc_;                                                                    \
    })

#define ESP_ERROR_CHECK_RETURN(x) ESP_ERROR_CHECK_OR(x, return err_rc_)

struct __attribute__ ((packed)) ChunkHeader {
    char header[4];
    uint32_t size;
    struct timeval timestamp;
    float gripper_state;
};

static esp_err_t stream_handler(httpd_req_t *req)
{
    char part_buf[64];
    struct ChunkHeader header = {{'C', 'H', 'D', 'R'}};
    int64_t last_frame = esp_timer_get_time();
    int64_t fps_counter_last_timestamp = last_frame;
    int32_t fps_counter = 0;

    ESP_ERROR_CHECK_RETURN(httpd_resp_set_type(req, _STREAM_CONTENT_TYPE));
    ESP_ERROR_CHECK_RETURN(httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"));
    ESP_ERROR_CHECK_RETURN(httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY)));

    while (true) {
        if (gettimeofday(&header.timestamp, nullptr) != 0) {
            header.timestamp.tv_sec = time(nullptr);
            header.timestamp.tv_usec = 0;
        }
        header.gripper_state = getManipulatorState();

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Failed to grab frame buffer.");
            return ESP_FAIL;
        }
        if (fb->format != PIXFORMAT_JPEG) {
            ESP_LOGE(TAG, "Wrong image format in the frame buffer.");
            return ESP_FAIL;
        }

        esp_err_t rc;
        size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, fb->len);

        header.size = sizeof(struct ChunkHeader) + fb->len;
        
        ESP_ERROR_CHECK_OR(rc = httpd_resp_send_chunk(req, (const char *)part_buf, hlen), goto ERR);
        ESP_ERROR_CHECK_OR(rc = httpd_resp_send_chunk(req, (const char *)&header, sizeof(header)), goto ERR);
        ESP_ERROR_CHECK_OR(rc = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len), goto ERR);
        ESP_ERROR_CHECK_OR(rc = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY)), goto ERR);
        ERR:

        esp_camera_fb_return(fb);

        if (rc!=ESP_OK) {
            return rc;
        }

        uint64_t now = esp_timer_get_time();
        uint64_t elasp = now - last_frame;
        float perframe = (1000000. / FPS);
        if (elasp < perframe) {
            vTaskDelay((perframe - elasp) / (1000.0 * portTICK_PERIOD_MS));
        }

        last_frame = esp_timer_get_time();

        if (last_frame - fps_counter_last_timestamp > 2000000) {
            ESP_LOGI(TAG, "fps: %d", int(fps_counter/2));
            fps_counter = 0;
            fps_counter_last_timestamp = last_frame;
        }
        fps_counter++;
    }
}

esp_err_t initHttpd()
{
    ESP_LOGI(TAG, "Starting server");
   
    httpd_uri_t stream_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = SERVER_PORT;

    ESP_ERROR_CHECK_RETURN(httpd_start(&stream_httpd, &config));
    ESP_ERROR_CHECK_RETURN(httpd_register_uri_handler(stream_httpd, &stream_uri));
    return ESP_OK;
}
