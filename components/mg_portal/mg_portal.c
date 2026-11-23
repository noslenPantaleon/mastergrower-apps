/**
 * @file mg_portal.c
 * @brief Ver mg_portal.h.
 *
 * Flujo: GET / (formulario) → GET /scan (redes, JSON) → POST /save (arranca la prueba y devuelve
 * una página que consulta GET /result cada 2 s) → si WiFi y servidor responden, guarda y reinicia;
 * si solo falla el servidor, ofrece POST /force ("guardar igual").
 * La prueba es asíncrona porque al conectarse al router el AP cambia al canal del router y el
 * celular se desconecta unos segundos: la página reintenta sola.
 */
#include "mg_portal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mg_settings.h"
#include "sdkconfig.h"

static const char *TAG = "mg_portal";

void mg_portal_dns_start(uint32_t ip_net_order);

#define BIT_GOT_IP  BIT0
#define MAX_APS     20
#define BODY_MAX    1024

typedef enum { T_IDLE, T_TESTING, T_OK, T_WIFI_FAIL, T_SERVER_FAIL } test_state_t;

static struct {
    char node[24];
    mg_settings_t cur;        /**< valores actuales (los que no se escriben se conservan) */
    mg_settings_t pending;    /**< lo que se está probando */
    volatile test_state_t state;
    char detail[256];
    EventGroupHandle_t ev;
    volatile int64_t last_activity_us;
    int timeout_s;
} s_p;

/* ---------------------------------------------------------------- utilidades */

static void touch(void)
{
    s_p.last_activity_us = esp_timer_get_time();
}

static void restart_cb(void *arg)
{
    esp_restart();
}

static void restart_in(int ms)
{
    const esp_timer_create_args_t a = { .callback = restart_cb, .name = "portal_rst" };
    esp_timer_handle_t t;
    if (esp_timer_create(&a, &t) == ESP_OK) {
        esp_timer_start_once(t, (uint64_t)ms * 1000);
    }
}

static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') {
            *o++ = ' ';
        } else if (*s == '%' && s[1] && s[2]) {
            char h[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = '\0';
}

/** Valor de `key` en un cuerpo x-www-form-urlencoded (decodificado). */
static bool form_get(const char *body, const char *key, char *out, size_t len)
{
    size_t kl = strlen(key);
    for (const char *p = body; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t n = strcspn(v, "&");
            snprintf(out, len, "%.*s", (int)n, v);
            url_decode(out);
            return true;
        }
    }
    return false;
}

/** Escapa para HTML/JSON (comillas, <, >, &, \). */
static void esc(const char *in, char *out, size_t len)
{
    size_t o = 0;
    for (; *in && o + 7 < len; in++) {
        const char *r = NULL;
        switch (*in) {
        case '"': r = "&quot;"; break;
        case '<': r = "&lt;"; break;
        case '>': r = "&gt;"; break;
        case '&': r = "&amp;"; break;
        case '\\': r = "\\\\"; break;
        default: break;
        }
        if (r) {
            o += snprintf(out + o, len - o, "%s", r);
        } else {
            out[o++] = *in;
        }
    }
    out[o] = '\0';
}

/* ---------------------------------------------------------------- prueba de conexión */

static bool tcp_reachable(const char *host, int port)
{
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM }, *res = NULL;
    char p[8];
    snprintf(p, sizeof(p), "%d", port);
    if (getaddrinfo(host, p, &hints, &res) != 0 || !res) {
        return false;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    bool ok = false;
    if (s >= 0) {
        struct timeval tv = { .tv_sec = 4 };
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        ok = connect(s, res->ai_addr, res->ai_addrlen) == 0;
        close(s);
    }
    freeaddrinfo(res);
    return ok;
}

