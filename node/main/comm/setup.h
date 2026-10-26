#pragma once
#include "packet.h"
#include "neighbor_table.h"

// Run the setup phase — blocks until both nodes confirm end-to-end connectivity.
// Internally handles espnow_init / espnow_deinit on each loop iteration.
// Sets g_setup_complete = true and calls espnow_deinit() before returning.
// my_pkt must be fully built (packet_build already called).
void setup_phase_run(SensorPacket *my_pkt, const NeighborTable *ntbl);
