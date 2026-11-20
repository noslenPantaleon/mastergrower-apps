/**
 * @file storage_queue.c
 * @brief Ver storage_queue.h.
 *
 * Formato del id: "20261111T043324Z_h_3f2a9c1b"  (hora UTC _ prioridad h(igh)|n(ormal) _ aleatorio)
 * Sin hora válida la parte de fecha es "19700101T000000Z" (ts = 0).
 */
#include "storage_queue.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "storage_queue";

#define PATH_MAX_LEN 96
#define IO_BUF_LEN   (8 * 1024)

static struct {
    SemaphoreHandle_t lock;
    char root[24];          /**< punto de montaje (p. ej. "/sd") */
    char dir[48];           /**< <root>/mg/pend */
    sq_entry_t *idx;        /**< índice en PSRAM, en orden de llegada */
    size_t n;
    bool full_latched;
} s_sq;

/* ------------------------------------------------------------------ utilidades */

static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

/** Interpreta un id. Devuelve false si no tiene el formato esperado. */
static bool parse_id(const char *id, time_t *ts, bool *high)
{
    int y, mo, d, h, mi, s;
    char p;
    if (sscanf(id, "%4d%2d%2dT%2d%2d%2dZ_%c_", &y, &mo, &d, &h, &mi, &s, &p) != 7 || (p != 'h' && p != 'n')) {
        return false;
    }
    *high = p == 'h';
    *ts = (y <= 1970) ? 0 : (time_t)(days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + s);
    return true;
}

static void make_id(char *out, time_t ts, bool high)
{
    struct tm tm;
    gmtime_r(&ts, &tm);
    char date[20];
    strftime(date, sizeof(date), "%Y%m%dT%H%M%SZ", &tm);
    snprintf(out, SQ_ID_MAX_LEN, "%s_%c_%08" PRIx32, date, high ? 'h' : 'n', esp_random());
}

static void path_of(char *out, const char *id, const char *ext)
{
    snprintf(out, PATH_MAX_LEN, "%s/%s%s", s_sq.dir, id, ext);
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

static bool file_exists(const char *path, uint32_t *size)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    if (size) {
        *size = (uint32_t)st.st_size;
    }
    return true;
}