static void test_task(void *arg)
{
    mg_settings_t *t = &s_p.pending;
    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, t->wifi_ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, t->wifi_pass, sizeof(sta.sta.password));
    esp_wifi_disconnect();
    xEventGroupClearBits(s_p.ev, BIT_GOT_IP);
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_connect();
    EventBits_t b = xEventGroupWaitBits(s_p.ev, BIT_GOT_IP, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if (!(b & BIT_GOT_IP)) {
        esp_wifi_disconnect();
        snprintf(s_p.detail, sizeof(s_p.detail), "No se pudo conectar a «%s». Revisá la contraseña.", t->wifi_ssid);
        s_p.state = T_WIFI_FAIL;
        vTaskDelete(NULL);
    }
    bool mq = tcp_reachable(t->server, CONFIG_MG_SETTINGS_MQTT_PORT);
    bool api = tcp_reachable(t->server, CONFIG_MG_SETTINGS_API_PORT);
    if (mq && api) {
        mg_settings_save(t);
        snprintf(s_p.detail, sizeof(s_p.detail), "Conectado a «%s» y el servidor %s responde. Guardado: "
                 "la cámara se reinicia.", t->wifi_ssid, t->server);
        s_p.state = T_OK;
        restart_in(6000);
    } else {
        snprintf(s_p.detail, sizeof(s_p.detail), "WiFi bien, pero el servidor %s no responde (MQTT %s, API %s).",
                 t->server, mq ? "ok" : "no", api ? "ok" : "no");
        s_p.state = T_SERVER_FAIL;
    }
    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------- páginas */

static const char HEAD[] =
    "<!doctype html><html lang='es'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'><title>MasterGrow</title><style>"
    "body{margin:0;background:#16201a;color:#e6ede4;font:16px/1.4 system-ui,sans-serif}"
    "main{max-width:420px;margin:0 auto;padding:20px}h1{font-size:22px;margin:0 0 4px}"
    "p{color:#9aa89a;margin:0 0 16px}label{display:block;font-size:14px;color:#9aa89a;margin:14px 0 4px}"
    "input,select{width:100%;box-sizing:border-box;padding:12px;border:0;border-radius:8px;"
    "background:#263529;color:#e6ede4;font-size:16px}button{width:100%;margin-top:20px;padding:14px;"
    "border:0;border-radius:8px;background:#2e6b33;color:#e6f2e4;font-size:17px}"
    ".card{background:#1f2c22;border-radius:12px;padding:16px;margin-top:12px}.ok{color:#7cc47f}"
    ".bad{color:#e06c5a}small{color:#9aa89a}</style></head><body><main>";

static esp_err_t send_page(httpd_req_t *r, const char *body)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(r, HEAD);
    httpd_resp_sendstr_chunk(r, body);
    httpd_resp_sendstr_chunk(r, "</main></body></html>");
    return httpd_resp_sendstr_chunk(r, NULL);
}

static esp_err_t h_index(httpd_req_t *r)
{
    touch();
    char ssid[80], server[140], *page = malloc(3600);
    if (!page) {
        return ESP_FAIL;
    }
    esc(s_p.cur.wifi_ssid, ssid, sizeof(ssid));
    esc(s_p.cur.server, server, sizeof(server));
    snprintf(page, 3600,
             "<h1>MasterGrow · %s</h1><p>Configuración de red y servidor</p>"
             "<form method='post' action='/save'>"
             "<label>Red WiFi</label><select id='nets' onchange='ssid.value=this.value'>"
             "<option value=''>Buscando redes…</option></select>"
             "<input id='ssid' name='ssid' value=\"%s\" placeholder='Nombre de la red' required>"
             "<label>Contraseña WiFi</label><input name='wpass' type='password' placeholder='(sin cambios)'>"
             "<label>Servidor (IP o nombre .local)</label><input name='server' value=\"%s\" required>"
             "<label>Contraseña MQTT <small>(usuario %s)</small></label>"
             "<input name='mpass' type='password' placeholder='(sin cambios)'>"
             "<label>Token de la API</label><input name='token' type='password' placeholder='(sin cambios)'>"
             "<button>Probar y guardar</button></form>"
             "<p style='margin-top:16px'><small>Los campos de contraseña vacíos conservan el valor actual.</small></p>"
             "<script>fetch('/scan').then(r=>r.json()).then(l=>{let s=document.getElementById('nets');"
             "s.innerHTML='<option value=\"\">Elegí una red ('+l.length+')</option>';"
             "l.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;"
             "o.textContent=n.ssid+'  ('+n.rssi+' dBm'+(n.open?', abierta':'')+')';s.appendChild(o)})})"
             ".catch(()=>{})</script>",
             s_p.node, ssid, server, s_p.node);
    esp_err_t e = send_page(r, page);
    free(page);
    return e;
}

