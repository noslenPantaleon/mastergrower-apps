/**
 * @file ui_thumbs.c
 * @brief Ver ui_thumbs.h.
 */
#include "ui_thumbs.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "state_store.h"
#include "theme.h"

typedef struct {
    char node_id[SS_ID_LEN];
    lv_image_dsc_t dsc;
    uint16_t *pixels;
    thumb_info_t info;
} entry_t;

static entry_t s_e[SS_MAX_NODES];

static entry_t *find(const char *node_id, bool create)
{
    for (int i = 0; i < SS_MAX_NODES; i++) {
        if (strcmp(s_e[i].node_id, node_id) == 0) {
            return &s_e[i];
        }
    }
    for (int i = 0; create && i < SS_MAX_NODES; i++) {
        if (!s_e[i].node_id[0]) {
            strlcpy(s_e[i].node_id, node_id, sizeof(s_e[i].node_id));
            return &s_e[i];
        }
    }
    return NULL;
}

void ui_thumbs_sync(void)
{
    static ss_node_t nodes[SS_MAX_NODES];
    int n = state_store_nodes(nodes, SS_MAX_NODES);
    for (int i = 0; i < n; i++) {
        thumb_t t;
        if (nodes[i].kind != MG_NODE_CAM || !thumb_service_take(nodes[i].node_id, &t)) {
            continue;
        }
        entry_t *e = find(nodes[i].node_id, true);
        if (!e) {
            heap_caps_free(t.pixels);
            continue;
        }
        uint16_t *old = e->pixels;
        e->pixels = t.pixels;
        e->info = t.info;
        e->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        e->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        e->dsc.header.w = t.w;
        e->dsc.header.h = t.h;
        e->dsc.header.stride = t.w * 2;
        e->dsc.data_size = (uint32_t)t.w * t.h * 2;
        e->dsc.data = (const uint8_t *)t.pixels;
        /* Mismo descriptor, píxeles nuevos: que LVGL no use lo que tenga en caché. Estamos en la
         * tarea de LVGL y fuera del dibujo, así que el búfer viejo ya no se lee. */
        lv_image_cache_drop(&e->dsc);
        heap_caps_free(old);
    }
}

const lv_image_dsc_t *ui_thumb(const char *node_id, thumb_info_t *info)
{
    entry_t *e = find(node_id, false);
    if (!e || !e->pixels) {
        return NULL;
    }
    if (info) {
        *info = e->info;
    }
    return &e->dsc;
}

lv_obj_t *ui_thumb_image(lv_obj_t *parent)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_set_size(img, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_COVER);
    lv_obj_set_style_radius(img, 8, 0);
    lv_obj_set_style_clip_corner(img, true, 0);
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    return img;
}

void ui_thumb_bind(lv_obj_t *img, const char *node_id)
{
    const lv_image_dsc_t *dsc = ui_thumb(node_id, NULL);
    if (!dsc) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_image_set_src(img, dsc);  // aunque sea el mismo descriptor: fuerza a redibujar
    lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
}

void ui_analysis_chip(const thumb_info_t *info, const char **text, uint32_t *bg, uint32_t *fg)
{
    if (!info || !info->has_analysis) {
        *text = "Sin análisis", *bg = TH_OFF_BG, *fg = TH_OFF_FG;
    } else if (info->dark_scene) {
        *text = "A oscuras", *bg = TH_OFF_BG, *fg = TH_OFF_FG;
    } else if (info->suspicious) {
        *text = "Revisar planta", *bg = TH_WARN_BG, *fg = TH_WARN_FG;
    } else if (info->low_plant) {
        *text = "Poca planta a la vista", *bg = TH_OFF_BG, *fg = TH_OFF_FG;
    } else {
        *text = "Sin anomalías", *bg = TH_OK_BG, *fg = TH_OK_FG;
    }
}
