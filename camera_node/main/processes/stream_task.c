/**
 * @file stream_task.c
 * @brief Ver stream_task.h.
 */
#include "stream_task.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "capture_task.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "hal_camera.h"
#include "mg_api.h"
#include "net_task.h"
#include "system_config.h"
#include "trigger_manager.h"

static const char *TAG = "stream";

#define SESSION_ID_MAX      24
#define URL_MAX             192
#define CONNECT_TIMEOUT_MS  5000
#define SEND_TIMEOUT_MS     2000
#define MAX_SEND_ERRORS     3
#define CAMERA_TIMEOUT_MS   1500
#define WS_BUFFER_SIZE      4096

#define EV_CONNECTED  BIT0
#define EV_CLOSED     BIT1

typedef struct {
    char session_id[SESSION_ID_MAX + 1];
    char url[URL_MAX];
    char cmd_id[MG_CMD_ID_MAX_LEN + 1];
    hal_camera_res_t res;
    uint8_t fps_max;
    uint16_t ttl_s;
} stream_req_t;

static struct {
    TaskHandle_t task;
    EventGroupHandle_t ev;
    portMUX_TYPE mux;
    stream_req_t pending;
    bool has_pending;
    volatile bool stop;                       /**< stop_stream o sesión nueva: cortar la actual */
    char current[SESSION_ID_MAX + 1];         /**< sesión transmitiendo ("" = ninguna) */
} s_st = { .mux = portMUX_INITIALIZER_UNLOCKED };

bool stream_task_active(void)
{
    return s_st.current[0] != '\0';
}

/* ---------------------------------------------------------------- comandos (net_task) */