static esp_err_t h_scan(httpd_req_t *r)
{
    touch();
    wifi_scan_config_t sc = { 0 };
    uint16_t n = 0;
    wifi_ap_record_t *recs = NULL;
    if (esp_wifi_scan_start(&sc, true) == ESP_OK && esp_wifi_scan_get_ap_num(&n) == ESP_OK && n) {
        recs = calloc(n, sizeof(*recs));
        if (recs) {
            esp_wifi_scan_get_ap_records(&n, recs);
        }
    }
    httpd_resp_set_type(r, "application/json");
    httpd_resp_sendstr_chunk(r, "[");
    int sent = 0;
    for (int i = 0; recs && i < n && sent < MAX_APS; i++) {
        if (!recs[i].ssid[0]) {
            continue;
        }
        bool dup = false;
        for (int k = 0; k < i && !dup; k++) {
            dup = strcmp((char *)recs[k].ssid, (char *)recs[i].ssid) == 0;
        }
        if (dup) {
            continue;
        }
        char e[80], item[140];
        esc((char *)recs[i].ssid, e, sizeof(e));
        snprintf(item, sizeof(item), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s}", sent ? "," : "", e,
                 recs[i].rssi, recs[i].authmode == WIFI_AUTH_OPEN ? "true" : "false");
        httpd_resp_sendstr_chunk(r, item);
        sent++;
    }
    free(recs);
    httpd_resp_sendstr_chunk(r, "]");
    return httpd_resp_sendstr_chunk(r, NULL);
}

static const char TESTING_PAGE[] =
    "<h1>Probando…</h1><div class='card' id='st'>Conectando a la red y al servidor. Puede tardar hasta "
    "30 s; si el celular se desconecta un momento, volvé a esta red y la página sigue sola.</div>"
    "<form method='post' action='/force' id='force' style='display:none'>"
    "<button>Guardar igual</button></form><p style='margin-top:16px'><a href='/' style='color:#7cc47f'>"
    "Volver al formulario</a></p>"
    "<script>function q(){fetch('/result').then(r=>r.json()).then(j=>{let s=document.getElementById('st');"
    "if(j.state=='testing'){setTimeout(q,2000);return}"
    "s.innerHTML='<span class='+(j.state=='ok'?'ok':'bad')+'>'+j.detail+'</span>';"
    "if(j.state=='server_fail')document.getElementById('force').style.display='block'})"
    ".catch(()=>setTimeout(q,2000))}setTimeout(q,2000)</script>";

static esp_err_t h_save(httpd_req_t *r)
{
    touch();
    if (s_p.state == T_TESTING) {
        return send_page(r, TESTING_PAGE);
    }
    char body[BODY_MAX + 1];
    int len = r->content_len < BODY_MAX ? r->content_len : BODY_MAX, got = 0;
    while (got < len) {
        int n = httpd_req_recv(r, body + got, len - got);
        if (n <= 0) {
            return ESP_FAIL;
        }
        got += n;
    }
    body[got] = '\0';
    mg_settings_t t = s_p.cur;
    char v[128];
    if (form_get(body, "ssid", v, sizeof(v)) && v[0]) {
        strlcpy(t.wifi_ssid, v, sizeof(t.wifi_ssid));
    }
    if (form_get(body, "wpass", v, sizeof(v)) && v[0]) {
        strlcpy(t.wifi_pass, v, sizeof(t.wifi_pass));
    }
    if (form_get(body, "server", v, sizeof(v)) && v[0]) {
        strlcpy(t.server, v, sizeof(t.server));
    }
    if (form_get(body, "mpass", v, sizeof(v)) && v[0]) {
        strlcpy(t.mqtt_pass, v, sizeof(t.mqtt_pass));
    }
    if (form_get(body, "token", v, sizeof(v)) && v[0]) {
        strlcpy(t.api_token, v, sizeof(t.api_token));
    }
    s_p.pending = t;
    s_p.state = T_TESTING;
    ESP_LOGI(TAG, "Probando WiFi \"%s\" y servidor %s", t.wifi_ssid, t.server);
    if (xTaskCreate(test_task, "portal_test", 4096, NULL, 4, NULL) != pdPASS) {
        s_p.state = T_IDLE;
    }
    return send_page(r, TESTING_PAGE);
}

