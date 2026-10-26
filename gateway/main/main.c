#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config/gw_config.h"
#include "packet.h"

static const char *TAG = "GW";

// ─── Watchdog: reboot if silent for WATCHDOG_TIMEOUT_MIN minutes ─────────────
static TimerHandle_t s_watchdog_timer = NULL;

static void watchdog_cb(TimerHandle_t t)
{
    ESP_LOGE(TAG, "Watchdog: no packet in %d minutes — rebooting", WATCHDOG_TIMEOUT_MIN);
    esp_restart();
}

static void watchdog_pet(void)
{
    if (s_watchdog_timer) xTimerReset(s_watchdog_timer, 0);
}

// ─── CSV serial output ────────────────────────────────────────────────────────
static void publish_serial(const SensorPacket *pkt)
{
    // soil_temp_c10 == 0x7FFF → V1 node or sensor absent, print as "N"
    char soil_temp_str[8];
    if (pkt->soil_temp_c10 == 0x7FFF) {
        soil_temp_str[0] = 'N'; soil_temp_str[1] = '\0';
    } else {
        snprintf(soil_temp_str, sizeof(soil_temp_str), "%.1f", pkt->soil_temp_c10 / 10.0f);
    }

    printf("D:%u,%.1f,%.1f,%u,%u,%d,%u,%u,%s,%lu,%u\n",
           pkt->node_id,
           pkt->soil_moisture / 100.0f,
           pkt->air_temp_c10  / 10.0f,
           pkt->air_humidity,
           pkt->battery_mv,
           pkt->rssi_dbm,
           pkt->hop_count,
           pkt->agg_count,
           soil_temp_str,
           (unsigned long)pkt->pressure_pa,
           pkt->flags);
    fflush(stdout);
}

// ─── ESP-NOW receive callback ─────────────────────────────────────────────────
static void recv_cb(const esp_now_recv_info_t *info,
                    const uint8_t *data, int len)
{
    int8_t rx_rssi = (int8_t)info->rx_ctrl->rssi;

    if (len < 1) return;

    switch (data[0]) {
        case PKT_TYPE_PING: {
            if (len < (int)sizeof(PingPacket)) return;
            const PingPacket *ping = (const PingPacket *)data;
            AckPacket ack = {
                .type         = PKT_TYPE_ACK,
                .node_id      = 0,
                .sender_ts_us = ping->timestamp_us,
                .local_ts_us  = esp_timer_get_time(),
                .rssi_dbm     = rx_rssi,
            };
            if (!esp_now_is_peer_exist(info->src_addr)) {
                esp_now_peer_info_t peer = {
                    .channel = ESPNOW_CHANNEL,
                    .encrypt = false,
                };
                memcpy(peer.peer_addr, info->src_addr, 6);
                esp_now_add_peer(&peer);
            }
            esp_now_send(info->src_addr, (const uint8_t *)&ack, sizeof(AckPacket));
            ESP_LOGD(TAG, "PING from " PRI_MAC " → ACK sent", MAC_ARGS(info->src_addr));
            break;
        }

        case PKT_TYPE_DATA: {
            if (len < (int)sizeof(SensorPacket)) {
                ESP_LOGW(TAG, "DATA too short: %d bytes (expected %zu)", len, sizeof(SensorPacket));
                return;
            }
            const SensorPacket *pkt = (const SensorPacket *)data;
            watchdog_pet();
            publish_serial(pkt);
            break;
        }

        default:
            ESP_LOGW(TAG, "Unknown packet type 0x%02X from " PRI_MAC, data[0], MAC_ARGS(info->src_addr));
            break;
    }
}

// ─── app_main ─────────────────────────────────────────────────────────────────
void app_main(void)
{
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔═══════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  Smart Agriculture IoT — GATEWAY (id=0)   ║");
    ESP_LOGI(TAG, "║  Serial-only mode  channel=%d              ║", ESPNOW_CHANNEL);
    ESP_LOGI(TAG, "╚═══════════════════════════════════════════╝");

    // Init Wi-Fi in STA mode (required by ESP-NOW stack, no AP connection)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // Init ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));

    // Print gateway MAC for filling in node_config.h
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "Gateway MAC: " PRI_MAC "  (use this as GATEWAY_MAC in node_config.h)",
             MAC_ARGS(mac));
    ESP_LOGI(TAG, "ESP-NOW ready on channel %d", ESPNOW_CHANNEL);

    // Watchdog timer
    s_watchdog_timer = xTimerCreate("watchdog",
                                    pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MIN * 60 * 1000),
                                    pdFALSE, NULL, watchdog_cb);
    if (s_watchdog_timer) xTimerStart(s_watchdog_timer, 0);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "✓ Gateway operational — waiting for node packets...");
    ESP_LOGI(TAG, "  Serial output format: D:<node_id>,<soil>,<temp>,<hum>,<batt_mv>,<rssi>,<hop>,<agg>,<soil_temp|N>,<pressure_pa>,<flags>");
    ESP_LOGI(TAG, "  Listener: python3 tools/listener.py --port /dev/cu.XXXX");
    ESP_LOGI(TAG, "");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGD(TAG, "Heartbeat — gateway alive");
    }
}
