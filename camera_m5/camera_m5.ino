#include "src/M5TimerCAM.h"
#include "src/utility/exif.h"
#include <sys/time.h>
#include <WiFi.h>
#include <esp_sntp.h>

const gpio_num_t EXT_WAKEUP_PIN = (gpio_num_t) 13;
const char* WIFI_SSID = "FRITZSIFEI";
const char* WIFI_PASS = "asdf1234!!!!";

static RTC_DATA_ATTR bool time_synced = false;

WiFiServer server(80);
static void jpegStream(WiFiClient* client);
static int http_transfer(WiFiClient *client, size_t num, const uint8_t *buf);

void setup() {
    TimerCAM.begin(true);
    TimerCAM.Power.setLed(64);

    if (!TimerCAM.Camera.begin()) {
        Serial.println("Camera Init Fail");
        return;
    }

    TimerCAM.Camera.sensor->set_pixformat(TimerCAM.Camera.sensor, PIXFORMAT_JPEG);
    TimerCAM.Camera.sensor->set_framesize(TimerCAM.Camera.sensor, FRAMESIZE_VGA);
    TimerCAM.Camera.sensor->set_vflip(TimerCAM.Camera.sensor, 1);
    TimerCAM.Camera.sensor->set_hmirror(TimerCAM.Camera.sensor, 0);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.setSleep(false);
    Serial.println("");
    Serial.print("Connecting to ");
    Serial.println(WIFI_SSID);
    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(WIFI_SSID);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    setenv("TZ", "UTC", 1);
    tzset();


    if (!time_synced) {
        Serial.println("initializing SNTP.");
        sntp_set_sync_interval(2 * 3600 * 1000);
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "de.pool.ntp.org");
        sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
        esp_sntp_init();

        int retry = 0;
        const int retry_count = 10;
        while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET
            && ++retry < retry_count) {
            Serial.printf("... Waiting for system time to be set... (%d/%d)\r\n", retry, retry_count);
            delay(2000);
        }
        if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
            Serial.println("... done");
        }
        else {
            Serial.println("... still not ready");
            abort();
        }

        // system time -> RTC
        struct tm timeinfo;
        if(!getLocalTime(&timeinfo)){
            Serial.println("Failed to obtain time");
            abort();
        }
        TimerCAM.Rtc.setDateTime(&timeinfo);
        time_synced = true;
    }
    else {
        // RTC -> system time
        TimerCAM.Rtc.setSystemTimeFromRtc();
    }

    rtc_datetime_t now;
    TimerCAM.Rtc.getDateTime(&now);
    Serial.printf("now (UTC): %d-%d-%d %d-%d-%d\n", now.date.year, now.date.month, now.date.date, now.time.hours, now.time.minutes, now.time.seconds);
    
    server.begin();

    gpio_hold_en((gpio_num_t)POWER_HOLD_PIN);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_ext0_wakeup(EXT_WAKEUP_PIN, 1);
}

void loop() {
    WiFiClient client = server.available();
    if (client) {
        Serial.println("Client connected");
        while (client.connected()) {
            if (client.available()) {
                jpegStream(&client);
            }
        }
        client.stop();
        Serial.println("Client Disconnected.");
    }

    if (digitalRead(EXT_WAKEUP_PIN) == LOW) {
      Serial.println("Going to sleep now");
      TimerCAM.Power.powerOff();
    }

    // keep on, then make a tiny delay before checking the
    // next connection.
    delay(100);
}

// used to image stream
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY     = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

const int INFO_INTERVAL = 60;
const int FPS = 10;
const int expected_frame_time = int(1000. / FPS) + 50;

static void jpegStream(WiFiClient* client) {
    struct timeval now_tv;
    const uint8_t *exif_buf = NULL;
    size_t exif_len = 0, data_offset;
    
    Serial.println("Image stream start");
    client->println("HTTP/1.1 200 OK");
    client->printf("Content-Type: %s\r\n", _STREAM_CONTENT_TYPE);
    client->println("Content-Disposition: inline; filename=capture.jpg");
    client->println("Access-Control-Allow-Origin: *");
    client->println();
    static int64_t last_frame = 0;
    if (!last_frame) {
        last_frame = esp_timer_get_time();
    }

    int interval = INFO_INTERVAL;
    int64_t frame_time_acc = 0;
    int64_t delay_acc = 0;
    int delay_cnt = 0;

    for (;;) {
        if (0 != gettimeofday(&now_tv, NULL)) {
            Serial.printf("cannot get the current timestamp. errno:%d\n", errno);
            now_tv.tv_sec = time(NULL);
            now_tv.tv_usec = 0;
        }
        if (TimerCAM.Camera.get()) {
            //Serial.printf("pic size: %d\n", TimerCAM.Camera.fb->len);

            get_exif_header(TimerCAM.Camera.fb, &now_tv, &exif_buf, &exif_len);
            data_offset = get_jpeg_data_offset(TimerCAM.Camera.fb);

            uint32_t total_bytes = exif_len + TimerCAM.Camera.fb->len - data_offset;
            //Serial.printf("pic with exif size: %d\n", total_bytes);

            client->print(_STREAM_BOUNDARY);
            client->printf(_STREAM_PART, total_bytes);

            if (http_transfer(client, exif_len, exif_buf) < 0) {
                goto client_exit;
            }
            if (http_transfer(client, TimerCAM.Camera.fb->len - data_offset, TimerCAM.Camera.fb->buf + data_offset) < 0) {
                goto client_exit;
            }
            
            int64_t fr_end     = esp_timer_get_time();
            int64_t frame_time = fr_end - last_frame;
            last_frame         = fr_end;
            frame_time /= 1000;

            /*
            if (frame_time < expected_frame_time) {
              int d = int(expected_frame_time - frame_time);
              if (d > 0) {
                delay_acc += d;
                delay_cnt += 1;
                delay(d);
              }
            }
            */
            
            frame_time_acc += (long unsigned int)frame_time;
            //Serial.printf(
            //    "MJPG: %luKB %lums (%.1ffps)\r\n",
            //    (long unsigned int)(TimerCAM.Camera.fb->len / 1024),
            //    (long unsigned int)frame_time, 1000.0 / (long unsigned int)frame_time
            //);
            if (--interval == 0) {
                Serial.printf(
                  "FPS: %.2f, avg delay (%.2f, %d), BAT Vol: %dmv, BAT Lvl: %d%%\r\n",
                  1000.0 * INFO_INTERVAL / frame_time_acc,
                  (float)delay_acc / INFO_INTERVAL,
                  delay_cnt,
                  TimerCAM.Power.getBatteryVoltage(),
                  TimerCAM.Power.getBatteryLevel()
                );
                interval = INFO_INTERVAL;
                frame_time_acc = 0;
                delay_acc = 0;
                delay_cnt = 0;
            }

            TimerCAM.Camera.free();
        }
    }

client_exit:
    TimerCAM.Camera.free();
    client->stop();
    Serial.printf("Image stream end\r\n");
}

int http_transfer(WiFiClient *client, size_t num, const uint8_t *buf)
{

    int32_t to_sends = num;
    int32_t now_sends = 0;
    const uint8_t *out_buf = buf;
    uint32_t packet_len = 8 * 1024;
    while (to_sends > 0) {
        now_sends = to_sends > packet_len ? packet_len : to_sends;
        if (client->write(out_buf, now_sends) == 0) {
            return -1;
        }
        out_buf += now_sends;
        to_sends -= packet_len;
    }
    return 0;
}
