// main/gd_store.c —— 存档的 NVS 读写,说明见 gd_store.h。
#include "gd_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "gd_store";
#define NS  "gdash"
#define KEY "save"

static bool s_ready;

bool gd_store_init(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要重建(%s),旧存档将丢失", esp_err_to_name(e));
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    s_ready = e == ESP_OK;
    if (!s_ready) ESP_LOGE(TAG, "NVS 初始化失败:%s,本次不保存进度", esp_err_to_name(e));
    return s_ready;
}

bool gd_store_load(gd_save_t *out) {
    gd_save_defaults(out);
    if (!s_ready) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t buf[GD_SAVE_BYTES];
    size_t len = sizeof buf;
    const esp_err_t e = nvs_get_blob(h, KEY, buf, &len);
    nvs_close(h);
    if (e != ESP_OK) return false;
    const bool ok = gd_save_decode(buf, len, out);
    if (!ok) ESP_LOGW(TAG, "存档格式不符(%u 字节),使用默认值", (unsigned)len);
    return ok;
}

bool gd_store_save(const gd_save_t *s) {
    if (!s_ready) return false;
    uint8_t buf[GD_SAVE_BYTES];
    const size_t len = gd_save_encode(s, buf);
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, KEY, buf, len);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK) ESP_LOGW(TAG, "保存失败:%s", esp_err_to_name(e));
    return e == ESP_OK;
}
