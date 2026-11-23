/**
 * @file dns_any.c
 * @brief DNS mínimo del portal cautivo: responde cualquier consulta A con la IP del AP
 *        (192.168.4.1), así el celular detecta "hay que iniciar sesión" y abre la página.
 */
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "mg_portal_dns";

static void dns_task(void *arg)
{
    uint32_t ip = (uint32_t)(uintptr_t)arg;  // orden de red
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "No se pudo abrir el puerto 53");
        vTaskDelete(NULL);
    }
    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf) - 16, 0, (struct sockaddr *)&from, &fl);
        if (n < 12) {
            continue;
        }
        /* Fin de la pregunta: nombre (etiquetas) + tipo + clase */
        int q = 12;
        while (q < n && buf[q]) {
            q += buf[q] + 1;
        }
        q += 5;
        if (q > n) {
            continue;
        }
        buf[2] = 0x81;  // respuesta, recursión disponible
        buf[3] = 0x80;
        buf[6] = 0;     // 1 respuesta
        buf[7] = 1;
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        const uint8_t ans[] = { 0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x04 };
        memcpy(buf + q, ans, sizeof(ans));
        memcpy(buf + q + sizeof(ans), &ip, 4);
        sendto(sock, buf, q + sizeof(ans) + 4, 0, (struct sockaddr *)&from, fl);
    }
}

void mg_portal_dns_start(uint32_t ip_net_order)
{
    xTaskCreate(dns_task, "portal_dns", 3072, (void *)(uintptr_t)ip_net_order, 4, NULL);
}
