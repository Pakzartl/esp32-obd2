#include "flash_logger.h"
#include "smart_features.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"

static const char *TAG = "FLOG";

#define MOUNT_POINT  "/spiffs"
#define LOG_FILE     MOUNT_POINT "/datalog.bin"
#define MAX_LOG_SIZE (350 * 1024)

static bool s_mounted = false;
static int64_t s_last_write_us = 0;
#define MIN_INTERVAL_US 1000000

esp_err_t flash_logger_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = "storage",
        .max_files = 2,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_mounted = true;
    logger_stats_t st = flash_logger_stats();
    ESP_LOGI(TAG, "Ready: %luKB used / %luKB total, %lu records",
             (unsigned long)(st.used_bytes / 1024),
             (unsigned long)(st.total_bytes / 1024),
             (unsigned long)st.record_count);
    return ESP_OK;
}

esp_err_t flash_logger_write(const relay_packet_t *pkt, uint16_t trip_id)
{
    if (!s_mounted) return ESP_ERR_INVALID_STATE;

    int64_t now = esp_timer_get_time();
    if ((now - s_last_write_us) < MIN_INTERVAL_US) return ESP_OK;
    s_last_write_us = now;

    struct stat st;
    if (stat(LOG_FILE, &st) == 0 && st.st_size >= MAX_LOG_SIZE) {
        remove(LOG_FILE);
        ESP_LOGW(TAG, "Log rotated (was %ld bytes)", st.st_size);
    }

    log_record_t rec = {
        .timestamp_s    = (uint32_t)(now / 1000000),
        .trip_id        = trip_id,
        .vd_flags       = pkt->vd_flags,
        .rpm            = pkt->vd_rpm,
        .speed          = pkt->vd_speed,
        .coolant_enc    = pkt->vd_coolant_enc,
        .throttle_enc   = pkt->vd_throttle_enc,
        .map            = pkt->vd_map,
        .iat_enc        = pkt->vd_iat_enc,
        .engine_load_enc = pkt->vd_engine_load_enc,
        .battery_cv     = pkt->vd_battery_cv,
        .fuel_rate_x100 = pkt->vd_fuel_rate_x100,
        .stft           = pkt->vd_stft,
        .score          = pkt->vd_score,
        .board_temp_enc = (uint8_t)(smart_get_board_temp() + 40),
    };

    FILE *f = fopen(LOG_FILE, "ab");
    if (!f) return ESP_FAIL;

    size_t written = fwrite(&rec, sizeof(rec), 1, f);
    fclose(f);

    return written == 1 ? ESP_OK : ESP_FAIL;
}

esp_err_t flash_logger_clear(void)
{
    if (!s_mounted) return ESP_ERR_INVALID_STATE;
    remove(LOG_FILE);
    ESP_LOGI(TAG, "Log cleared");
    return ESP_OK;
}

logger_stats_t flash_logger_stats(void)
{
    logger_stats_t stats = {0};
    if (!s_mounted) return stats;

    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    stats.total_bytes = total;
    stats.used_bytes = used;
    stats.free_bytes = total > used ? total - used : 0;

    struct stat st;
    if (stat(LOG_FILE, &st) == 0) {
        stats.record_count = st.st_size / sizeof(log_record_t);
    }

    return stats;
}
