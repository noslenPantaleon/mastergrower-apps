/**
 * @file live_view.c
 * @brief Ver live_view.h.
 */
#include "live_view.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "jpeg_decoder.h"
#include "mg_api.h"
#include "system_config.h"

static const char *TAG = "live";

#define JPEG_MAX        (256 * 1024)          /**< mismo límite que la API */
#define FRAME_MAX_PX    (640 * 480)           /**< VGA */
#define N_FRAMES        3
#define FIRST_FRAME_TIMEOUT_US  (12LL * 1000000)
#define STALL_TIMEOUT_US        (6LL * 1000000)
#define TTL_S           300
#define WS_BUFFER       4096   /**< RAM interna: los cuadros llegan en varios pedazos */
#define URL_MAX         256

typedef enum { CMD_START, CMD_STOP } cmd_kind_t;
typedef struct {
    cmd_kind_t kind;
    char node_id[16];
    char resolution[6];
    int fps_max;
} cmd_t;

typedef enum { FB_FREE, FB_DECODING, FB_READY, FB_SHOWN } fb_state_t;

static struct {
    TaskHandle_t task;
    QueueHandle_t cmds;
    SemaphoreHandle_t lock;
    live_status_t st;

    /* Sesión */
    char session_id[24];
    esp_websocket_client_handle_t ws;
    int64_t t_request_us, t_last_frame_us;
    volatile bool closed;              /**< el WebSocket se cerró (lo marca el evento) */
    char close_reason[64];

    /* JPEG, tres búferes que se intercambian y nunca quedan nulos:
     *  rx_buf: lo llena el evento del WebSocket · latest_buf: último completo · work_buf: se
     *  decodifica (solo lo toca live_task) */
    uint8_t *rx_buf, *latest_buf, *work_buf;
    size_t rx_len, latest_len;
    bool latest_new;

    /* Cuadros decodificados */
    uint16_t *fb[N_FRAMES];
    fb_state_t fb_state[N_FRAMES];
    uint16_t fb_w[N_FRAMES], fb_h[N_FRAMES];
    uint32_t fb_seq[N_FRAMES];
    uint32_t seq;

    /* fps de recepción */
    int64_t fps_t0;
    uint32_t fps_n;
} s_lv;

static void set_state(live_state_t st, const char *msg)
{
    xSemaphoreTake(s_lv.lock, portMAX_DELAY);
    s_lv.st.state = st;
    strlcpy(s_lv.st.msg, msg ? msg : "", sizeof(s_lv.st.msg));
    xSemaphoreGive(s_lv.lock);
}

/* ---------------------------------------------------------------- WebSocket (su tarea) */

