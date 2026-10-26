#!/usr/bin/env python3
"""Summarise a gateway measurement log (MEAS_PDR / MEAS_TXPWR lines).

Usage: python3 tools/analyze_meas_log.py docs/data/2026-05-20_meas_log.txt

Reports, per node: packet delivery ratio over the observed cycle range,
gateway inter-arrival period (mean / std), cumulative timing drift
(residual of a linear fit of arrival time vs. cycle), RSSI ranges, and the
leaf->relay arrival offset at the gateway (a proxy for how well the two
nodes stay phase-aligned across deep-sleep cycles).
"""
import sys
import statistics as st


def parse(path):
    pdr, rssi = {}, {}
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) < 3:
                continue
            kind = parts[1]
            kv = dict(p.split("=", 1) for p in parts[2:])
            node = int(kv["node"])
            if kind == "MEAS_PDR":
                pdr.setdefault(node, {})[int(kv["cycle"])] = int(kv["t"])
            elif kind == "MEAS_TXPWR":
                rssi.setdefault(node, []).append((int(kv["pkt_rssi"]), int(kv["gw_rssi"])))
    return pdr, rssi


def linear_residuals(xs, ys):
    mx, my = st.mean(xs), st.mean(ys)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
    icpt = my - slope * mx
    return slope, [y - (icpt + slope * x) for x, y in zip(xs, ys)]


def main(path):
    pdr, rssi = parse(path)
    lo = min(min(c) for c in pdr.values())
    hi = max(max(c) for c in pdr.values())
    total_rx = total_exp = 0
    for node in sorted(pdr):
        cyc = pdr[node]
        expected = hi - lo + 1
        missing = [c for c in range(lo, hi + 1) if c not in cyc]
        total_rx += len(cyc)
        total_exp += expected
        keys = sorted(cyc)
        periods = [cyc[b] - cyc[a] for a, b in zip(keys, keys[1:]) if b == a + 1]
        slope, res = linear_residuals(keys, [cyc[k] for k in keys])
        pk = [p for p, _ in rssi.get(node, [])]
        gw = [g for _, g in rssi.get(node, [])]
        print(f"Node {node}: cycles {lo}-{hi}, received {len(cyc)}/{expected} "
              f"(PDR {100 * len(cyc) / expected:.1f} %), missing {missing}")
        print(f"  period at gateway: mean {st.mean(periods):.1f} ms, std {st.pstdev(periods):.1f} ms")
        print(f"  drift vs. linear fit ({slope:.1f} ms/cycle): max |residual| {max(map(abs, res)):.1f} ms")
        if pk:
            print(f"  RSSI: link {min(pk)}..{max(pk)} dBm, gateway {min(gw)}..{max(gw)} dBm")
    print(f"Overall PDR: {total_rx}/{total_exp} = {100 * total_rx / total_exp:.1f} %")
    if 1 in pdr and 2 in pdr:
        both = [c for c in pdr[1] if c in pdr[2]]
        gap = [pdr[2][c] - pdr[1][c] for c in both]
        print(f"Node1->Node2 arrival offset at gateway: mean {st.mean(gap):.1f} ms, "
              f"std {st.pstdev(gap):.2f} ms, range {min(gap)}..{max(gap)} ms (n={len(gap)})")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
