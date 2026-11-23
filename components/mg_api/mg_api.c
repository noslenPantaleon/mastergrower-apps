/**
 * @file mg_api.c
 * @brief Ver mg_api.h.
 */
#include "mg_api.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "sdkconfig.h"
#if CONFIG_MG_API_USE_TLS
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "mg_api";

#define URL_MAX       160
#define HDR_MAX       256
#define BOUNDARY_LEN  32
#define WRITE_CHUNK   4096

/* URL base y token en uso: los de Kconfig hasta que mg_api_configure() los cambie (mg_settings). */
static char s_base[160] = CONFIG_MG_API_BASE_URL;
static char s_token[128] = CONFIG_MG_API_TOKEN;

esp_err_t mg_api_configure(const char *base_url, const char *token)
{
    if (base_url && base_url[0]) {
        if (strlcpy(s_base, base_url, sizeof(s_base)) >= sizeof(s_base)) {
            return ESP_ERR_INVALID_SIZE;
        }
        size_t n = strlen(s_base);
        if (n && s_base[n - 1] == '/') {
            s_base[n - 1] = '\0';  // sin barra final
        }
    }
    if (token && token[0] && strlcpy(s_token, token, sizeof(s_token)) >= sizeof(s_token)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

const char *mg_api_base_url(void)
{
    return s_base;
}

const char *mg_api_token(void)
{
    return s_token;
}

esp_err_t mg_api_url(const char *url_or_path, char *out, size_t out_len)
{
    if (!url_or_path || !out || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    int n;
    if (strncmp(url_or_path, "http://", 7) == 0 || strncmp(url_or_path, "https://", 8) == 0) {
        n = snprintf(out, out_len, "%s", url_or_path);
    } else if (url_or_path[0] == '/') {
        const char *base = s_base;
        const char *host = strstr(base, "://");
        if (!host) {
            return ESP_ERR_INVALID_ARG;
        }
        size_t prefix = (size_t)(host - base) + 3;
        size_t host_len = strcspn(host + 3, "/");
        n = snprintf(out, out_len, "%.*s%s", (int)(prefix + host_len), base, url_or_path);
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return (n > 0 && (size_t)n < out_len) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t mg_api_ws_url(const char *url_or_path, char *out, size_t out_len)
{
    if (!url_or_path || !out || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    int n;
    if (strncmp(url_or_path, "ws://", 5) == 0 || strncmp(url_or_path, "wss://", 6) == 0) {
        n = snprintf(out, out_len, "%s", url_or_path);
    } else if (url_or_path[0] == '/') {
        /* "http://host:8000/api/v1" → "ws://host:8000" + ruta */
        const char *base = s_base;
        bool tls = strncmp(base, "https://", 8) == 0;
        const char *host = strstr(base, "://");
        if (!host) {
            return ESP_ERR_INVALID_ARG;
        }
        host += 3;
        size_t host_len = strcspn(host, "/");
        n = snprintf(out, out_len, "%s://%.*s%s", tls ? "wss" : "ws", (int)host_len, host, url_or_path);
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return (n > 0 && (size_t)n < out_len) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

void mg_api_resp_free(mg_api_resp_t *resp)
{
    if (resp) {
        free(resp->body);
        resp->body = NULL;
        resp->body_len = 0;
    }
}

static esp_http_client_handle_t open_client(const char *path, esp_http_client_method_t method)
{
    char url[URL_MAX];
    int n = snprintf(url, sizeof(url), "%s%s", s_base, path);
    if (n < 0 || n >= (int)sizeof(url)) {
        ESP_LOGE(TAG, "URL demasiado larga: %s", path);
        return NULL;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .method = method,
        .timeout_ms = CONFIG_MG_API_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .keep_alive_enable = false,
#if CONFIG_MG_API_USE_TLS
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return NULL;
    }
    char auth[HDR_MAX];
    snprintf(auth, sizeof(auth), "Bearer %s", s_token);
    esp_http_client_set_header(c, "Authorization", auth);
    return c;
}

static esp_err_t write_all(esp_http_client_handle_t c, const void *data, size_t len)
{
    const char *p = data;
    while (len) {
        int chunk = len > WRITE_CHUNK ? WRITE_CHUNK : (int)len;
        int w = esp_http_client_write(c, p, chunk);
        if (w <= 0) {
            return ESP_FAIL;
        }
        p += w;
        len -= (size_t)w;
    }
    return ESP_OK;
}

/** Lee la respuesta (encabezados ya recibidos) y guarda hasta max_body bytes. */
static void read_body(esp_http_client_handle_t c, size_t max_body, mg_api_resp_t *resp)
{
    resp->status = esp_http_client_get_status_code(c);
    if (!max_body) {
        esp_http_client_flush_response(c, NULL);
        return;
    }
    char *buf = malloc(max_body + 1);
    if (!buf) {
        esp_http_client_flush_response(c, NULL);
        return;
    }
    size_t got = 0;
    for (;;) {
        char tmp[256];
        int r = esp_http_client_read(c, tmp, sizeof(tmp));
        if (r <= 0) {
            break;
        }
        size_t keep = got + (size_t)r > max_body ? max_body - got : (size_t)r;
        memcpy(buf + got, tmp, keep);
        got += keep;
    }
    buf[got] = '\0';
    resp->body = buf;
    resp->body_len = got;
}

/* ---------------------------------------------------------------- multipart */

static int part_header(char *out, size_t sz, const char *boundary, const mg_api_part_t *p)
{
    int n = snprintf(out, sz, "--%s\r\nContent-Disposition: form-data; name=\"%s\"", boundary, p->name);
    if (p->filename) {
        n += snprintf(out + n, sz - (size_t)n, "; filename=\"%s\"", p->filename);
    }
    n += snprintf(out + n, sz - (size_t)n, "\r\n");
    if (p->content_type) {
        n += snprintf(out + n, sz - (size_t)n, "Content-Type: %s\r\n", p->content_type);
    }
    n += snprintf(out + n, sz - (size_t)n, "\r\n");
    return n < (int)sz ? n : -1;
}

esp_err_t mg_api_post_multipart(const char *path, const mg_api_part_t *parts, size_t n_parts,
                                const char *idempotency_key, size_t max_body, mg_api_resp_t *resp)
{
    ESP_RETURN_ON_FALSE(path && parts && n_parts && resp, ESP_ERR_INVALID_ARG, TAG, "argumentos");
    *resp = (mg_api_resp_t){ 0 };

    char boundary[BOUNDARY_LEN + 1];
    snprintf(boundary, sizeof(boundary), "mgb%08" PRIx32 "%08" PRIx32 "%08" PRIx32, esp_random(),
             esp_random(), esp_random());

    /* Largo total: así se manda con Content-Length (sin chunked) y el servidor sabe cuánto esperar. */
    char hdr[HDR_MAX];
    size_t total = 0;
    for (size_t i = 0; i < n_parts; i++) {
        int h = part_header(hdr, sizeof(hdr), boundary, &parts[i]);
        ESP_RETURN_ON_FALSE(h > 0, ESP_ERR_INVALID_SIZE, TAG, "encabezado de parte muy largo");
        total += (size_t)h + parts[i].len + 2;  // + "\r\n"
    }
    char tail[BOUNDARY_LEN + 8];
    int tail_len = snprintf(tail, sizeof(tail), "--%s--\r\n", boundary);
    total += (size_t)tail_len;

    esp_http_client_handle_t c = open_client(path, HTTP_METHOD_POST);
    ESP_RETURN_ON_FALSE(c, ESP_ERR_NO_MEM, TAG, "no se pudo crear el cliente");

    char ctype[64];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(c, "Content-Type", ctype);
    if (idempotency_key && idempotency_key[0]) {
        esp_http_client_set_header(c, "Idempotency-Key", idempotency_key);
    }

    esp_err_t err = esp_http_client_open(c, (int)total);
    for (size_t i = 0; err == ESP_OK && i < n_parts; i++) {
        int h = part_header(hdr, sizeof(hdr), boundary, &parts[i]);
        err = write_all(c, hdr, (size_t)h);
        if (err == ESP_OK) {
            err = write_all(c, parts[i].data, parts[i].len);
        }
        if (err == ESP_OK) {
            err = write_all(c, "\r\n", 2);
        }
    }
    if (err == ESP_OK) {
        err = write_all(c, tail, (size_t)tail_len);
    }
    if (err == ESP_OK && esp_http_client_fetch_headers(c) < 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        read_body(c, max_body, resp);
    } else {
        ESP_LOGD(TAG, "POST %s sin respuesta: %s", path, esp_err_to_name(err));
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

esp_err_t mg_api_get(const char *path, const char *if_none_match, size_t max_body, mg_api_resp_t *resp)
{
    ESP_RETURN_ON_FALSE(path && resp, ESP_ERR_INVALID_ARG, TAG, "argumentos");
    *resp = (mg_api_resp_t){ 0 };
    esp_http_client_handle_t c = open_client(path, HTTP_METHOD_GET);
    ESP_RETURN_ON_FALSE(c, ESP_ERR_NO_MEM, TAG, "no se pudo crear el cliente");
    if (if_none_match && if_none_match[0]) {
        esp_http_client_set_header(c, "If-None-Match", if_none_match);
    }

    esp_err_t err = esp_http_client_open(c, 0);
    if (err == ESP_OK && esp_http_client_fetch_headers(c) < 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        read_body(c, max_body, resp);
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

esp_err_t mg_api_request_json(const char *method, const char *path, const char *json, size_t max_body,
                              mg_api_resp_t *resp)
{
    ESP_RETURN_ON_FALSE(method && path && resp, ESP_ERR_INVALID_ARG, TAG, "argumentos");
    *resp = (mg_api_resp_t){ 0 };
    esp_http_client_method_t m;
    if (strcmp(method, "POST") == 0) {
        m = HTTP_METHOD_POST;
    } else if (strcmp(method, "PUT") == 0) {
        m = HTTP_METHOD_PUT;
    } else if (strcmp(method, "PATCH") == 0) {
        m = HTTP_METHOD_PATCH;
    } else if (strcmp(method, "DELETE") == 0) {
        m = HTTP_METHOD_DELETE;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    esp_http_client_handle_t c = open_client(path, m);
    ESP_RETURN_ON_FALSE(c, ESP_ERR_NO_MEM, TAG, "no se pudo crear el cliente");
    size_t len = json ? strlen(json) : 0;
    if (len) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
    }
    esp_err_t err = esp_http_client_open(c, (int)len);
    if (err == ESP_OK && len) {
        err = write_all(c, json, len);
    }
    if (err == ESP_OK && esp_http_client_fetch_headers(c) < 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        read_body(c, max_body, resp);
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}