static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const esp_websocket_event_data_t *d = data;
    switch (id) {
    case WEBSOCKET_EVENT_DATA:
        if (d->op_code == 0x08) {  // cierre: código + motivo
            int rl = d->data_len > 2 ? d->data_len - 2 : 0;
            xSemaphoreTake(s_lv.lock, portMAX_DELAY);
            snprintf(s_lv.close_reason, sizeof(s_lv.close_reason), "%.*s", rl, d->data_ptr + 2);
            xSemaphoreGive(s_lv.lock);
            s_lv.closed = true;
            xTaskNotifyGive(s_lv.task);
        } else if (d->op_code == 0x02 || (d->op_code == 0x00 && s_lv.rx_len)) {
            /* Un cuadro puede llegar en varios eventos (payload_offset / payload_len) */
            if (d->payload_offset == 0) {
                s_lv.rx_len = 0;
            }
            if (d->payload_len > JPEG_MAX || s_lv.rx_len + d->data_len > JPEG_MAX) {
                s_lv.rx_len = 0;
                break;
            }
            memcpy(s_lv.rx_buf + s_lv.rx_len, d->data_ptr, d->data_len);
            s_lv.rx_len += d->data_len;
            if (d->payload_offset + d->data_len >= d->payload_len) {
                xSemaphoreTake(s_lv.lock, portMAX_DELAY);
                if (s_lv.latest_new) {
                    s_lv.st.dropped++;  // el anterior no llegó a decodificarse
                }
                uint8_t *t = s_lv.latest_buf;
                s_lv.latest_buf = s_lv.rx_buf;
                s_lv.latest_len = s_lv.rx_len;
                s_lv.latest_new = true;
                s_lv.rx_buf = t;  // nunca nulo: work_buf es otro búfer
                s_lv.rx_len = 0;
                s_lv.st.frames++;
                xSemaphoreGive(s_lv.lock);
                xTaskNotifyGive(s_lv.task);
            }
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
        s_lv.closed = true;
        xTaskNotifyGive(s_lv.task);
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------- textos */

static void reason_text(const char *r, char *out, size_t len)
{
    if (strcmp(r, "ttl") == 0) {
        snprintf(out, len, "Se terminó el tiempo de la sesión");
    } else if (strcmp(r, "publisher_closed") == 0) {
        snprintf(out, len, "La cámara cortó la transmisión");
    } else if (strcmp(r, "node_timeout") == 0) {
        snprintf(out, len, "La cámara no respondió");
    } else if (strncmp(r, "rejected:", 9) == 0) {
        snprintf(out, len, "La cámara rechazó la vista en vivo (%.24s)", r + 9);
    } else if (strncmp(r, "stopped_by:", 11) == 0) {
        snprintf(out, len, "Sesión cerrada por %.32s", r + 11);
    } else if (strcmp(r, "no_viewers") == 0) {
        snprintf(out, len, "Sesión cerrada por falta de espectadores");
    } else if (r[0]) {
        snprintf(out, len, "Sesión terminada (%.32s)", r);
    } else {
        snprintf(out, len, "Se perdió la conexión con la API");
    }
}

/* ---------------------------------------------------------------- sesión */

/**
 * Cierra la sesión sin bloquear: primero DELETE en la API (que cierra el WebSocket del lado del
 * servidor y la tarea del cliente termina sola) y después destroy. No se usa
 * esp_websocket_client_close(): manda el cierre sin timeout y podía dejar esta tarea esperando
 * para siempre (se colgaba al volver a abrir la vista en vivo).
 */
static void close_session(bool delete_on_api)
{
    int64_t t0 = esp_timer_get_time();
    if (delete_on_api && s_lv.session_id[0]) {
        char path[64];
        snprintf(path, sizeof(path), "/stream/sessions/%s", s_lv.session_id);
        mg_api_resp_t resp;
        esp_err_t err = mg_api_request_json("DELETE", path, NULL, 0, &resp);
        ESP_LOGI(TAG, "DELETE %s → %d%s", s_lv.session_id, resp.status, err == ESP_OK ? "" : " (sin red)");
        mg_api_resp_free(&resp);
    }
    if (s_lv.ws) {
        esp_websocket_client_destroy(s_lv.ws);
        s_lv.ws = NULL;
        ESP_LOGI(TAG, "WebSocket cerrado (%" PRId64 " ms)", (esp_timer_get_time() - t0) / 1000);
    }
    s_lv.session_id[0] = '\0';
    s_lv.closed = false;
    xSemaphoreTake(s_lv.lock, portMAX_DELAY);
    s_lv.latest_new = false;
    for (int i = 0; i < N_FRAMES; i++) {
        if (s_lv.fb_state[i] != FB_SHOWN) {
            s_lv.fb_state[i] = FB_FREE;  // el que está en pantalla lo libera la interfaz
        }
    }
    xSemaphoreGive(s_lv.lock);
}

static bool alloc_buffers(void)
{
    if (s_lv.rx_buf) {
        return true;
    }
    s_lv.rx_buf = heap_caps_malloc(JPEG_MAX, MALLOC_CAP_SPIRAM);
    s_lv.latest_buf = heap_caps_malloc(JPEG_MAX, MALLOC_CAP_SPIRAM);
    s_lv.work_buf = heap_caps_malloc(JPEG_MAX, MALLOC_CAP_SPIRAM);
    bool ok = s_lv.rx_buf && s_lv.latest_buf && s_lv.work_buf;
    for (int i = 0; i < N_FRAMES && ok; i++) {
        s_lv.fb[i] = heap_caps_aligned_alloc(64, FRAME_MAX_PX * 2, MALLOC_CAP_SPIRAM);
        ok = s_lv.fb[i] != NULL;
    }
    if (!ok) {
        ESP_LOGE(TAG, "Sin PSRAM para los búferes de video");
    }
    return ok;
}

static void open_session(const cmd_t *c)
{
    if (!alloc_buffers()) {
        set_state(LIVE_ERROR, "Sin memoria para el video");
        return;
    }
    xSemaphoreTake(s_lv.lock, portMAX_DELAY);
    memset(&s_lv.st, 0, sizeof(s_lv.st));
    strlcpy(s_lv.st.node_id, c->node_id, sizeof(s_lv.st.node_id));
    s_lv.st.state = LIVE_REQUESTING;
    strlcpy(s_lv.st.msg, "Pidiendo la sesión…", sizeof(s_lv.st.msg));
    s_lv.close_reason[0] = '\0';
    xSemaphoreGive(s_lv.lock);
    s_lv.t_request_us = esp_timer_get_time();
    s_lv.closed = false;

    char body[128];
    snprintf(body, sizeof(body), "{\"node_id\":\"%s\",\"resolution\":\"%s\",\"fps_max\":%d,\"ttl_s\":%d}",
             c->node_id, c->resolution, c->fps_max, TTL_S);
    mg_api_resp_t resp;
    esp_err_t err = mg_api_request_json("POST", "/stream/sessions", body, 2048, &resp);
    if (err != ESP_OK) {
        mg_api_resp_free(&resp);
        set_state(LIVE_ERROR, "No se pudo contactar a la API");
        return;
    }
    cJSON *root = cJSON_ParseWithLength(resp.body ? resp.body : "", resp.body_len);
    int status = resp.status;
    mg_api_resp_free(&resp);
    if (status != 200 && status != 201) {
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
        const char *cs = cJSON_IsString(code) ? code->valuestring : "";
        set_state(LIVE_ERROR, strcmp(cs, "node_offline") == 0 ? "La cámara no está en línea"
                            : strcmp(cs, "stream_limit") == 0 ? "Hay demasiadas sesiones abiertas"
                            : status == 503 ? "La API no tiene conexión con el broker"
                            : "La API rechazó el pedido");
        ESP_LOGW(TAG, "POST /stream/sessions → %d %s", status, cs);
        cJSON_Delete(root);
        return;
    }
    const cJSON *sid = cJSON_GetObjectItemCaseSensitive(root, "session_id");
    const cJSON *tok = cJSON_GetObjectItemCaseSensitive(root, "token");
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "ws_view_url");
    const cJSON *res = cJSON_GetObjectItemCaseSensitive(root, "resolution");
    const cJSON *fps = cJSON_GetObjectItemCaseSensitive(root, "fps_max");
    char ws_url[URL_MAX];
    bool ok = cJSON_IsString(sid) && cJSON_IsString(tok) && cJSON_IsString(url) &&
              snprintf(ws_url, sizeof(ws_url), "%s?token=%s", url->valuestring, tok->valuestring) <
                  (int)sizeof(ws_url);
    if (ok) {
        strlcpy(s_lv.session_id, sid->valuestring, sizeof(s_lv.session_id));
        xSemaphoreTake(s_lv.lock, portMAX_DELAY);
        strlcpy(s_lv.st.resolution, cJSON_IsString(res) ? res->valuestring : c->resolution,
                sizeof(s_lv.st.resolution));
        s_lv.st.fps_max = cJSON_IsNumber(fps) ? fps->valueint : c->fps_max;
        xSemaphoreGive(s_lv.lock);
    }
    cJSON_Delete(root);
    if (!ok) {
        set_state(LIVE_ERROR, "Respuesta inválida de la API");
        return;
    }

    set_state(LIVE_CONNECTING, "Conectando…");
    esp_websocket_client_config_t cfg = {
        .uri = ws_url,
        .buffer_size = WS_BUFFER,
        .network_timeout_ms = 5000,
        .disable_auto_reconnect = true,
        .task_prio = SYS_NET_TASK_PRIO - 1,
        .task_stack = 4096,
        .task_core_id_set = true,
        .task_core_id = SYS_CORE_NET,  // fuera del núcleo de LVGL
    };
    s_lv.ws = esp_websocket_client_init(&cfg);
    if (!s_lv.ws) {
        set_state(LIVE_ERROR, "Sin memoria para el WebSocket");
        close_session(true);
        return;
    }
    esp_websocket_register_events(s_lv.ws, WEBSOCKET_EVENT_ANY, ws_event, NULL);
    if (esp_websocket_client_start(s_lv.ws) != ESP_OK) {
        set_state(LIVE_ERROR, "No se pudo abrir el WebSocket");
        close_session(true);
        return;
    }
    s_lv.t_last_frame_us = 0;
    s_lv.fps_t0 = esp_timer_get_time();
    s_lv.fps_n = 0;
    set_state(LIVE_WAITING, "Esperando a la cámara…");
    ESP_LOGI(TAG, "Sesión %s de %s (%s, %d fps)", s_lv.session_id, c->node_id, s_lv.st.resolution,
             s_lv.st.fps_max);
}

/* ---------------------------------------------------------------- decodificación */

static void decode_latest(void)
{
    xSemaphoreTake(s_lv.lock, portMAX_DELAY);
    if (!s_lv.latest_new) {
        xSemaphoreGive(s_lv.lock);
        return;
    }
    /* El último JPEG pasa a ser de esta tarea (intercambio con work_buf) */
    uint8_t *jpg = s_lv.latest_buf;
    size_t len = s_lv.latest_len;
    s_lv.latest_buf = s_lv.work_buf;
    s_lv.work_buf = jpg;
    s_lv.latest_new = false;
    /* Framebuffer libre; si no hay, se reutiliza el listo que la interfaz no tomó (atrasado) */
    int slot = -1;
    for (int i = 0; i < N_FRAMES && slot < 0; i++) {
        if (s_lv.fb_state[i] == FB_FREE) {
            slot = i;
        }
    }
    for (int i = 0; i < N_FRAMES && slot < 0; i++) {
        if (s_lv.fb_state[i] == FB_READY) {
            slot = i;
            s_lv.st.dropped++;
        }
    }
    if (slot >= 0) {
        s_lv.fb_state[slot] = FB_DECODING;
    }
    xSemaphoreGive(s_lv.lock);

    esp_err_t err = ESP_FAIL;
    esp_jpeg_image_output_t img = { 0 };
    if (slot >= 0) {
        esp_jpeg_image_cfg_t cfg = {
            .indata = jpg,
            .indata_size = len,
            .outbuf = (uint8_t *)s_lv.fb[slot],
            .outbuf_size = FRAME_MAX_PX * 2,
            .out_format = JPEG_IMAGE_FORMAT_RGB565,  // sin swap: orden nativo de LVGL
            .out_scale = JPEG_IMAGE_SCALE_0,
        };
        err = esp_jpeg_get_image_info(&cfg, &img);
        if (err == ESP_OK && (uint32_t)img.width * img.height <= FRAME_MAX_PX) {
            err = esp_jpeg_decode(&cfg, &img);
        } else if (err == ESP_OK) {
            err = ESP_ERR_INVALID_SIZE;
        }
    }

    xSemaphoreTake(s_lv.lock, portMAX_DELAY);
    if (slot >= 0) {
        if (err == ESP_OK) {
            s_lv.fb_state[slot] = FB_READY;
            s_lv.fb_w[slot] = img.width;
            s_lv.fb_h[slot] = img.height;
            s_lv.fb_seq[slot] = ++s_lv.seq;
        } else {
            s_lv.fb_state[slot] = FB_FREE;
        }
    }
    xSemaphoreGive(s_lv.lock);
    if (err != ESP_OK && slot >= 0) {
        ESP_LOGW(TAG, "Cuadro inválido: %s (%u bytes, %02x %02x … %02x %02x, %ux%u)", esp_err_to_name(err),
                 (unsigned)len, jpg[0], jpg[1], len > 1 ? jpg[len - 2] : 0, len > 1 ? jpg[len - 1] : 0,
                 img.width, img.height);
    }
}

/* ---------------------------------------------------------------- tarea */

static void update_rates(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_lv.fps_t0 >= 1000000) {
        xSemaphoreTake(s_lv.lock, portMAX_DELAY);
        uint32_t n = s_lv.st.frames - s_lv.fps_n;
        s_lv.st.fps_rx = n * 1e6f / (now - s_lv.fps_t0);
        s_lv.fps_n = s_lv.st.frames;
        xSemaphoreGive(s_lv.lock);
        s_lv.fps_t0 = now;
    }
}

