#!/usr/bin/env python3
"""
tools/listener.py — Smart Agriculture IoT serial listener

Reads DATA:{json} lines from the gateway's USB serial port and prints
parsed sensor readings to the console.

Usage:
    python3 tools/listener.py --port /dev/cu.usbmodem101
    python3 tools/listener.py --port /dev/ttyUSB0 --baud 115200
    python3 tools/listener.py --list

Requirements:
    pip install pyserial
"""

import argparse
import json
import sys
import time
from datetime import datetime

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not installed. Run: pip install pyserial")
    sys.exit(1)


def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found.")
        return
    print("Available serial ports:")
    for p in ports:
        print(f"  {p.device}  — {p.description}")


def format_flags(flags: dict) -> str:
    active = [k.upper() for k, v in flags.items() if v]
    return " [" + ", ".join(active) + "]" if active else ""


def print_packet(data: dict):
    ts = datetime.now().strftime("%H:%M:%S")
    flags_str = format_flags(data.get("flags", {}))
    print(
        f"[{ts}] "
        f"Node {data['node_id']:>2}  "
        f"soil={data['soil_moisture']:>6.2f}%  "
        f"temp={data['air_temp_c']:>5.1f}C  "
        f"hum={data['air_humidity']:>3}%  "
        f"bat={data['battery_mv']:>4}mV({data['battery_pct']:>3}%)  "
        f"rssi={data['rssi_dbm']:>4}dBm  "
        f"hops={data['hop_count']}  "
        f"agg={data['agg_count']}"
        f"{flags_str}"
    )


def run(port: str, baud: int, verbose: bool):
    print(f"Connecting to {port} @ {baud} baud...")
    try:
        ser = serial.Serial(port, baud, timeout=2)
    except serial.SerialException as e:
        print(f"ERROR: {e}")
        print("Tip: check available ports with --list")
        sys.exit(1)

    print("Listening for DATA: packets. Press Ctrl+C to stop.\n")
    print(
        f"{'Time':>10}  {'Node':>4}  "
        f"{'Soil%':>7}  {'Temp':>6}  {'Hum':>4}  "
        f"{'Battery':>14}  {'RSSI':>8}  {'Hops':>4}  {'Agg':>3}  Flags"
    )
    print("-" * 95)

    try:
        while True:
            try:
                line = ser.readline().decode("utf-8", errors="replace").strip()
            except serial.SerialException:
                print("\nSerial connection lost. Retrying in 2s...")
                time.sleep(2)
                try:
                    ser.close()
                    ser.open()
                except Exception:
                    pass
                continue

            if not line:
                continue

            if line.startswith("DATA:"):
                json_str = line[5:]
                try:
                    data = json.loads(json_str)
                    print_packet(data)
                    if verbose:
                        print(f"  RAW: {json_str}")
                except json.JSONDecodeError as e:
                    print(f"  [JSON parse error: {e}]  raw: {json_str[:80]}")
            else:
                # Gateway ESP_LOG lines — shown only in verbose mode
                if verbose:
                    print(f"  LOG: {line}")

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        ser.close()


def main():
    parser = argparse.ArgumentParser(
        description="Smart Agriculture IoT — Gateway Serial Listener"
    )
    parser.add_argument("--port", "-p", help="Serial port (e.g. /dev/cu.usbmodem101)")
    parser.add_argument("--baud", "-b", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("--list", "-l", action="store_true",
                        help="List available serial ports and exit")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Also print raw JSON and ESP_LOG lines")
    args = parser.parse_args()

    if args.list:
        list_ports()
        sys.exit(0)

    if not args.port:
        print("ERROR: --port is required. Use --list to see available ports.")
        parser.print_help()
        sys.exit(1)

    run(args.port, args.baud, args.verbose)


if __name__ == "__main__":
    main()