static const char *param_str(const cJSON *p, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(p, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static int param_int(const cJSON *p, const char *key, int def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(p, key);
    return cJSON_IsNumber(v) ? (int)v->valuedouble : def;
}

static esp_err_t cmd_start_stream(const mg_cmd_t *cmd, char *reason, size_t reason_len)
{
    const char *sid = param_str(cmd->params, "session_id");
    const char *url = param_str(cmd->params, "ws_publish_url");
    const char *res_name = param_str(cmd->params, "resolution");
    stream_req_t req = { 0 };
    if (!sid || !*sid || strlen(sid) > SESSION_ID_MAX || !url || !res_name ||
        !hal_camera_res_from_name(res_name, &req.res) ||
        (req.res != HAL_CAM_RES_QVGA && req.res != HAL_CAM_RES_VGA) ||
        mg_api_ws_url(url, req.url, sizeof(req.url)) != ESP_OK) {
        snprintf(reason, reason_len, "invalid_format");
        return ESP_ERR_INVALID_ARG;
    }

    /* La API ya recorta a la config, pero el nodo no confía: aplica sus propios límites. */
    node_config_t c;
    config_store_get(&c);
    hal_camera_res_t res_max = HAL_CAM_RES_VGA;
    hal_camera_res_from_name(c.stream_resolution_max, &res_max);
    if (req.res > res_max) {
        req.res = res_max;
    }
    int fps = param_int(cmd->params, "fps_max", c.stream_fps_max);
    int ttl = param_int(cmd->params, "ttl_s", c.stream_ttl_max_s);
    req.fps_max = (uint8_t)(fps < 1 ? 1 : fps > c.stream_fps_max ? c.stream_fps_max : fps);
    req.ttl_s = (uint16_t)(ttl < 1 ? 1 : ttl > c.stream_ttl_max_s ? c.stream_ttl_max_s : ttl);
    strlcpy(req.session_id, sid, sizeof(req.session_id));
    strlcpy(req.cmd_id, cmd->cmd_id, sizeof(req.cmd_id));

    if (strcmp(s_st.current, sid) == 0) {
        snprintf(reason, reason_len, "already_streaming");
        return ESP_OK;
    }
    taskENTER_CRITICAL(&s_st.mux);
    s_st.pending = req;
    s_st.has_pending = true;
    s_st.stop = true;  // si hay otra sesión transmitiendo, se corta y arranca esta
    taskEXIT_CRITICAL(&s_st.mux);
    xTaskNotifyGive(s_st.task);
    return ESP_ERR_NOT_FINISHED;  // "done" cuando sale el primer cuadro
}

static esp_err_t cmd_stop_stream(const mg_cmd_t *cmd, char *reason, size_t reason_len)
{
    const char *sid = param_str(cmd->params, "session_id");
    bool match = false;
    taskENTER_CRITICAL(&s_st.mux);
    if (s_st.has_pending && (!sid || strcmp(s_st.pending.session_id, sid) == 0)) {
        s_st.has_pending = false;  // todavía no había arrancado
        match = true;
    }
    if (s_st.current[0] && (!sid || strcmp(s_st.current, sid) == 0)) {
        s_st.stop = true;
        match = true;
    }
    taskEXIT_CRITICAL(&s_st.mux);
    if (!match) {
        snprintf(reason, reason_len, "not_streaming");
    }
    return ESP_OK;
}

/* ---------------------------------------------------------------- WebSocket */

static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        xEventGroupSetBits(s_st.ev, EV_CONNECTED);
        break;
    case WEBSOCKET_EVENT_DATA: {
        const esp_websocket_event_data_t *d = data;
        if (d->op_code == 0x08) {  // la API cierra la sesión (DELETE, ttl, sin espectadores)
            unsigned code = d->data_len >= 2 ? ((uint8_t)d->data_ptr[0] << 8) | (uint8_t)d->data_ptr[1] : 0;
            ESP_LOGI(TAG, "La API cerró la sesión (%u %.*s)", code, d->data_len > 2 ? d->data_len - 2 : 0,
                     d->data_len > 2 ? d->data_ptr + 2 : "");
            xEventGroupSetBits(s_st.ev, EV_CLOSED);
        }
        break;
    }
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
        xEventGroupSetBits(s_st.ev, EV_CLOSED);
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- sesión */

/** Toma un cuadro en la resolución del stream y lo copia a `buf` (crece si hace falta). */
static esp_err_t grab(hal_camera_res_t res, uint8_t **buf, size_t *cap, size_t *len)
{
    if (!capture_task_camera_lock(pdMS_TO_TICKS(CAMERA_TIMEOUT_MS * 4))) {
        return ESP_ERR_TIMEOUT;  // una captura completa en curso tarda; se reintenta en el próximo
    }
    hal_camera_info_t info;
    hal_camera_get_info(&info);
    esp_err_t err = ESP_OK;
    if (info.resolution != res) {
        err = hal_camera_set_resolution(res);
        if (err == ESP_OK) {
            /* Con 2 framebuffers, el primero (o los dos) pueden venir en la resolución anterior:
             * con 1 solo descarte salía un cuadro de 800×600 al empezar el stream */
            hal_camera_discard_frames(2);
        }
    }
    hal_frame_t f;
    if (err == ESP_OK) {
        err = hal_camera_capture(&f, pdMS_TO_TICKS(CAMERA_TIMEOUT_MS));
    }
    if (err == ESP_OK) {
        if (f.len > *cap) {
            uint8_t *nb = heap_caps_realloc(*buf, f.len + 4096, MALLOC_CAP_SPIRAM);
            if (!nb) {
                err = ESP_ERR_NO_MEM;
            } else {
                *buf = nb;
                *cap = f.len + 4096;
            }
        }
        if (err == ESP_OK) {
            memcpy(*buf, f.data, f.len);
            *len = f.len;
        }
        hal_camera_release(&f);  // el framebuffer vuelve antes de mandar por la red
    }
    capture_task_camera_unlock();
    return err;
}

static void run_session(const stream_req_t *req)
{
    char auth[160];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s\r\n", mg_api_token());
    esp_websocket_client_config_t cfg = {
        .uri = req->url,
        .headers = auth,
        .buffer_size = WS_BUFFER_SIZE,
        .network_timeout_ms = CONNECT_TIMEOUT_MS,
        .disable_auto_reconnect = true,
        .task_prio = SYS_NET_TASK_PRIO - 1,
        .task_stack = 4096,
    };
    int64_t t0 = esp_timer_get_time();
    ESP_LOGI(TAG, "Sesión %s: %s %u fps máx %u s → %s", req->session_id, hal_camera_res_name(req->res),
             req->fps_max, req->ttl_s, req->url);

    xEventGroupClearBits(s_st.ev, EV_CONNECTED | EV_CLOSED);
    esp_websocket_client_handle_t ws = esp_websocket_client_init(&cfg);
    if (!ws) {
        net_task_send_ack(req->cmd_id, MG_ACK_REJECTED, "out_of_memory");
        return;
    }
    esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, ws_event, NULL);
    EventBits_t bits = 0;
    if (esp_websocket_client_start(ws) == ESP_OK) {
        bits = xEventGroupWaitBits(s_st.ev, EV_CONNECTED | EV_CLOSED, pdFALSE, pdFALSE,
                                   pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    }
    if (!(bits & EV_CONNECTED) || (bits & EV_CLOSED)) {
        ESP_LOGW(TAG, "Sesión %s: no se pudo abrir el WebSocket", req->session_id);
        net_task_send_ack(req->cmd_id, MG_ACK_REJECTED, "stream_connect_failed");
        esp_websocket_client_destroy(ws);
        return;
    }

    strlcpy(s_st.current, req->session_id, sizeof(s_st.current));
    trigger_manager_set_streaming(true);
    net_task_request_status();

    const TickType_t period = pdMS_TO_TICKS(1000 / req->fps_max) ? pdMS_TO_TICKS(1000 / req->fps_max) : 1;
    const int64_t deadline = t0 + (int64_t)req->ttl_s * 1000000;
    uint8_t *buf = NULL;
    size_t cap = 0, len = 0;
    uint32_t frames = 0, send_errors = 0, cam_errors = 0;
    uint64_t bytes = 0;
    int64_t first_us = 0;
    const char *end = "ttl";
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        if (s_st.stop) {
            end = "stop";
            break;
        }
        if (xEventGroupGetBits(s_st.ev) & EV_CLOSED || !esp_websocket_client_is_connected(ws)) {
            end = "api_closed";
            break;
        }
        if (esp_timer_get_time() >= deadline) {
            break;
        }
        esp_err_t err = grab(req->res, &buf, &cap, &len);
        if (err != ESP_OK) {
            if (++cam_errors >= 5) {
                end = "camera_error";
                break;
            }
            ESP_LOGW(TAG, "Sin cuadro: %s", esp_err_to_name(err));
        } else if (esp_websocket_client_send_bin(ws, (const char *)buf, (int)len,
                                                 pdMS_TO_TICKS(SEND_TIMEOUT_MS)) < 0) {
            if (++send_errors >= MAX_SEND_ERRORS) {
                end = "send_error";
                break;
            }
        } else {
            cam_errors = send_errors = 0;
            bytes += len;
            if (frames++ == 0) {
                first_us = esp_timer_get_time();
                ESP_LOGI(TAG, "Primer cuadro en %" PRId64 " ms (%u bytes)", (first_us - t0) / 1000, (unsigned)len);
                net_task_send_ack(req->cmd_id, MG_ACK_DONE, NULL);
            }
        }
        vTaskDelayUntil(&last, period);
    }

    if (frames == 0) {  // nunca llegó a transmitir: el pedido falló
        net_task_send_ack(req->cmd_id, MG_ACK_REJECTED,
                          strcmp(end, "camera_error") == 0 ? "camera_error" : "stream_connect_failed");
    }
    if (esp_websocket_client_is_connected(ws)) {
        esp_websocket_client_close(ws, pdMS_TO_TICKS(2000));
    }
    esp_websocket_client_destroy(ws);
    heap_caps_free(buf);

    double secs = first_us ? (esp_timer_get_time() - first_us) / 1e6 : 0;
    ESP_LOGI(TAG, "Sesión %s terminada (%s): %" PRIu32 " cuadros, %.1f fps, %.0f KB", req->session_id, end,
             frames, secs > 0 ? (frames - 1) / secs : 0.0, bytes / 1024.0);
    s_st.current[0] = '\0';
    trigger_manager_set_streaming(false);
    net_task_request_status();
}

static void stream_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {  // puede haber llegado otro start_stream mientras transmitía
            stream_req_t req;
            bool have;
            taskENTER_CRITICAL(&s_st.mux);
            have = s_st.has_pending;
            if (have) {
                req = s_st.pending;
                s_st.has_pending = false;
                s_st.stop = false;
            }
            taskEXIT_CRITICAL(&s_st.mux);
            if (!have) {
                break;
            }
            run_session(&req);
        }
    }
}

esp_err_t stream_task_start(void)
{
    if (s_st.task) {
        return ESP_ERR_INVALID_STATE;
    }
    s_st.ev = xEventGroupCreate();
    if (!s_st.ev) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(stream_task, "stream_task", SYS_STREAM_STACK, NULL,
                                            SYS_STREAM_PRIO, &s_st.task, SYS_CORE_APP);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = net_task_register_cmd(MG_ACTION_START_STREAM, cmd_start_stream);
    if (err == ESP_OK) {
        err = net_task_register_cmd(MG_ACTION_STOP_STREAM, cmd_stop_stream);
    }
    return err;
}
