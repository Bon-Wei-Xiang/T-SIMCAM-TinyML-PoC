// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "driver/ledc.h"
#include "sdkconfig.h"
#include "camera_index.h"
#include <BonWeiXiang-project-1_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define TAG ""
#else
#include "esp_log.h"
static const char *TAG = "camera_httpd";
#endif

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

/* Constant defines -------------------------------------------------------- */
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS           320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS           240
#define EI_CAMERA_FRAME_BYTE_SIZE                 3

/* Private variables ------------------------------------------------------- */
static bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
static bool is_initialised = true;
static uint8_t *snapshot_buf = NULL; // points to the output of the capture
typedef struct
{
    size_t size;  //number of values used for filtering
    size_t index; //current value index
    size_t count; //value count
    int sum;
    int *values; //array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;
static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
    memset(filter, 0, sizeof(ra_filter_t));

    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values)
    {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}


void draw_rectangle(uint8_t *img, int width, int height, int x, int y, int w, int h, uint32_t color) {
    for (int i = x; i < x + w; i++) {
        if (i < 0 || i >= width) continue;
        if (y >= 0 && y < height) {
            int idx = (y * width + i) * 3;
            img[idx + 0] = (color >> 16) & 0xFF;
            img[idx + 1] = (color >> 8) & 0xFF;
            img[idx + 2] = color & 0xFF;
        }
        if ((y + h) >= 0 && (y + h) < height) {
            int idx = ((y + h) * width + i) * 3;
            img[idx + 0] = (color >> 16) & 0xFF;
            img[idx + 1] = (color >> 8) & 0xFF;
            img[idx + 2] = color & 0xFF;
        }
    }
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= height) continue;
        if (x >= 0 && x < width) {
            int idx = (j * width + x) * 3;
            img[idx + 0] = (color >> 16) & 0xFF;
            img[idx + 1] = (color >> 8) & 0xFF;
            img[idx + 2] = color & 0xFF;
        }
        if ((x + w) >= 0 && (x + w) < width) {
            int idx = (j * width + (x + w)) * 3;
            img[idx + 0] = (color >> 16) & 0xFF;
            img[idx + 1] = (color >> 8) & 0xFF;
            img[idx + 2] = color & 0xFF;
        }
    }
}

/**
 * @brief      Capture, rescale and crop image
 *
 * @param[in]  img_width     width of output image
 * @param[in]  img_height    height of output image
 * @param[in]  out_buf       pointer to store output image, NULL may be used
 *                           if ei_camera_frame_buffer is to be used for capture and resize/cropping.
 *
 * @retval     false if not initialised, image captured, rescaled or cropped failed
 *
 */
