#include "packet.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

// Compile-time size guard — will fail build if struct layout drifts
_Static_assert(sizeof(SensorPacket) == 40, "SensorPacket must be exactly 40 bytes");
_Static_assert(sizeof(PingPacket)   == 10, "PingPacket must be exactly 10 bytes");
_Static_assert(sizeof(AckPacket)    == 19, "AckPacket must be exactly 19 bytes");
_Static_assert(sizeof(SetupPacket)  ==  3, "SetupPacket must be exactly 3 bytes");

static const char *TAG = "PACKET";

void packet_build(SensorPacket *pkt,
                  uint8_t  node_id,
                  uint16_t soil_moisture,
                  int16_t  air_temp_c10,
                  uint8_t  air_humidity,
                  uint16_t battery_mv,
                  int8_t   rssi_dbm,
                  uint8_t  hop_count,
                  uint8_t  flags)
{
    memset(pkt, 0, sizeof(SensorPacket));
    pkt->type          = PKT_TYPE_DATA;
    pkt->node_id       = node_id;
    pkt->soil_moisture = soil_moisture;
    pkt->air_temp_c10  = air_temp_c10;
    pkt->air_humidity  = air_humidity;
    pkt->soil_temp_c10 = SOIL_TEMP_INVALID;   // V2 placeholder
    pkt->pressure_pa   = PRESSURE_INVALID;     // V2 placeholder
    pkt->battery_mv    = battery_mv;
    pkt->rssi_dbm      = rssi_dbm;
    pkt->hop_count     = hop_count;
    pkt->flags         = flags;
    pkt->agg_count     = 1;
    // fec_parity stays zeroed (FEC Phase 3.3 not implemented yet)
}

void packet_print(const SensorPacket *pkt)
{
    ESP_LOGI(TAG, "──────────────────────────────────");
    ESP_LOGI(TAG, "  type         : 0x%02X", pkt->type);
    ESP_LOGI(TAG, "  node_id      : %u",     pkt->node_id);
    ESP_LOGI(TAG, "  timestamp_ms : %lu",    pkt->timestamp_ms);
    ESP_LOGI(TAG, "  soil_moisture: %u",     pkt->soil_moisture);
    ESP_LOGI(TAG, "  air_temp     : %.1f°C", pkt->air_temp_c10 / 10.0f);
    ESP_LOGI(TAG, "  air_humidity : %u%%",   pkt->air_humidity);
    ESP_LOGI(TAG, "  battery_mv   : %u mV",  pkt->battery_mv);
    ESP_LOGI(TAG, "  rssi_dbm     : %d dBm", pkt->rssi_dbm);
    ESP_LOGI(TAG, "  hop_count    : %u",     pkt->hop_count);
    ESP_LOGI(TAG, "  agg_count    : %u",     pkt->agg_count);
    ESP_LOGI(TAG, "  flags        : 0x%02X%s%s%s",
             pkt->flags,
             (pkt->flags & FLAG_FROST_ALERT) ? " [FROST]"    : "",
             (pkt->flags & FLAG_BROWNOUT)    ? " [BROWNOUT]" : "",
             (pkt->flags & FLAG_NODE_SKIP)   ? " [SKIP]"     : "");
    ESP_LOGI(TAG, "──────────────────────────────────");
}

bool mac_is_zero(const uint8_t mac[6])
{
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) return false;
    }
    return true;
}
