#include "neighbor_table.h"
#include "node_config.h"
#include "packet.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NTBL";

void neighbor_table_get(NeighborTable *tbl)
{
    static const uint8_t node_a_mac[] = NODE_A_MAC;
    static const uint8_t node_b_mac[] = NODE_B_MAC;
    static const uint8_t gw_mac[]     = GATEWAY_MAC;

    memset(tbl, 0, sizeof(NeighborTable));

#if NODE_ID == 1
    // ── Node A: Leaf ──────────────────────────────────────────────────────────
    // No children. Parent = Node B. Skip = Gateway (bypass B if B is down).
    memcpy(tbl->parent_mac, node_b_mac, 6);
    memcpy(tbl->skip_mac,   gw_mac,     6);

#elif NODE_ID == 2
    // ── Node B: Relay ─────────────────────────────────────────────────────────
    // Child = Node A. Parent = Gateway. Skip = Gateway (only one hop away anyway).
    memcpy(tbl->child_mac,  node_a_mac, 6);
    memcpy(tbl->parent_mac, gw_mac,     6);
    memcpy(tbl->skip_mac,   gw_mac,     6);

#else
    #error "NODE_ID must be 1 (leaf) or 2 (relay) for node firmware. Use gateway/ for id=0."
#endif

    ESP_LOGI(TAG, "Neighbor table for Node %d:", NODE_ID);
    if (!mac_is_zero(tbl->child_mac)) {
        ESP_LOGI(TAG, "  child  : " PRI_MAC, MAC_ARGS(tbl->child_mac));
    } else {
        ESP_LOGI(TAG, "  child  : (none — leaf node)");
    }
    ESP_LOGI(TAG, "  parent : " PRI_MAC, MAC_ARGS(tbl->parent_mac));
    ESP_LOGI(TAG, "  skip   : " PRI_MAC, MAC_ARGS(tbl->skip_mac));

    // Warn if MACs are still all-FF (placeholder — user hasn't filled them in)
    bool parent_unset = true;
    for (int i = 0; i < 6; i++) {
        if (tbl->parent_mac[i] != 0xFF) { parent_unset = false; break; }
    }
    if (parent_unset) {
        ESP_LOGW(TAG, "⚠ parent_mac is still 0xFF:FF:FF:FF:FF:FF — update node_config.h!");
    }
}