bool ei_camera_capture(camera_fb_t *fb,uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    bool do_resize = false;

    if (!is_initialised) {
        ei_printf("ERR: Camera is not initialized\r\n");
        return false;
    }

    if (!fb) {
        ei_printf("Camera capture failed\n");
        return false;
    }

   bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);

   if(!converted){
       ei_printf("Conversion failed\n");
       return false;
   }

    if ((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS)
        || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)) {
        do_resize = true;
    }

    if (do_resize) {
        ei::image::processing::crop_and_interpolate_rgb888(
        out_buf,
        EI_CAMERA_RAW_FRAME_BUFFER_COLS,
        EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
        out_buf,
        img_width,
        img_height);
    }


    return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    // we already have a RGB888 buffer, so recalculate offset into pixel index
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    while (pixels_left != 0) {
        // Swap BGR to RGB here
        // due to https://github.com/espressif/esp32-camera/issues/379
        out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix + 2] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix];

        // go to the next pixel
        out_ptr_ix++;
        pixel_ix+=3;
        pixels_left--;
    }
    // and done!
    return 0;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t jpg_buf_len = 0;
    uint8_t *jpg_buf = NULL;
    char part_buf[128];
    static int64_t last_frame = 0;
    if (!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);  // multipart/x-mixed-replace
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");

    // Allocate snapshot buffer (optionally in PSRAM if available)
    snapshot_buf = (uint8_t*)malloc(EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);
    // snapshot_buf = (uint8_t*)heap_caps_malloc(..., MALLOC_CAP_SPIRAM); // use this if you have PSRAM

    if (snapshot_buf == nullptr) {
        Serial.println("ERR: Failed to allocate snapshot buffer!\n");
        return ESP_FAIL;
    }

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            continue;
        }

        // Edge Impulse processing
        ei::signal_t signal;
        signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
        signal.get_data = &ei_camera_get_data;

        if (ei_camera_capture(fb, (size_t)EI_CLASSIFIER_INPUT_WIDTH, (size_t)EI_CLASSIFIER_INPUT_HEIGHT, snapshot_buf)) {
            ei_impulse_result_t result = { 0 };
            EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);

            if (err != EI_IMPULSE_OK) {
                Serial.print("ERR: Failed to run classifier (");
                Serial.print(err);
                Serial.println(")");
            }

            // print the predictions usega
            Serial.print("Predictions (DSP: ");
            Serial.print(result.timing.dsp);
            Serial.print(" ms., Classification: ");
            Serial.print(result.timing.classification);
            Serial.print(" ms., Anomaly: ");
            Serial.print(result.timing.anomaly);
            Serial.println(" ms.): ");
                
            bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
            if (converted) {
                for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
                    ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
                    if (bb.value == 0) continue;

                    int x = (int)((bb.x / (float)EI_CLASSIFIER_INPUT_WIDTH) * fb->width);
                    int y = (int)((bb.y / (float)EI_CLASSIFIER_INPUT_HEIGHT) * fb->height);
                    int w = (int)((bb.width / (float)EI_CLASSIFIER_INPUT_WIDTH) * fb->width);
                    int h = (int)((bb.height / (float)EI_CLASSIFIER_INPUT_HEIGHT) * fb->height);
                    
                    Serial.printf("%s (%.6f) [x:%d, y:%d, w:%d, h:%d]\n", bb.label, bb.value, x, y, w, h);

                    draw_rectangle(snapshot_buf, EI_CAMERA_RAW_FRAME_BUFFER_COLS, EI_CAMERA_RAW_FRAME_BUFFER_ROWS, x, y, w, h, 0xFF0000);
                }

                bool jpeg_converted = fmt2jpg(snapshot_buf, fb->width * fb->height * 3, fb->width, fb->height, PIXFORMAT_RGB888, 80, &jpg_buf, &jpg_buf_len);
                if (!jpeg_converted) {
                    Serial.println("Replce draw frame to original Conversion failed");
                }
            } else {
                Serial.println("Conversion failed\n");
            }
        } else {
            Serial.println("Failed to capture image\r\n");
        }

        if (fb->format != PIXFORMAT_JPEG && jpg_buf == NULL) {
            bool jpeg_converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
            if (!jpeg_converted) {
                ESP_LOGE(TAG, "JPEG compression failed");
                res = ESP_FAIL;
            }
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }

        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, jpg_buf_len, 0, 0);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }

        if (res == ESP_OK && jpg_buf != NULL) {
            res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_buf_len);
        }

        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Stream send failed");
            break;
        }

        int64_t now = esp_timer_get_time();
        int64_t frame_time = (now - last_frame) / 1000;
        last_frame = now;
        ESP_LOGI(TAG, "MJPG: %uB %ums (%.1ffps)",
                 (uint32_t)jpg_buf_len,
                 (uint32_t)frame_time,
                 1000.0 / (float)frame_time);

        esp_camera_fb_return(fb);

        if (jpg_buf) {
            free(jpg_buf);
            jpg_buf = NULL;
        }

        // Optional debug
        // Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    }

    last_frame = 0;
    free(snapshot_buf);
    snapshot_buf = NULL;
    return res;
}


static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        if (s->id.PID == OV3660_PID) {
            return httpd_resp_send(req, (const char *)home_html_gz, home_html_gz_len);
        } else if (s->id.PID == OV5640_PID) {
            return httpd_resp_send(req, (const char *)home_html_gz, home_html_gz_len);
        } else {
            return httpd_resp_send(req, (const char *)home_html_gz, home_html_gz_len);
        }
    } else {
        ESP_LOGE(TAG, "Camera sensor not found");
        return httpd_resp_send_500(req);
    }
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    ra_filter_init(&ra_filter, 20);

    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
    }
    config.server_port += 1;
    config.ctrl_port += 1;
    ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}