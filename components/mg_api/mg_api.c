/**
 * @file mg_api.c
 * @brief Ver mg_api.h.
 */
#include "mg_api.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
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

const char *mg_api_base_url(void)
{
    return CONFIG_MG_API_BASE_URL;
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
    int n = snprintf(url, sizeof(url), "%s%s", CONFIG_MG_API_BASE_URL, path);
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
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_MG_API_TOKEN);
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

esp_err_t mg_api_get(const char *path, size_t max_body, mg_api_resp_t *resp)
{
    ESP_RETURN_ON_FALSE(path && resp, ESP_ERR_INVALID_ARG, TAG, "argumentos");
    *resp = (mg_api_resp_t){ 0 };
    esp_http_client_handle_t c = open_client(path, HTTP_METHOD_GET);
    ESP_RETURN_ON_FALSE(c, ESP_ERR_NO_MEM, TAG, "no se pudo crear el cliente");

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