static void live_task(void *arg)
{
    uint32_t last_frames = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
        cmd_t c;
        while (xQueueReceive(s_lv.cmds, &c, 0) == pdTRUE) {
            close_session(true);
            if (c.kind == CMD_START) {
                open_session(&c);
            } else {
                set_state(LIVE_IDLE, "");
            }
        }
        if (!s_lv.session_id[0]) {
            continue;
        }
        decode_latest();
        update_rates();

        live_status_t st = live_view_status();
        int64_t now = esp_timer_get_time();
        if (st.frames != last_frames) {
            if (last_frames == 0 && st.frames) {
                xSemaphoreTake(s_lv.lock, portMAX_DELAY);
                s_lv.st.first_frame_ms = (now - s_lv.t_request_us) / 1000;
                xSemaphoreGive(s_lv.lock);
                ESP_LOGI(TAG, "Primer cuadro a los %" PRIu32 " ms", (uint32_t)((now - s_lv.t_request_us) / 1000));
            }
            last_frames = st.frames;
            s_lv.t_last_frame_us = now;
            if (st.state != LIVE_STREAMING) {
                set_state(LIVE_STREAMING, "");
            }
        }
        if (s_lv.closed) {
            char r[64], msg[72];
            xSemaphoreTake(s_lv.lock, portMAX_DELAY);
            strlcpy(r, s_lv.close_reason, sizeof(r));
            xSemaphoreGive(s_lv.lock);
            reason_text(r, msg, sizeof(msg));
            ESP_LOGI(TAG, "Sesión terminada: %s", r[0] ? r : "(conexión perdida)");
            close_session(false);  // la API ya la cerró (o se cayó la red)
            set_state(LIVE_ENDED, msg);
            last_frames = 0;
        } else if (st.state == LIVE_WAITING && now - s_lv.t_request_us > FIRST_FRAME_TIMEOUT_US) {
            close_session(true);
            set_state(LIVE_ERROR, "La cámara no responde");
            last_frames = 0;
        } else if (st.state == LIVE_STREAMING && now - s_lv.t_last_frame_us > STALL_TIMEOUT_US) {
            set_state(LIVE_STREAMING, "Sin cuadros nuevos…");
        }
    }
}