static esp_err_t h_result(httpd_req_t *r)
{
    touch();
    static const char *names[] = { "idle", "testing", "ok", "wifi_fail", "server_fail" };
    char d[400], out[480];
    esc(s_p.detail, d, sizeof(d));
    snprintf(out, sizeof(out), "{\"state\":\"%s\",\"detail\":\"%s\"}", names[s_p.state], d);
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, out);
}

static esp_err_t h_force(httpd_req_t *r)
{
    touch();
    if (s_p.state != T_SERVER_FAIL) {
        return send_page(r, "<h1>Nada para guardar</h1><p><a href='/' style='color:#7cc47f'>Volver</a></p>");
    }
    mg_settings_save(&s_p.pending);  // corre en la tarea de httpd (pila interna): puede escribir la flash
    restart_in(4000);
    return send_page(r, "<h1>Guardado</h1><div class='card'>La cámara se reinicia y se conecta a la red. "
                        "Cuando el servidor esté disponible se conecta sola.</div>");
}

/** Cualquier otra ruta (detección de portal cautivo de Android/iOS/Windows) → al formulario. */
static esp_err_t h_redirect(httpd_req_t *r, httpd_err_code_t err)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
    return httpd_resp_send(r, NULL, 0);
}

/* ---------------------------------------------------------------- arranque */

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_p.ev, BIT_GOT_IP);
    }
}

static void idle_check(void *arg)
{
    if (esp_timer_get_time() - s_p.last_activity_us > (int64_t)s_p.timeout_s * 1000000 && s_p.state != T_TESTING) {
        ESP_LOGW(TAG, "Portal sin uso %d s: reinicio en modo normal", s_p.timeout_s);
        esp_restart();
    }
}

void mg_portal_run(const mg_portal_config_t *cfg)
{
    strlcpy(s_p.node, cfg->node_id, sizeof(s_p.node));
    s_p.timeout_s = cfg->timeout_s > 0 ? cfg->timeout_s : 900;
    mg_settings_load(&s_p.cur);
    s_p.ev = xEventGroupCreate();
    touch();

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    wifi_config_t ap = { .ap = { .channel = 1, .max_connection = 4, .authmode = WIFI_AUTH_WPA2_PSK } };
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "MasterGrow-%s-%02X%02X", cfg->node_id, mac[4], mac[5]);
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "mg%02x%02x%02x", mac[3], mac[4], mac[5]);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGW(TAG, "MODO CONFIGURACIÓN · red \"%s\" · contraseña \"%s\" · http://192.168.4.1", ap.ap.ssid,
             ap.ap.password);

    esp_netif_ip_info_t ipi;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ipi);
    mg_portal_dns_start(ipi.ip.addr);

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 8;
    hc.lru_purge_enable = true;
    hc.stack_size = 6144;
    httpd_handle_t h;
    ESP_ERROR_CHECK(httpd_start(&h, &hc));
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_index },
        { .uri = "/scan", .method = HTTP_GET, .handler = h_scan },
        { .uri = "/save", .method = HTTP_POST, .handler = h_save },
        { .uri = "/result", .method = HTTP_GET, .handler = h_result },
        { .uri = "/force", .method = HTTP_POST, .handler = h_force },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(h, &uris[i]);
    }
    httpd_register_err_handler(h, HTTPD_404_NOT_FOUND, h_redirect);

    const esp_timer_create_args_t ta = { .callback = idle_check, .name = "portal_idle" };
    esp_timer_handle_t t;
    esp_timer_create(&ta, &t);
    esp_timer_start_periodic(t, 10 * 1000000);

    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
