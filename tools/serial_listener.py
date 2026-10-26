#!/usr/bin/env python3
"""
Smart Agriculture IoT — Serial Listener
========================================
Reads DATA:{...} lines from the gateway over USB-serial and prints a live
table to terminal.  Works as a drop-in fallback when Wi-Fi / MQTT is absent.

Prerequisites:
    pip install pyserial

Run:
    python3 serial_listener.py --port /dev/cu.usbserial-0001
    python3 serial_listener.py --port /dev/ttyUSB0 --baud 115200
    python3 serial_listener.py                        # auto-detect port
    python3 serial_listener.py --verbose              # print full JSON too

The gateway firmware prefixes every JSON payload with "DATA:" so this script
can ignore ESP_LOG lines without any extra filtering logic.
"""

import argparse
import glob
import json
import signal
import sys
from datetime import datetime

try:
    import serial
except ImportError:
    print("ERROR: pyserial not installed.  Run: pip install pyserial")
    sys.exit(1)

# ── ANSI colours ──────────────────────────────────────────────────────────────
GREEN  = "\033[92m"
YELLOW = "\033[93m"
RED    = "\033[91m"
CYAN   = "\033[96m"
RESET  = "\033[0m"
BOLD   = "\033[1m"

def battery_color(pct):
    if pct > 50: return GREEN
    if pct > 20: return YELLOW
    return RED

def flag_str(flags):
    parts = []
    if flags.get("frost"):     parts.append(f"{CYAN}❄ FROST{RESET}")
    if flags.get("brownout"):  parts.append(f"{RED}⚡ BROWNOUT{RESET}")
    if flags.get("node_skip"): parts.append(f"{YELLOW}↷ SKIP{RESET}")
    return "  ".join(parts) if parts else "—"

# ── Port auto-detection ───────────────────────────────────────────────────────
def find_serial_ports():
    patterns = [
        "/dev/cu.usbserial*",
        "/dev/cu.SLAB_USBtoUART*",
        "/dev/cu.wchusbserial*",
        "/dev/cu.usbmodem*",
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
    ]
    ports = []
    for p in patterns:
        ports.extend(glob.glob(p))
    return sorted(ports)

def resolve_port(arg_port):
    if arg_port:
        return arg_port

    ports = find_serial_ports()
    if not ports:
        print(f"{RED}✗ No serial ports found.{RESET}")
        print("  Plug in the gateway and try again, or pass --port explicitly.")
        sys.exit(1)

    if len(ports) == 1:
        print(f"{GREEN}Auto-detected port: {ports[0]}{RESET}")
        return ports[0]

    print("Multiple serial ports found:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p}")
    try:
        idx = int(input("Select port number: ").strip())
        return ports[idx]
    except (ValueError, IndexError):
        print(f"{RED}Invalid selection.{RESET}")
        sys.exit(1)

# ── Print helpers ─────────────────────────────────────────────────────────────
nodes = {}   # node_id → last data dict

def print_oneliner(data, verbose):
    node_id = data.get("node_id", "?")
    nodes[node_id] = data
    ts = datetime.now().strftime("%H:%M:%S")

    if verbose:
        print(f"[{ts}] Node {node_id}")
        print(f"       {json.dumps(data, indent=2)}")
        return

    pct   = data.get("battery_pct", 0)
    bc    = battery_color(pct)
    flags = data.get("flags", {})
    alert = ""
    if flags.get("frost"):     alert += f" {CYAN}❄FROST{RESET}"
    if flags.get("brownout"):  alert += f" {RED}⚡BROWNOUT{RESET}"
    if flags.get("node_skip"): alert += f" {YELLOW}↷SKIP{RESET}"

    print(
        f"[{ts}] Node {node_id:>2}  "
        f"soil={data.get('soil_moisture', 0):.0f}  "
        f"temp={data.get('air_temp_c', 0):.1f}°C  "
        f"hum={data.get('air_humidity', 0)}%  "
        f"bat={bc}{pct}%{RESET}({data.get('battery_mv', 0)}mV)  "
        f"rssi={data.get('rssi_dbm', 0)}dBm  "
        f"hops={data.get('hop_count', 0)}  "
        f"agg={data.get('agg_count', 0)}"
        f"{alert}"
    )

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Smart Agriculture IoT — Serial Listener"
    )
    parser.add_argument("--port",    default=None,  help="Serial port (e.g. /dev/cu.usbserial-0001)")
    parser.add_argument("--baud",    type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--verbose", action="store_true", help="Print full JSON payload")
    args = parser.parse_args()

    port = resolve_port(args.port)

    print(f"\n{BOLD}Smart Agriculture IoT — Serial Listener{RESET}")
    print(f"Port: {port}  Baud: {args.baud}")
    print(f"Waiting for DATA lines...  (Ctrl+C to quit)\n")

    try:
        ser = serial.Serial(port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"{RED}✗ Cannot open {port}: {e}{RESET}")
        available = find_serial_ports()
        if available:
            print("Available ports:")
            for p in available:
                print(f"  {p}")
        sys.exit(1)

    def handle_sigint(sig, frame):
        print(f"\n{YELLOW}Shutting down...{RESET}")
        ser.close()
        sys.exit(0)
    signal.signal(signal.SIGINT, handle_sigint)

    while True:
        try:
            raw = ser.readline()
        except serial.SerialException as e:
            print(f"{RED}Serial error: {e}{RESET}")
            break

        if not raw:
            continue

        try:
            line = raw.decode("utf-8", errors="replace").rstrip()
        except Exception:
            continue

        if not line.startswith("DATA:"):
            continue

        json_str = line[5:]   # strip "DATA:" prefix
        try:
            data = json.loads(json_str)
        except json.JSONDecodeError as e:
            print(f"{YELLOW}JSON parse error: {e} — raw: {json_str}{RESET}")
            continue

        print_oneliner(data, args.verbose)

    ser.close()

if __name__ == "__main__":
    main()
