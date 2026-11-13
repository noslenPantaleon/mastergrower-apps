/**
 * @file debug_http.c
 * @brief Servidor HTTP de depuración para validar la cámara desde el navegador (fase C1).
 */
#include "debug_http.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal_camera.h"
#include "hal_storage.h"
#include "storage_queue.h"
#include "mg_wifi.h"
#include "system_config.h"

static const char *TAG = "debug_http";
static httpd_handle_t s_server;

static const char k_index_html[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>" SYS_NODE_ID " debug</title>"
    "<style>body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:16px}"
    "img{max-width:100%;border:1px solid #444}button,select,input{font-size:1rem;margin:4px}"
    "pre{background:#222;padding:8px;overflow:auto}</style></head><body>"
    "<h2>" SYS_NODE_ID " &mdash; cámara</h2>"
    "<div><select id='res'>"
    "<option>QVGA</option><option>VGA</option><option selected>SVGA</option><option>XGA</option>"
    "<option>HD</option><option>SXGA</option><option>UXGA</option><option>QXGA</option></select>"
    "Calidad <input id='q' type='number' min='4' max='63' value='12' style='width:4em'>"
    "<label><input id='vf' type='checkbox' checked>vflip</label>"
    "<label><input id='hm' type='checkbox'>hmirror</label>"
    "<button onclick='apply()'>Aplicar</button><button onclick='shot()'>Capturar</button></div>"
    "<p id='m'></p><img id='img'><pre id='st'></pre>"
    "<script>"
    "async function shot(){const t=performance.now();const r=await fetch('/capture.jpg?'+Date.now());"
    "const b=await r.blob();document.getElementById('img').src=URL.createObjectURL(b);"
    "document.getElementById('m').textContent=`${(b.size/1024).toFixed(1)} KB · captura `+"
    "r.headers.get('X-Capture-Ms')+` ms · total ${(performance.now()-t).toFixed(0)} ms`;stat()}"
    "async function apply(){const p=new URLSearchParams({res:res.value,quality:q.value,"
    "vflip:vf.checked?1:0,hmirror:hm.checked?1:0});await fetch('/control?'+p);shot()}"
    "async function stat(){const r=await fetch('/status');"
    "document.getElementById('st').textContent=JSON.stringify(await r.json(),null,2)}"
    "shot();</script></body></html>";

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, k_index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t capture_get(httpd_req_t *req)
{
    int64_t t0 = esp_timer_get_time();
    hal_frame_t f;
    esp_err_t err = hal_camera_capture(&f, pdMS_TO_TICKS(3000));
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }
    int64_t capture_ms = (esp_timer_get_time() - t0) / 1000;

    char hdr_ms[24], hdr_size[16];
    snprintf(hdr_ms, sizeof(hdr_ms), "%" PRId64, capture_ms);
    snprintf(hdr_size, sizeof(hdr_size), "%ux%u", f.width, f.height);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Expose-Headers", "X-Capture-Ms, X-Frame-Size");
    httpd_resp_set_hdr(req, "X-Capture-Ms", hdr_ms);
    httpd_resp_set_hdr(req, "X-Frame-Size", hdr_size);

    err = httpd_resp_send(req, (const char *)f.data, f.len);
    ESP_LOGI(TAG, "/capture.jpg %ux%u %u bytes, captura %" PRId64 " ms", f.width, f.height,
             (unsigned)f.len, capture_ms);
    hal_camera_release(&f);
    return err;
}

