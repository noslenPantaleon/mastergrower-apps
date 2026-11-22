/**
 * @file lv_mem_psram.c
 * @brief Asignador de memoria de LVGL en PSRAM (CONFIG_LV_USE_CUSTOM_MALLOC).
 *
 * Con el de la libc, todo pedido menor a CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (16 KB) iba a la RAM
 * interna: cada objeto de la interfaz y la caché de glifos de TinyTTF la agotaban en segundos y
 * el WiFi/lwIP se quedaban sin memoria (panic en D5). La RAM interna queda para DMA, pilas, WiFi
 * y el panel; LVGL accede a la PSRAM a través de la caché, que alcanza para la interfaz.
 */
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "src/stdlib/lv_mem.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#define CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    return heap_caps_malloc(size, CAPS);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    return heap_caps_realloc(p, new_size, CAPS);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, CAPS);
    lv_memzero(mon, sizeof(*mon));
    mon->total_size = info.total_free_bytes + info.total_allocated_bytes;
    mon->free_size = info.total_free_bytes;
    mon->free_biggest_size = info.largest_free_block;
    mon->used_cnt = info.allocated_blocks;
    mon->free_cnt = info.free_blocks;
    mon->max_used = mon->total_size - info.minimum_free_bytes;
    mon->used_pct = mon->total_size ? 100 - (100U * mon->free_size) / mon->total_size : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(CAPS, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

#endif
