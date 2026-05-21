#include "wifi_portal.h"
#include "espnow_rx.h"
#include "smart_features.h"
#include "flash_logger.h"
#include "ble_relay.h"
#include "ble_ota.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs.h"

static const char *TAG = "PORTAL";
static httpd_handle_t s_server = NULL;

static const char INDEX_HTML[] =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ADV350</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0}"
    "body{font:14px/1.5 system-ui;max-width:420px;margin:0 auto;padding:12px;background:#111;color:#ddd}"
    "h1{font-size:1.1em;color:#0f0;margin-bottom:12px}"
    "h2{font-size:.85em;color:#888;margin:12px 0 6px;text-transform:uppercase}"
    ".c{background:#1a1a2e;border-radius:8px;padding:10px;margin-bottom:8px}"
    ".r{display:flex;justify-content:space-between;padding:3px 0;border-bottom:1px solid #222}"
    ".v{color:#0f0;font-weight:700}"
    "label{display:block;font-size:.85em;color:#888;margin-top:8px}"
    "input,select{width:100%;padding:6px;border:1px solid #333;border-radius:4px;background:#0a0a1a;color:#fff}"
    ".b{display:block;width:100%;padding:8px;border:none;border-radius:4px;margin-top:8px;font-weight:700;cursor:pointer;color:#000}"
    ".bg{background:#0f0}.br{background:#c00;color:#fff}"
    "</style></head><body>"
    "<h1>ADV350 Setup</h1>"
    "<div class='c'><h2>Status</h2><div id='s'></div></div>"
    "<div class='c'><h2>Data Logger</h2><div id='l'></div>"
    "<button class='b br' onclick=\"if(confirm('Clear all logs?'))fetch('/api/log/clear',{method:'POST'}).then(()=>location.reload())\">Clear Logs</button></div>"
    "<div class='c'><h2>Config</h2>"
    "<form method='POST' action='/api/save'>"
    "<label>ESP-NOW Channel</label><select name='ch' id='fc'></select>"
    "<button type='submit' class='b bg'>Save &amp; Restart</button></form></div>"
    "<div class='c'><button class='b br' onclick=\"if(confirm('Restart?'))fetch('/api/restart',{method:'POST'})\">Restart Device</button></div>"
    "<script>"
    "let s=document.getElementById('fc');"
    "for(let i=1;i<=13;i++){let o=new Option('Ch '+i,i);s.add(o)}"
    "fetch('/api/status').then(r=>r.json()).then(d=>{"
    "let h='',ks=['uptime','heap','fw','rx_count','loss_pct','board_temp','trip_count','trip_active','peer','ota'];"
    "ks.forEach(k=>{if(k in d)h+='<div class=r><span>'+k+'</span><span class=v>'+d[k]+'</span></div>'});"
    "document.getElementById('s').innerHTML=h;"
    "document.getElementById('fc').value=d.espnow_ch||1;"
    "let li=d.log_records!=null?'<div class=r><span>Records</span><span class=v>'+d.log_records+'</span></div>"
    "<div class=r><span>Used</span><span class=v>'+d.log_used_kb+' KB</span></div>"
    "<div class=r><span>Free</span><span class=v>'+d.log_free_kb+' KB</span></div>':'N/A';"
    "document.getElementById('l').innerHTML=li"
    "});"
    "</script></body></html>";

// ── GET / ──

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

// ── GET /api/status ──

static esp_err_t status_handler(httpd_req_t *req)
{
    uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000);
    uint32_t heap = (uint32_t)esp_get_free_heap_size();
    float board_temp = smart_get_board_temp();
    float loss_pct = g_relay.rx_count > 0
        ? (float)g_relay.rx_lost / (g_relay.rx_count + g_relay.rx_lost) * 100.0f
        : 0;
    logger_stats_t ls = flash_logger_stats();

    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"uptime\":\"%luh%lum%lus\","
        "\"heap\":%lu,"
        "\"fw\":\"%s\","
        "\"rx_count\":%lu,"
        "\"loss_pct\":\"%.1f%%\","
        "\"board_temp\":\"%.1fC\","
        "\"trip_count\":%lu,"
        "\"trip_active\":%s,"
        "\"peer\":%s,"
        "\"ota\":\"%s\","
        "\"espnow_ch\":1,"
        "\"log_records\":%lu,"
        "\"log_used_kb\":%lu,"
        "\"log_free_kb\":%lu}",
        (unsigned long)(uptime / 3600),
        (unsigned long)((uptime % 3600) / 60),
        (unsigned long)(uptime % 60),
        (unsigned long)heap,
        FW_VERSION_S3,
        (unsigned long)g_relay.rx_count,
        loss_pct,
        board_temp,
        (unsigned long)g_trip.trip_count,
        g_trip.state == TRIP_ACTIVE ? "true" : "false",
        g_relay.peer_known ? "true" : "false",
        ble_ota_get_state() == OTA_IDLE ? "idle" : "active",
        (unsigned long)ls.record_count,
        (unsigned long)(ls.used_bytes / 1024),
        (unsigned long)(ls.free_bytes / 1024));

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

// ── POST /api/save ──

static esp_err_t save_handler(httpd_req_t *req)
{
    char body[128];
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    char val[16];
    nvs_handle_t nvs;
    if (nvs_open("portal", NVS_READWRITE, &nvs) == ESP_OK) {
        if (httpd_query_key_value(body, "ch", val, sizeof(val)) == ESP_OK) {
            uint8_t ch = (uint8_t)atoi(val);
            if (ch >= 1 && ch <= 13) {
                nvs_set_u8(nvs, "espnow_ch", ch);
            }
        }
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, "<h3>Saved. Restarting...</h3>", HTTPD_RESP_USE_STRLEN);

    const esp_timer_create_args_t args = {
        .callback = (esp_timer_cb_t)esp_restart,
        .name = "rst",
    };
    esp_timer_handle_t timer;
    esp_timer_create(&args, &timer);
    esp_timer_start_once(timer, 1000000);

    return ESP_OK;
}

// ── POST /api/log/clear ──

static esp_err_t log_clear_handler(httpd_req_t *req)
{
    flash_logger_clear();
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// ── POST /api/restart ──

static esp_err_t restart_handler(httpd_req_t *req)
{
    httpd_resp_send(req, "OK", 2);

    const esp_timer_create_args_t args = {
        .callback = (esp_timer_cb_t)esp_restart,
        .name = "rst",
    };
    esp_timer_handle_t timer;
    esp_timer_create(&args, &timer);
    esp_timer_start_once(timer, 500000);

    return ESP_OK;
}

// ── Init ──

esp_err_t wifi_portal_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.stack_size = 4096;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = index_handler },
        { .uri = "/api/status",    .method = HTTP_GET,  .handler = status_handler },
        { .uri = "/api/save",      .method = HTTP_POST, .handler = save_handler },
        { .uri = "/api/log/clear", .method = HTTP_POST, .handler = log_clear_handler },
        { .uri = "/api/restart",   .method = HTTP_POST, .handler = restart_handler },
    };

    for (int i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    ESP_LOGI(TAG, "Portal started at http://192.168.4.1");
    return ESP_OK;
}

void wifi_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
