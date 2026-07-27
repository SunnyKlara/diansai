// See jpeg_view.h for why the JPEG is embedded rather than received over the network.
#include "jpeg_view.h"

#include <string.h>
#include "driver/jpeg_decode.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "jpeg_view";

// Embedded by main/CMakeLists.txt (EMBED_FILES k230_frame1.jpg).
extern const uint8_t k230_jpg_start[] asm("_binary_k230_frame1_jpg_start");
extern const uint8_t k230_jpg_end[]   asm("_binary_k230_frame1_jpg_end");

static lv_image_dsc_t s_dsc;
static char s_status[96] = "not run";

const lv_image_dsc_t *jpeg_view_dsc(void)
{
    return s_dsc.data ? &s_dsc : NULL;
}

const char *jpeg_view_status(void)
{
    return s_status;
}

bool jpeg_view_init(void)
{
    const uint32_t jpg_size = (uint32_t)(k230_jpg_end - k230_jpg_start);
    // NOTE: index off a runtime pointer, not off the `k230_jpg_end[]` symbol.
    // `k230_jpg_end[-2]` looks natural but GCC sees a declared array and rejects it
    // under -Werror=array-bounds ("array subscript -2 is below array bounds").
    const uint8_t *jpg = k230_jpg_start;
    ESP_LOGI(TAG, "embedded frame: %lu bytes, first2=%02X%02X last2=%02X%02X",
             (unsigned long)jpg_size, jpg[0], jpg[1],
             jpg[jpg_size - 2], jpg[jpg_size - 1]);

    jpeg_decode_picture_info_t info = { 0 };
    esp_err_t err = jpeg_decoder_get_info(k230_jpg_start, jpg_size, &info);
    if (err != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "get_info failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", s_status);
        return false;
    }
    ESP_LOGI(TAG, "header says %lux%lu sampling=%d",
             (unsigned long)info.width, (unsigned long)info.height, (int)info.sample_method);

    jpeg_decoder_handle_t dec = NULL;
    // 40 ms budget: comfortably above a 30 fps frame time, so a timeout here really
    // means "the decoder is stuck", not "we were impatient".
    jpeg_decode_engine_cfg_t eng = { .intr_priority = 0, .timeout_ms = 40 };
    err = jpeg_new_decoder_engine(&eng, &dec);
    if (err != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "engine failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", s_status);
        return false;
    }

    // The engine wants DMA-capable, correctly aligned buffers on both sides; the
    // helper is the only safe way to get them (a plain malloc'd or const-flash
    // pointer is not usable as the input bitstream).
    jpeg_decode_memory_alloc_cfg_t in_cfg  = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
    jpeg_decode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };

    size_t in_alloc = 0, out_alloc = 0;
    uint8_t *in_buf = jpeg_alloc_decoder_mem(jpg_size, &in_cfg, &in_alloc);
    uint8_t *out_buf = jpeg_alloc_decoder_mem((size_t)info.width * info.height * 2,
                                              &out_cfg, &out_alloc);
    if (!in_buf || !out_buf) {
        snprintf(s_status, sizeof(s_status), "alloc failed (in=%u out=%u)",
                 (unsigned)in_alloc, (unsigned)out_alloc);
        ESP_LOGE(TAG, "%s", s_status);
        jpeg_del_decoder_engine(dec);
        return false;
    }
    memcpy(in_buf, k230_jpg_start, jpg_size);

    jpeg_decode_cfg_t cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        // If red and blue come out swapped on the panel, flip this to _RGB.
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };

    uint32_t out_len = 0;
    int64_t t0 = esp_timer_get_time();
    err = jpeg_decoder_process(dec, &cfg, in_buf, jpg_size, out_buf,
                               (uint32_t)out_alloc, &out_len);
    int64_t us = esp_timer_get_time() - t0;
    jpeg_del_decoder_engine(dec);

    if (err != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "decode failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", s_status);
        return false;
    }

    // Prove the output is an actual image and not a buffer of zeros: average the
    // green channel and count non-zero pixels. "decode returned ESP_OK" alone has
    // fooled us before -- a number you can sanity-check is worth the 20 lines.
    const uint16_t *px = (const uint16_t *)out_buf;
    const uint32_t n = info.width * info.height;
    uint64_t sum = 0;
    uint32_t nonzero = 0;
    uint16_t vmin = 0xFFFF, vmax = 0;
    for (uint32_t i = 0; i < n; i += 7) {          // sample every 7th pixel, plenty
        uint16_t v = px[i];
        sum += (v >> 5) & 0x3F;                    // green field of RGB565
        if (v) { nonzero++; }
        if (v < vmin) { vmin = v; }
        if (v > vmax) { vmax = v; }
    }
    const uint32_t sampled = (n + 6) / 7;
    ESP_LOGI(TAG, "decoded %lu B in %lld us | green avg %llu/63 | nonzero %lu/%lu | raw %u..%u",
             (unsigned long)out_len, us, (unsigned long long)(sum / (sampled ? sampled : 1)),
             (unsigned long)nonzero, (unsigned long)sampled, vmin, vmax);

    s_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w      = info.width;
    s_dsc.header.h      = info.height;
    s_dsc.header.stride = info.width * 2;
    s_dsc.data_size     = out_len;
    s_dsc.data          = out_buf;

    snprintf(s_status, sizeof(s_status), "%lux%lu %luB->%luB %lld.%02lldms",
             (unsigned long)info.width, (unsigned long)info.height,
             (unsigned long)jpg_size, (unsigned long)out_len,
             us / 1000, (us % 1000) / 10);
    ESP_LOGI(TAG, "JPEG_VIEW RESULT: PASS %s", s_status);
    return true;
}