/* ---------------------------------------------------------------- API pública */

esp_err_t live_view_init(void)
{
    s_lv.lock = xSemaphoreCreateMutex();
    s_lv.cmds = xQueueCreate(4, sizeof(cmd_t));
    if (!s_lv.lock || !s_lv.cmds) {
        return ESP_ERR_NO_MEM;
    }
    /* Pila en PSRAM: la RAM interna es escasa y esta tarea no escribe la flash */
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(live_task, "live", SYS_LIVE_STACK, NULL, SYS_LIVE_PRIO,
                                                    &s_lv.task, SYS_CORE_NET, MALLOC_CAP_SPIRAM);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t live_view_start(const char *node_id, const char *resolution, int fps_max)
{
    cmd_t c = { .kind = CMD_START, .fps_max = fps_max };
    strlcpy(c.node_id, node_id, sizeof(c.node_id));
    strlcpy(c.resolution, resolution, sizeof(c.resolution));
    set_state(LIVE_REQUESTING, "Pidiendo la sesión…");
    if (xQueueSend(s_lv.cmds, &c, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    xTaskNotifyGive(s_lv.task);
    return ESP_OK;
}

void live_view_stop(void)
{
    cmd_t c = { .kind = CMD_STOP };
    xQueueSend(s_lv.cmds, &c, 0);
    xTaskNotifyGive(s_lv.task);
}

live_status_t live_view_status(void)
{
    xSemaphoreTake(s_lv.lock, portMAX_DELAY);
    live_status_t st = s_lv.st;
    xSemaphoreGive(s_lv.lock);
    return st;
}

bool live_view_take_frame(const uint16_t **pixels, uint16_t *w, uint16_t *h)
{
    if (xSemaphoreTake(s_lv.lock, 0) != pdTRUE) {
        return false;  // la tarea de LVGL no espera: prueba en el próximo ciclo
    }
    int best = -1;
    for (int i = 0; i < N_FRAMES; i++) {
        if (s_lv.fb_state[i] == FB_READY && (best < 0 || s_lv.fb_seq[i] > s_lv.fb_seq[best])) {
            best = i;
        }
    }
    if (best >= 0) {
        for (int i = 0; i < N_FRAMES; i++) {
            if (s_lv.fb_state[i] == FB_SHOWN) {
                s_lv.fb_state[i] = FB_FREE;
            }
        }
        s_lv.fb_state[best] = FB_SHOWN;
        *pixels = s_lv.fb[best];
        *w = s_lv.fb_w[best];
        *h = s_lv.fb_h[best];
    }
    xSemaphoreGive(s_lv.lock);
    return best >= 0;
}

void live_view_release_frames(void)
{
    xSemaphoreTake(s_lv.lock, portMAX_DELAY);
    for (int i = 0; i < N_FRAMES; i++) {
        if (s_lv.fb_state[i] == FB_SHOWN) {
            s_lv.fb_state[i] = FB_FREE;
        }
    }
    xSemaphoreGive(s_lv.lock);
}
