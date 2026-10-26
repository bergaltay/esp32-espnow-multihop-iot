# Energy-Efficient Linear Multi-Hop IoT Network (ESP32-C6 / ESP-NOW)

<!-- DRAFT — not public yet. Items marked TODO must be resolved before switching the repo to public. -->

A low-power sensor chain for precision agriculture: leaf and relay nodes deep-sleep most of the time, wake on a shared schedule, and forward readings hop-by-hop over ESP-NOW to an always-on gateway. Built as an individual research project (TED University, CMPE490 – Introduction to Research in Computer Engineering, Spring 2026).

**Status:** Phase 1 (research prototype) completed June 2026 — indoor, USB-powered. Phase 2 (battery operation, power profiling, field tests) planned. *Last updated: 10.2026*

---

## Problem

Field sensors in agriculture are often out of Wi-Fi range and must eventually run on batteries. A Wi-Fi/MQTT-per-node design keeps the radio on too long. This project explores how far a low-cost ESP32 chain can go using a connectionless protocol (ESP-NOW) plus coordinated sleep, without an external RTC or extra radio hardware.

## Architecture

Three physical devices: two sensor nodes (leaf, relay) and one gateway.

```mermaid
flowchart LR
    A["Node A — Leaf<br/>sensors · deep sleep"] -- ESP-NOW --> B["Node B — Relay<br/>sensors · aggregation · deep sleep"]
    B -- ESP-NOW --> G["Gateway<br/>always-on"]
    G -- USB serial / JSON --> PC["PC<br/>Python log parser"]
```

## Key features

| Feature | Approach |
|---|---|
| Time synchronization | Cristian's algorithm + EWMA smoothing (α = 0.2); no external RTC |
| Setup phase | Handshake to align nodes at first power-on |
| Adaptive TX power | RSSI-driven, 11.0–19.5 dBm, ±5 dB hysteresis |
| In-network aggregation | Relay merges its own and the leaf's readings into one packet |
| Self-healing | Failure counter persisted in RTC memory across deep sleep; skip-route after 3 failures |
| Sensor power gating | P-MOSFET switches analog sensors off during sleep |
| FEC | 5 bytes reserved in packet; Hamming(72,64) **not implemented** (Phase 2) |

## Hardware

| Item | Detail |
|---|---|
| MCU | ESP32-C6 Super Mini (RISC-V, 160 MHz) — all three devices |
| Sensors (V2) | DS18B20 (temperature), BMP280 (pressure/temperature), SW-520D (tilt) |
| Sensors (V1) | DHT22, resistive soil moisture |
| Power (Phase 1) | USB-powered during all tests — **no battery yet** |
| Power (Phase 2) | Single-cell lithium battery + voltage-divider monitoring (planned) |
| Gateway link | USB serial, 115200 baud, JSON lines |

<!-- TODO: add prototype photo → docs/hardware/prototype.jpg -->

## Results (Phase 1)

**Test setup:** indoor, ~1.5 m node spacing, 15 s sleep interval, ~30 min run, gateway serial logs parsed offline in Python.

### Measured

| Metric | Result | Target | Conditions |
|---|---|---|---|
| Packet delivery ratio | 99.6 % (273 / 274) | > 95 % | 1 × ~30 min run |
| Clock sync accuracy | ±2.6 ms max deviation | ±5 ms | 27 sync cycles |
| TX power at minimum level | 97.4 % of cycles (266 / 273) at 11.0 dBm | — | 2 power changes during a fade event |
| TX current @ 11 dBm | ~125 mA | — | USB inline tester |

### Calculated / not measured

The following are **model-based estimates**, not measurements: per-cycle energy (~1.19 J relay, assuming 3.7 V supply), TPC energy saving (~2.6 % of active-phase energy), aggregation radio-time saving, setup convergence time, self-healing recovery time. Battery life and end-to-end latency were **not measured** — this is the main goal of Phase 2.

## Engineering note: ADC vs. ESP-NOW init order

On ESP32-C6 (ESP-IDF v5.3.2), ADC readings became invalid when taken after `esp_now_init()`. Reading all ADC channels **before** initializing ESP-NOW resolved it. <!-- TODO: check Espressif docs/issues; keep wording as "observed" unless confirmed undocumented -->

## Limitations

- Indoor tests only, short distances
- USB-powered: no battery drain or lifetime data yet
- Self-healing and setup timing verified in logic, not physically fault-injected
- One BMP280 unit gave invalid readings due to a weak I²C pull-up
- FEC reserved but not implemented

## Roadmap — Phase 2

- [ ] Battery integration and battery-voltage monitoring
- [ ] Power profiling with a proper current meter (baseline vs. TPC / aggregation / sleep)
- [ ] Measured battery lifetime estimate
- [ ] Longer chains and outdoor field tests
- [ ] Physical fault-injection test for self-healing
- [ ] Hamming(72,64) FEC

## Repository layout

<!-- TODO: fill after clean code import -->
```
components/ag_common/   shared packet definitions
node/                   leaf + relay firmware (same binary, NODE_ID selects role)
gateway/                gateway firmware (serial output)
tools/                  Python log parser
docs/                   paper, measurement data, hardware
```

## Build

Requires ESP-IDF v5.3.2.

```bash
cd gateway && idf.py set-target esp32c6 && idf.py build flash monitor
cd node    && idf.py set-target esp32c6 && idf.py build flash monitor   # set NODE_ID in node_config.h
```

<!-- TODO: power-on order + MAC table steps from old README (corrected for serial gateway) -->

## Paper

*Energy-Efficient Linear Multi-Hop IoT Network for Precision Agriculture* — S. B. Altay, supervised by Prof. Dr. Gökçe Nur Yılmaz, TED University, 2026.
<!-- TODO: link after v1.1 corrections + supervisor approval (arXiv or Zenodo) -->

---

### Türkçe özet

ESP32-C6 ve ESP-NOW üzerinde, düşük güç tüketimi için tasarlanmış doğrusal çok-atlamalı bir tarım sensör ağı (iki sensör düğümü + ağ geçidi). Düğümler ortak bir zaman çizelgesiyle uyanıyor; veriler aracı düğümde birleştirilip ağ geçidine iletiliyor. Faz 1'de (iç mekân, USB beslemeli) %99,6 paket iletim oranı ve ±2,6 ms saat senkronizasyonu ölçüldü. Enerji kazanımları hesaplanmış olup ölçülmemiştir; pil entegrasyonu ve güç ölçümleri Faz 2'nin konusudur.

## License

Code: [MIT](LICENSE). The paper is not covered by this license; all rights reserved by the author.
