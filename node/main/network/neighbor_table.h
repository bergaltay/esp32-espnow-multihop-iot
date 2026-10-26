#pragma once
#include <stdint.h>
#include <stdbool.h>

// MAC addresses of each neighbor role.
// child_mac: node that sends data TO this node (all-zeros if leaf).
// parent_mac: primary relay target (next node toward gateway).
// skip_mac: failover target used when parent is unreachable (self-healing).
typedef struct {
    uint8_t child_mac[6];
    uint8_t parent_mac[6];
    uint8_t skip_mac[6];
} NeighborTable;

// Populate NeighborTable based on NODE_ID and the MAC defines in node_config.h.
// Called once at startup. Logs the routing table to serial.
void neighbor_table_get(NeighborTable *tbl);
