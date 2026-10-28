#!/usr/bin/env python3
# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
"""Read-only watch of a running cook. Reads GET /data every 60 s and prints
only anomalies. Never writes to the device.

    python3 tools/live_watch.py --device http://192.168.178.60
    python3 tools/live_watch.py --replay session.csv      # test with a logger session

Anomalies:
  - deviation > 5 °C for more than 5 min (pitmaster auto, outside autotune,
    not within 15 min after an open lid or 20 min after a setpoint change)
  - oscillation: >= 3 sign changes of the deviation (1 °C hysteresis) with
    >= 4 °C peak to peak in 20 min
  - output at a limit (minimum or 100 %) for more than 10 min
  - flame out alarm / flame out suspicion
  - sensor of the pitmaster channel lost (999)
  - autotune finished or aborted
"""
import argparse
import csv
import json
import sys
import time
import urllib.request
from collections import deque

INTERVAL_S = 60


class Watch:
    def __init__(self, min_output=None):
        self.min_output = min_output  # minOut of the active profile (from /settings)
        self.hist = deque(maxlen=30)  # (t, temp, set, value, auto)
        self.reported = {}
        self.at_state = None
        self.quiet_until = 0.0  # after lid or setpoint change
        self.last_set = None

    def report(self, key, text, t, repeat_s=900):
        last = self.reported.get(key)
        if last is None or t - last >= repeat_s:
            self.reported[key] = t
            print(f"[{time.strftime('%H:%M', time.localtime(t))}] {text}", flush=True)
            return True
        return False

    def clear(self, key):
        self.reported.pop(key, None)

    def step(self, t, temp, setp, value, typ, flame_out, autotune, lid=False):
        auto = typ == "auto"
        at_running = bool(autotune) and autotune.get("state") == 1
        if lid:
            self.quiet_until = max(self.quiet_until, t + 900)
        if self.last_set is not None and abs(setp - self.last_set) > 0.05:
            self.quiet_until = max(self.quiet_until, t + 1200)
        self.last_set = setp
        quiet = t < self.quiet_until
        if quiet:
            self.hist.clear()
        self.hist.append((t, temp, setp, value, auto and not at_running and not quiet))

        if temp is None or temp >= 900:
            self.report("sensor", "Fühler des Pitmaster-Kanals liefert keinen Wert (999)!", t, 300)
            return
        self.clear("sensor")

        if flame_out:
            self.report("flame", f"FLAMME AUS gemeldet bei {temp:.1f} °C (Soll {setp}). Gas prüfen!", t, 300)
        else:
            self.clear("flame")

        if autotune:
            st = autotune.get("state")
            if st != self.at_state and self.at_state is not None and st in (2, 3):
                if st == 2:
                    self.report("at", f"Autotune fertig: Kp {autotune.get('kp')} Ki {autotune.get('ki')} Kd {autotune.get('kd')}", t, 0)
                else:
                    self.report("at", f"Autotune abgebrochen, Fehler {autotune.get('error')}", t, 0)
            self.at_state = st

        valid = [h for h in self.hist if h[4]]
        # deviation > 5 °C for > 5 min
        recent = [h for h in valid if t - h[0] <= 300]
        if len(recent) >= 5 and all(abs(h[1] - h[2]) > 5 for h in recent):
            dev = recent[-1][1] - recent[-1][2]
            self.report("dev", f"Abweichung seit > 5 min: {dev:+.1f} °C (Ist {recent[-1][1]:.1f}, Soll {recent[-1][2]})", t)
        else:
            self.clear("dev")

        # oscillation in the last 20 min
        win = [h for h in valid if t - h[0] <= 1200]
        if len(win) >= 15:
            e = [h[1] - h[2] for h in win]
            # sign changes with 1 °C hysteresis, noise around zero does not count
            signs = [1 if v > 1 else -1 for v in e if abs(v) > 1]
            changes = sum(1 for a, b in zip(signs, signs[1:]) if a != b)
            if changes >= 3 and max(e) - min(e) >= 4:
                self.report("osc", f"Regelung schwingt: {changes} Vorzeichenwechsel, ±{(max(e) - min(e)) / 2:.1f} °C in 20 min", t)

        # output at a limit for > 10 min
        lim = [h for h in valid if t - h[0] <= 600]
        if len(lim) >= 10:
            values = [h[3] for h in lim]
            if all(v >= 99.5 for v in values):
                self.report("max", "Ausgang seit > 10 min auf 100 % (Gas zu? Flasche leer? Wind?)", t)
            elif self.min_output is not None and max(values) <= self.min_output + 0.5 and lim[-1][1] > lim[-1][2] + 2:
                self.report("min", f"Ausgang seit > 10 min an der Mindeststufe ({values[-1]} %) und trotzdem über Soll", t)


def poll_device(url):
    with urllib.request.urlopen(url.rstrip("/") + "/data", timeout=10) as r:
        data = json.loads(r.read().decode())
    pm = data["pitmaster"]["pm"][0]
    ch = data["channel"][int(pm["channel"]) - 1]
    return ch["temp"], pm["set"], pm["value"], pm["typ"], pm.get("flameOut", False), pm.get("autotune"), pm.get("lid", False)


def min_output_of(url):
    """minOut of the profile of pitmaster 0 (read only)"""
    with urllib.request.urlopen(url.rstrip("/") + "/data", timeout=10) as r:
        pid = json.loads(r.read().decode())["pitmaster"]["pm"][0]["pid"]
    with urllib.request.urlopen(url.rstrip("/") + "/settings", timeout=10) as r:
        profiles = json.loads(r.read().decode())["pid"]
    return float(next(p for p in profiles if p["id"] == pid).get("minOut", 0))


def replay(path, watch):
    with open(path) as f:
        rows = list(csv.DictReader(f))
    last = None
    for r in rows:
        t = float(r["ts"])
        if last is not None and t - last < INTERVAL_S:
            continue
        last = t
        flags = int(float(r["flags"] or 0))
        typ = {"0": "off", "1": "manual", "2": "auto"}.get(str(int(float(r["type"]))), "off")
        at = {"state": int(float(r["at_state"])), "kp": "?", "ki": "?", "kd": "?", "error": "?"}
        temp = float(r["temp"]) if r["temp"] not in ("", "None") else 999.0
        watch.step(t, temp, float(r["set"]), float(r["out"]), typ, bool(flags & 8), at, bool(flags & 2))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device")
    ap.add_argument("--replay")
    ap.add_argument("--count", type=int, default=0, help="number of polls (0 = endless)")
    args = ap.parse_args()
    watch = Watch()
    if args.device:
        try:
            watch.min_output = min_output_of(args.device)
        except Exception as e:  # noqa: BLE001
            print(f"minOut nicht lesbar ({e}), Prüfung 'Mindeststufe' aus", flush=True)
    if args.replay:
        replay(args.replay, watch)
        return
    if not args.device:
        ap.error("--device or --replay")
    n = 0
    while True:
        try:
            watch.step(time.time(), *poll_device(args.device))
        except Exception as e:  # noqa: BLE001
            watch.report("conn", f"Gerät nicht erreichbar: {e}", time.time(), 600)
        n += 1
        if args.count and n >= args.count:
            break
        time.sleep(INTERVAL_S)


if __name__ == "__main__":
    sys.exit(main())
