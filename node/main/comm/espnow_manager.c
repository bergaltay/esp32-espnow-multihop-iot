#include "espnow_manager.h"
#include "node_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ESPNOW";

// ─── Internal state ───────────────────────────────────────────────────────────
static SemaphoreHandle_t s_send_sem   = NULL;   // signals send callback completion
static bool              s_send_ok    = false;  // last send delivery status
static espnow_ping_cb_t  s_ping_cb   = NULL;
static espnow_ack_cb_t   s_ack_cb    = NULL;
static espnow_data_cb_t  s_data_cb   = NULL;
static espnow_setup_cb_t s_setup_cb  = NULL;

// ─── Early-arrival data buffer ────────────────────────────────────────────────
// Holds a DATA packet that arrived before relay_listen registered data_cb.
static SensorPacket s_pending_data;
static uint8_t      s_pending_data_src[6];
static int8_t       s_pending_data_rssi = 0;
static bool         s_pending_data_valid = false;

// ─── Send callback ────────────────────────────────────────────────────────────
static void send_cb(const uint8_t *mac, esp_now_send_status_t status)
{
    s_send_ok = (status == ESP_NOW_SEND_SUCCESS);
    if (s_send_sem) xSemaphoreGive(s_send_sem);
}

// ─── Receive callback — dispatches by first byte (packet type) ────────────────
static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len < 1) return;
    int8_t rssi = (int8_t)info->rx_ctrl->rssi;

    switch (data[0]) {
        case PKT_TYPE_PING:
            if (len >= (int)sizeof(PingPacket) && s_ping_cb) {
                s_ping_cb(info->src_addr, (const PingPacket *)data, rssi);
            }
            break;

        case PKT_TYPE_ACK:
            if (len >= (int)sizeof(AckPacket) && s_ack_cb) {
                s_ack_cb((const AckPacket *)data);
            }
            break;

        case PKT_TYPE_DATA:
            if (len >= (int)sizeof(SensorPacket)) {
                if (s_data_cb) {
                    s_data_cb(info->src_addr, (const SensorPacket *)data, rssi);
                } else {
                    // relay_listen not yet called — buffer for early-arrival handling
                    memcpy(&s_pending_data, data, sizeof(SensorPacket));
                    memcpy(s_pending_data_src, info->src_addr, 6);
                    s_pending_data_rssi  = rssi;
                    s_pending_data_valid = true;
                    ESP_LOGD(TAG, "DATA buffered (early arrival) from " PRI_MAC, MAC_ARGS(info->src_addr));
                }
            }
            break;

        case PKT_TYPE_SETUP:
            if (len >= (int)sizeof(SetupPacket) && s_setup_cb) {
                s_setup_cb(info->src_addr, (const SetupPacket *)data, rssi);
            }
            break;

        default:
            ESP_LOGW(TAG, "Unknown packet type 0x%02X from " PRI_MAC, data[0], MAC_ARGS(info->src_addr));
            break;
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────
esp_err_t espnow_init(void)
{
    s_send_sem = xSemaphoreCreateBinary();
    if (!s_send_sem) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));

    // Print our own MAC so user can fill in MAC table
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "Node %d MAC: " PRI_MAC, NODE_ID, MAC_ARGS(mac));
    ESP_LOGI(TAG, "ESP-NOW init OK on channel %d", ESPNOW_CHANNEL);
    return ESP_OK;
}

void espnow_deinit(void)
{
    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_event_loop_delete_default();

    if (s_send_sem) {
        vSemaphoreDelete(s_send_sem);
        s_send_sem = NULL;
    }
    s_pending_data_valid = false;
    s_data_cb  = NULL;
    s_ping_cb  = NULL;
    s_ack_cb   = NULL;
    s_setup_cb = NULL;
    ESP_LOGI(TAG, "ESP-NOW deinit OK");
}

esp_err_t espnow_add_peer(const uint8_t mac[6])
{
    if (mac_is_zero(mac)) return ESP_OK;  // unset MAC — skip
    if (esp_now_is_peer_exist(mac)) return ESP_OK;

    esp_now_peer_info_t peer = {
        .channel = ESPNOW_CHANNEL,
        .encrypt = false,
    };
    memcpy(peer.peer_addr, mac, 6);
    esp_err_t err = esp_now_add_peer(&peer);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Peer added: " PRI_MAC, MAC_ARGS(mac));
    } else {
        ESP_LOGE(TAG, "Failed to add peer " PRI_MAC ": %s", MAC_ARGS(mac), esp_err_to_name(err));
    }
    return err;
}

esp_err_t espnow_send_data(const uint8_t mac[6], const void *data, size_t len)
{
    esp_err_t err = esp_now_send(mac, (const uint8_t *)data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(err));
        return err;
    }
    // Wait for delivery callback
    if (xSemaphoreTake(s_send_sem, pdMS_TO_TICKS(SEND_TIMEOUT_MS)) == pdFALSE) {
        ESP_LOGE(TAG, "Send timeout (no callback in %d ms)", SEND_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }
    if (!s_send_ok) {
        ESP_LOGE(TAG, "Send delivery FAILED (no ACK from peer)");
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Delivery confirmed to " PRI_MAC, MAC_ARGS(mac));
    return ESP_OK;
}

void espnow_send_ack_nowait(const uint8_t mac[6], const AckPacket *ack)
{
    // Fire-and-forget — called from receive callback, cannot block
    esp_now_send(mac, (const uint8_t *)ack, sizeof(AckPacket));
}

void espnow_register_ping_cb(espnow_ping_cb_t cb)   { s_ping_cb  = cb; }
void espnow_register_ack_cb(espnow_ack_cb_t cb)     { s_ack_cb   = cb; }
void espnow_register_data_cb(espnow_data_cb_t cb)   { s_data_cb  = cb; }
void espnow_register_setup_cb(espnow_setup_cb_t cb) { s_setup_cb = cb; }

bool espnow_pop_pending_data(uint8_t src_mac[6], SensorPacket *out_pkt, int8_t *out_rssi)
{
    if (!s_pending_data_valid) return false;
    memcpy(out_pkt, &s_pending_data, sizeof(SensorPacket));
    memcpy(src_mac, s_pending_data_src, 6);
    if (out_rssi) *out_rssi = s_pending_data_rssi;
    s_pending_data_valid = false;
    ESP_LOGI(TAG, "Erken paket tampondan alindi: " PRI_MAC, MAC_ARGS(src_mac));
    return true;
}