static esp_err_t status_get(httpd_req_t *req)
{
    hal_camera_info_t ci;
    hal_camera_get_info(&ci);
    char ip[16];
    mg_wifi_get_ip_str(ip, sizeof(ip));

    char json[640];
    int n = snprintf(json, sizeof(json),
        "{\"node_id\":\"%s\",\"firmware\":\"%s\",\"uptime_s\":%" PRId64 ","
        "\"camera\":{\"camera_type\":\"%s\",\"sensor\":\"%s\",\"resolucion\":\"%s\","
        "\"calidad_jpeg\":%u,\"capturas_ok\":%" PRIu32 ",\"capturas_error\":%" PRIu32 "},"
        "\"heap\":{\"internal_free\":%u,\"internal_min\":%u,\"internal_largest\":%u,"
        "\"psram_free\":%u,\"psram_min\":%u},"
        "\"red\":{\"ip\":\"%s\",\"rssi\":%d}}",
        SYS_NODE_ID, SYS_FIRMWARE_VERSION, esp_timer_get_time() / 1000000,
        ci.camera_type, ci.sensor_name, hal_camera_res_name(ci.resolution), ci.jpeg_quality,
        ci.captures_ok, ci.captures_failed,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
        ip, mg_wifi_get_rssi());
    if (n < 0 || n >= (int)sizeof(json)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}

static esp_err_t control_get(httpd_req_t *req)
{
    char query[128], val[16];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "faltan parámetros");
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    bool res_changed = false;

    if (httpd_query_key_value(query, "res", val, sizeof(val)) == ESP_OK) {
        hal_camera_res_t res;
        if (!hal_camera_res_from_name(val, &res)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "resolución inválida");
            return ESP_FAIL;
        }
        err = hal_camera_set_resolution(res);
        res_changed = true;
    }
    if (err == ESP_OK && httpd_query_key_value(query, "quality", val, sizeof(val)) == ESP_OK) {
        err = hal_camera_set_quality((uint8_t)atoi(val));
    }
    char vf[4] = "", hm[4] = "";
    bool has_vf = httpd_query_key_value(query, "vflip", vf, sizeof(vf)) == ESP_OK;
    bool has_hm = httpd_query_key_value(query, "hmirror", hm, sizeof(hm)) == ESP_OK;
    if (err == ESP_OK && (has_vf || has_hm)) {
        err = hal_camera_set_orientation(has_vf && atoi(vf), has_hm && atoi(hm));
    }
    if (err == ESP_OK && res_changed) {
        /* Los primeros cuadros tras cambiar de resolución pueden salir corruptos o mal expuestos. */
        hal_camera_discard_frames(2);
    }

    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "/control %s", query);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t storage_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    if (!storage_queue_ready()) {
        return httpd_resp_sendstr(req, "{\"montada\":false}");
    }
    uint64_t total = 0, libre = 0;
    hal_storage_get_usage(&total, &libre);

    sq_entry_t recent[10];
    size_t n = storage_queue_list_recent(recent, 10);

    char head[160];
    snprintf(head, sizeof(head),
             "{\"montada\":true,\"total_mb\":%llu,\"libre_mb\":%llu,\"libre_pct\":%d,\"pendientes\":%u,"
             "\"recientes\":[", total >> 20, libre >> 20, hal_storage_free_pct(), (unsigned)storage_queue_count());
    httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);
    for (size_t i = 0; i < n; i++) {
        char item[112];
        snprintf(item, sizeof(item), "%s{\"id\":\"%s\",\"bytes\":%" PRIu32 ",\"alta\":%s}", i ? "," : "",
                 recent[i].id, recent[i].jpeg_len, recent[i].alta ? "true" : "false");
        httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_send_chunk(req, "]}", 2);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t storage_img_get(httpd_req_t *req)
{
    char query[96], id[SQ_ID_MAX_LEN];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "id", id, sizeof(id)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta id");
        return ESP_FAIL;
    }
    uint8_t *jpeg = NULL;
    size_t len = 0;
    if (storage_queue_read(id, &jpeg, &len, NULL) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no existe");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    esp_err_t err = httpd_resp_send(req, (const char *)jpeg, len);
    free(jpeg);
    return err;
}

esp_err_t debug_http_start(void)
{
    if (s_server) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = CONFIG_MG_DEBUG_HTTP_PORT;
    cfg.stack_size = SYS_DEBUG_HTTP_STACK;
    cfg.task_priority = SYS_DEBUG_HTTP_PRIO;
    cfg.core_id = SYS_CORE_NET;
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        return err;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_get },
        { .uri = "/capture.jpg", .method = HTTP_GET, .handler = capture_get },
        { .uri = "/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/control", .method = HTTP_GET, .handler = control_get },
        { .uri = "/storage", .method = HTTP_GET, .handler = storage_get },
        { .uri = "/storage/img", .method = HTTP_GET, .handler = storage_img_get },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    char ip[16];
    mg_wifi_get_ip_str(ip, sizeof(ip));
    ESP_LOGW(TAG, "Servidor de DEPURACIÓN sin autenticación en http://%s:%d/", ip, cfg.server_port);
    return ESP_OK;
}

void debug_http_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
