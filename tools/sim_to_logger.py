#!/usr/bin/env python3
# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
"""Sends a simulator CSV (gas_sim --cook) as telemetry batches to the logger,
exactly like the firmware does (src/Telemetry.cpp). For testing the logger and
the analysis without a real cook.

    python3 tools/sim_to_logger.py cook.csv http://192.168.178.250 [--sn simulator]
"""
import argparse
import csv
import json
import time
import urllib.request

COLS = ["ms", "pm", "type", "raw", "temp", "set", "out", "servo_us", "p", "i", "d", "esum", "flags",
        "at_state", "at_phase", "at_cycle"]


def flags(r):
    return ((1 if r["jump"] == "1" else 0) | (2 if r["lid"] == "1" else 0) | (4 if r["flame_susp"] == "1" else 0) |
            (8 if r["flame_out"] == "1" else 0) | (16 if r["at_state"] == "1" else 0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("logger", help="e.g. http://192.168.178.250")
    ap.add_argument("--sn", default="simulator0001")
    ap.add_argument("--start", type=float, default=None, help="epoch of t=0 (default: now - duration)")
    ap.add_argument("--batch", type=int, default=10)
    args = ap.parse_args()

    with open(args.csv, newline="") as f:
        rows = list(csv.DictReader(f))
    duration = float(rows[-1]["t_s"])
    start = args.start if args.start is not None else time.time() - duration - 60

    sent = 0
    for k in range(0, len(rows), args.batch):
        chunk = rows[k:k + args.batch]
        last_ms = int(float(chunk[-1]["t_s"]) * 1000)
        batch = {
            "sn": args.sn, "fw": "simulator", "now": int(start + last_ms / 1000.0), "now_ms": last_ms,
            "heap": 0, "loop_us_max": 0, "dropped": 0, "failed": 0, "interval": 1, "cols": COLS,
            "rows": [[int(float(r["t_s"]) * 1000), 0, int(r["type"]), float(r["temp"]), float(r["temp"]),
                      float(r["set"]), float(r["output"]), int(r["servo_us"]), float(r["p"]), float(r["i"]),
                      float(r["d"]), float(r["esum"]), flags(r), int(r["at_state"]), int(r["at_phase"]),
                      int(r["at_cycle"])] for r in chunk],
        }
        req = urllib.request.Request(args.logger.rstrip("/") + "/telemetry", data=json.dumps(batch).encode(),
                                     headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=10) as resp:
            resp.read()
        sent += len(chunk)
    print(f"sent {sent} rows to {args.logger}")


if __name__ == "__main__":
    main()