static esp_err_t write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "No se pudo crear %s (errno %d)", path, errno);
        return ESP_FAIL;
    }
    setvbuf(f, NULL, _IOFBF, IO_BUF_LEN);
    size_t w = fwrite(data, 1, len, f);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    if (w != len) {
        ESP_LOGE(TAG, "Escritura incompleta en %s (%u/%u)", path, (unsigned)w, (unsigned)len);
        unlink(path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static int free_pct(void)
{
    uint64_t total = 0, free_b = 0;
    if (esp_vfs_fat_info(s_sq.root, &total, &free_b) != ESP_OK || total == 0) {
        return -1;
    }
    return (int)(free_b * 100 / total);
}

/* ------------------------------------------------------------------ índice (con lock tomado) */

static void idx_remove_at(size_t i)
{
    memmove(&s_sq.idx[i], &s_sq.idx[i + 1], (s_sq.n - i - 1) * sizeof(sq_entry_t));
    s_sq.n--;
}

static int idx_find(const char *id)
{
    for (size_t i = 0; i < s_sq.n; i++) {
        if (strcmp(s_sq.idx[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void delete_files(const char *id)
{
    char p[PATH_MAX_LEN];
    path_of(p, id, ".jpg");
    unlink(p);
    path_of(p, id, ".json");
    unlink(p);
}

/** Política de limpieza: borra la rutinaria más vieja; si solo quedan de prioridad alta, la más vieja. */
static bool evict_one(void)
{
    int victim = -1;
    for (int pass = 0; pass < 2 && victim < 0; pass++) {
        bool want_high = pass == 1;
        for (size_t i = 0; i < s_sq.n; i++) {
            if (s_sq.idx[i].high == want_high && (victim < 0 || s_sq.idx[i].ts < s_sq.idx[victim].ts)) {
                victim = (int)i;
            }
        }
    }
    if (victim < 0) {
        return false;
    }
    ESP_LOGW(TAG, "Limpieza: se descarta %s (%s)", s_sq.idx[victim].id, s_sq.idx[victim].high ? "high" : "routine");
    delete_files(s_sq.idx[victim].id);
    idx_remove_at((size_t)victim);
    return true;
}

static void enforce_limits(void)
{
    while (s_sq.n >= CONFIG_MG_SQ_MAX_PENDING && evict_one()) {
    }
    int fp = free_pct();
    while (fp >= 0 && fp < CONFIG_MG_SQ_MIN_FREE_PCT && evict_one()) {
        fp = free_pct();
    }
}

/* ------------------------------------------------------------------ API */

static esp_err_t ensure_dirs(void)
{
    char p[PATH_MAX_LEN];
    snprintf(p, sizeof(p), "%s/mg", s_sq.root);
    if (mkdir(p, 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    if (mkdir(s_sq.dir, 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void rebuild_index(void)
{
    DIR *d = opendir(s_sq.dir);
    if (!d) {
        return;
    }
    size_t removed = 0;
    struct dirent *e;
    char p[PATH_MAX_LEN];

    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        if (ends_with(name, ".tmp") || ends_with(name, ".jtm")) {
            if (snprintf(p, sizeof(p), "%s/%s", s_sq.dir, name) < (int)sizeof(p)) {
                unlink(p);
            }  // resto de una escritura interrumpida
            removed++;
            continue;
        }
        if (!ends_with(name, ".jpg")) {
            continue;
        }
        char id[SQ_ID_MAX_LEN];
        size_t idlen = strlen(name) - 4;
        if (idlen >= sizeof(id)) {
            continue;
        }
        memcpy(id, name, idlen);
        id[idlen] = '\0';

        sq_entry_t ent = { 0 };
        strlcpy(ent.id, id, sizeof(ent.id));
        path_of(p, id, ".json");
        if (!parse_id(id, &ent.ts, &ent.high) || !file_exists(p, NULL)) {
            delete_files(id);  // sin metadatos o nombre ajeno: no se puede subir
            removed++;
            continue;
        }
        path_of(p, id, ".jpg");
        file_exists(p, &ent.jpeg_len);
        if (s_sq.n < CONFIG_MG_SQ_MAX_PENDING) {
            s_sq.idx[s_sq.n++] = ent;
        }
    }
    closedir(d);

    /* .json sin su .jpg */
    d = opendir(s_sq.dir);
    while (d && (e = readdir(d)) != NULL) {
        if (ends_with(e->d_name, ".json")) {
            char id[SQ_ID_MAX_LEN];
            size_t idlen = strlen(e->d_name) - 5;
            if (idlen < sizeof(id)) {
                memcpy(id, e->d_name, idlen);
                id[idlen] = '\0';
                if (idx_find(id) < 0) {
                    if (snprintf(p, sizeof(p), "%s/%s", s_sq.dir, e->d_name) < (int)sizeof(p)) {
                        unlink(p);
                    }
                    removed++;
                }
            }
        }
    }
    if (d) {
        closedir(d);
    }
    ESP_LOGI(TAG, "Índice reconstruido: %u pendientes (%u restos incompletos borrados)", (unsigned)s_sq.n,
             (unsigned)removed);
}

esp_err_t storage_queue_init(const char *root)
{
    if (s_sq.idx) {
        return ESP_ERR_INVALID_STATE;
    }
    strlcpy(s_sq.root, root, sizeof(s_sq.root));
    snprintf(s_sq.dir, sizeof(s_sq.dir), "%s/mg/pend", root);

    s_sq.lock = xSemaphoreCreateMutex();
    s_sq.idx = heap_caps_calloc(CONFIG_MG_SQ_MAX_PENDING, sizeof(sq_entry_t), MALLOC_CAP_SPIRAM);
    if (!s_sq.lock || !s_sq.idx) {
        return ESP_ERR_NO_MEM;
    }
    if (ensure_dirs() != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo crear %s", s_sq.dir);
        heap_caps_free(s_sq.idx);
        s_sq.idx = NULL;
        return ESP_FAIL;
    }
    rebuild_index();
    return ESP_OK;
}

bool storage_queue_ready(void)
{
    return s_sq.idx != NULL;
}

esp_err_t storage_queue_put(const uint8_t *jpeg, size_t jpeg_len, const char *meta_json, time_t ts,
                            bool high, char *out_id)
{
    if (!s_sq.idx) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!jpeg || !jpeg_len || !meta_json || !out_id) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_sq.lock, portMAX_DELAY);
    enforce_limits();

    char id[SQ_ID_MAX_LEN];
    make_id(id, ts, high);
    char tmp_jpg[PATH_MAX_LEN], tmp_json[PATH_MAX_LEN], fin_jpg[PATH_MAX_LEN], fin_json[PATH_MAX_LEN];
    path_of(tmp_jpg, id, ".tmp");
    path_of(tmp_json, id, ".jtm");
    path_of(fin_jpg, id, ".jpg");
    path_of(fin_json, id, ".json");

    /* Orden: datos a temporales → .json definitivo → .jpg definitivo. Una captura cuenta solo
     * cuando existe el .jpg, y para entonces su .json ya está completo. */
    esp_err_t err = write_file(tmp_jpg, jpeg, jpeg_len);
    if (err == ESP_OK) {
        err = write_file(tmp_json, meta_json, strlen(meta_json));
    }
    if (err == ESP_OK && (rename(tmp_json, fin_json) != 0 || rename(tmp_jpg, fin_jpg) != 0)) {
        ESP_LOGE(TAG, "rename falló (errno %d)", errno);
        err = ESP_FAIL;
    }
    if (err != ESP_OK) {
        unlink(tmp_jpg);
        unlink(tmp_json);
        unlink(fin_json);
        unlink(fin_jpg);
        xSemaphoreGive(s_sq.lock);
        return err;
    }

    sq_entry_t *ent = &s_sq.idx[s_sq.n++];
    *ent = (sq_entry_t){ .ts = ts, .high = high, .jpeg_len = (uint32_t)jpeg_len, .in_progress = true };
    strlcpy(ent->id, id, sizeof(ent->id));
    strlcpy(out_id, id, SQ_ID_MAX_LEN);
    size_t n = s_sq.n;
    xSemaphoreGive(s_sq.lock);

    ESP_LOGI(TAG, "Guardada %s (%u bytes) · pendientes %u", id, (unsigned)jpeg_len, (unsigned)n);
    return ESP_OK;
}

esp_err_t storage_queue_set_ready(const char *id)
{
    if (!s_sq.idx) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_sq.lock, portMAX_DELAY);
    int i = idx_find(id);
    if (i >= 0) {
        s_sq.idx[i].in_progress = false;
    }
    xSemaphoreGive(s_sq.lock);
    return i >= 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t storage_queue_update_meta(const char *id, const char *meta_json)
{
    if (!s_sq.idx) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_sq.lock, portMAX_DELAY);
    if (idx_find(id) < 0) {
        xSemaphoreGive(s_sq.lock);
        return ESP_ERR_NOT_FOUND;  // ya se subió o se descartó
    }
    char tmp[PATH_MAX_LEN], fin[PATH_MAX_LEN];
    path_of(tmp, id, ".jtm");
    path_of(fin, id, ".json");
    esp_err_t err = write_file(tmp, meta_json, strlen(meta_json));
    if (err == ESP_OK) {
        unlink(fin);  // FATFS no permite rename sobre un archivo existente
        if (rename(tmp, fin) != 0) {
            err = ESP_FAIL;
        }
    }
    xSemaphoreGive(s_sq.lock);
    return err;
}

esp_err_t storage_queue_promote(const char *id, const char *meta_json, char *out_id)
{
    if (!s_sq.idx) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!id || !meta_json || !out_id) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_sq.lock, portMAX_DELAY);
    int i = idx_find(id);
    if (i < 0) {
        xSemaphoreGive(s_sq.lock);
        return ESP_ERR_NOT_FOUND;
    }
    if (s_sq.idx[i].high) {
        xSemaphoreGive(s_sq.lock);
        if (out_id != id) {
            strlcpy(out_id, id, SQ_ID_MAX_LEN);
        }
        return storage_queue_update_meta(id, meta_json);
    }

    char new_id[SQ_ID_MAX_LEN];
    strlcpy(new_id, id, sizeof(new_id));
    char *mark = strstr(new_id, "_n_");
    if (!mark) {
        xSemaphoreGive(s_sq.lock);
        return ESP_ERR_INVALID_ARG;
    }
    mark[1] = 'h';

    /* Orden seguro ante cortes: .json nuevo completo → .jpg renombrado → .json viejo borrado.
     * Un corte en el medio deja un .json huérfano que rebuild_index limpia. */
    char tmp[PATH_MAX_LEN], new_json[PATH_MAX_LEN], old_jpg[PATH_MAX_LEN], new_jpg[PATH_MAX_LEN],
        old_json[PATH_MAX_LEN];
    path_of(tmp, new_id, ".jtm");
    path_of(new_json, new_id, ".json");
    path_of(old_jpg, id, ".jpg");
    path_of(new_jpg, new_id, ".jpg");
    path_of(old_json, id, ".json");
    esp_err_t err = write_file(tmp, meta_json, strlen(meta_json));
    if (err == ESP_OK && rename(tmp, new_json) != 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && rename(old_jpg, new_jpg) != 0) {
        unlink(new_json);
        err = ESP_FAIL;
    }
    if (err != ESP_OK) {
        unlink(tmp);
        xSemaphoreGive(s_sq.lock);
        ESP_LOGE(TAG, "No se pudo promover %s (errno %d)", id, errno);
        return err;
    }
    unlink(old_json);
    strlcpy(s_sq.idx[i].id, new_id, sizeof(s_sq.idx[i].id));
    s_sq.idx[i].high = true;
    xSemaphoreGive(s_sq.lock);

    ESP_LOGI(TAG, "%s → %s (prioridad alta)", id, new_id);
    strlcpy(out_id, new_id, SQ_ID_MAX_LEN);
    return ESP_OK;
}

esp_err_t storage_queue_peek(sq_entry_t *out)
{
    if (!s_sq.idx) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_sq.lock, portMAX_DELAY);
    int best = -1;
    for (size_t i = 0; i < s_sq.n; i++) {
        const sq_entry_t *e = &s_sq.idx[i];
        if (e->in_progress) {
            continue;
        }
        if (best < 0 || (e->high && !s_sq.idx[best].high) ||
            (e->high == s_sq.idx[best].high && e->ts < s_sq.idx[best].ts)) {
            best = (int)i;
        }
    }
    if (best >= 0) {
        *out = s_sq.idx[best];
    }
    xSemaphoreGive(s_sq.lock);
    return best >= 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t read_all(const char *path, uint8_t **buf, size_t *len, bool nul)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return ESP_FAIL;
    }
    uint8_t *b = heap_caps_malloc((size_t)sz + (nul ? 1 : 0), MALLOC_CAP_SPIRAM);
    if (!b) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t r = fread(b, 1, (size_t)sz, f);
    fclose(f);
    if (r != (size_t)sz) {
        free(b);
        return ESP_FAIL;
    }
    if (nul) {
        b[sz] = '\0';
    }
    *buf = b;
    if (len) {
        *len = (size_t)sz;
    }
    return ESP_OK;
}

esp_err_t storage_queue_read(const char *id, uint8_t **jpeg, size_t *jpeg_len, char **meta)
{
    if (!s_sq.idx || !id || !jpeg || !jpeg_len) {
        return ESP_ERR_INVALID_ARG;
    }
    char p[PATH_MAX_LEN];
    xSemaphoreTake(s_sq.lock, portMAX_DELAY);
    path_of(p, id, ".jpg");
    esp_err_t err = read_all(p, jpeg, jpeg_len, false);
    if (err == ESP_OK && meta) {
        path_of(p, id, ".json");
        err = read_all(p, (uint8_t **)meta, NULL, true);
        if (err != ESP_OK) {
            free(*jpeg);
            *jpeg = NULL;
        }
    }
    xSemaphoreGive(s_sq.lock);
    return err;
}

esp_err_t storage_queue_remove(const char *id)
{
    if (!s_sq.idx) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_sq.lock, portMAX_DELAY);
    int i = idx_find(id);
    if (i >= 0) {
        delete_files(id);
        idx_remove_at((size_t)i);
    }
    xSemaphoreGive(s_sq.lock);
    return i >= 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

size_t storage_queue_count(void)
{
    if (!s_sq.idx) {
        return 0;
    }
    xSemaphoreTake(s_sq.lock, portMAX_DELAY);
    size_t n = s_sq.n;
    xSemaphoreGive(s_sq.lock);
    return n;
}

size_t storage_queue_list_recent(sq_entry_t *out, size_t max)
{
    if (!s_sq.idx || !out) {
        return 0;
    }
    xSemaphoreTake(s_sq.lock, portMAX_DELAY);
    /* El índice está en orden de llegada: las más recientes están al final. */
    size_t k = 0;
    for (size_t i = s_sq.n; i > 0 && k < max; i--) {
        out[k++] = s_sq.idx[i - 1];
    }
    xSemaphoreGive(s_sq.lock);
    return k;
}

bool storage_queue_check_full_transition(int used_pct)
{
    if (used_pct < 0) {
        return false;
    }
    if (!s_sq.full_latched && used_pct >= CONFIG_MG_SQ_FULL_EVENT_USED_PCT) {
        s_sq.full_latched = true;
        return true;
    }
    if (s_sq.full_latched && used_pct < CONFIG_MG_SQ_FULL_EVENT_USED_PCT - 5) {
        s_sq.full_latched = false;  // histéresis: rearma el evento
    }
    return false;
}
