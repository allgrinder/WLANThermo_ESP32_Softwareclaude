#!/usr/bin/env python3
# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
"""Report for one cook session of the logger (wlt-update).

    python3 tools/analyze_cook.py --logger http://192.168.178.250 --session 3 --out docs/cooks/2026-11-01
    python3 tools/analyze_cook.py --csv session.csv --out report_dir

Input is the telemetry CSV of the logger (/sessions/<id>.csv, 1-5 s). Without
telemetry the cloud CSV (/sessions/<id>/cloud.csv, 30 s) is used, then only
temperature based figures are available.

Writes <out>/report.md, <out>/cook.png and <out>/metrics.json.
"""
import argparse
import csv
import io
import json
import math
import os
import urllib.request

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

BLUE, ORANGE, AQUA, INK, MUTED = "#2a78d6", "#eb6834", "#1baf7a", "#3d3d3a", "#8c8b84"
SERVO_US_MIN, SERVO_US_MAX = 550.0, 2250.0  # default servo pulse range (Pitmaster.cpp)


# ---------------------------------------------------------------------------
def fetch(url):
    with urllib.request.urlopen(url, timeout=30) as r:
        return r.read().decode()


def load(args):
    if args.csv:
        with open(args.csv) as f:
            text = f.read()
    else:
        text = fetch(f"{args.logger.rstrip('/')}/sessions/{args.session}.csv")
        if len(text.splitlines()) < 3:
            text = fetch(f"{args.logger.rstrip('/')}/sessions/{args.session}/cloud.csv")
    rows = list(csv.DictReader(io.StringIO(text)))
    if not rows:
        raise SystemExit("session is empty")
    if "temp" in rows[0]:
        return "telemetry", telemetry_arrays(rows)
    return "cloud", cloud_arrays(rows)


def _f(v, default=math.nan):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def telemetry_arrays(rows):
    d = {k: np.array([_f(r[k]) for r in rows]) for k in
         ("ts", "raw", "temp", "set", "out", "servo_us", "p", "i", "d", "esum", "flags", "at_state", "at_phase",
          "at_cycle", "type")}
    d["raw"][d["raw"] >= 900] = np.nan
    d["temp"][d["temp"] >= 900] = np.nan
    flags = d["flags"].astype(int)
    d["jump"] = (flags & 1) > 0
    d["lid"] = (flags & 2) > 0
    d["fo_susp"] = (flags & 4) > 0
    d["fo"] = (flags & 8) > 0
    d["at_running"] = (flags & 16) > 0
    return d


def cloud_arrays(rows):
    ch = int(_f(rows[0].get("pm_channel"), 1) or 1)
    d = {"ts": np.array([_f(r["ts"]) for r in rows]),
         "raw": np.array([_f(r.get(f"ch{ch}")) for r in rows]),
         "set": np.array([_f(r.get("pm_set")) for r in rows]),
         "out": np.array([_f(r.get("pm_value")) for r in rows]),
         "type": np.array([2.0 if r.get("pm_typ") == "auto" else (1.0 if r.get("pm_typ") == "manual" else 0.0)
                           for r in rows])}
    d["temp"] = d["raw"]
    n = len(rows)
    for k in ("servo_us", "p", "i", "d", "esum"):
        d[k] = np.full(n, np.nan)
    for k in ("jump", "lid", "fo_susp", "fo", "at_running"):
        d[k] = np.zeros(n, dtype=bool)
    d["at_state"] = d["at_phase"] = d["at_cycle"] = np.zeros(n)
    return d


def edges(mask):
    """[(start_index, end_index_exclusive)] of True runs"""
    out, start = [], None
    for i, m in enumerate(mask):
        if m and start is None:
            start = i
        elif not m and start is not None:
            out.append((start, i))
            start = None
    if start is not None:
        out.append((start, len(mask)))
    return out


# ---------------------------------------------------------------------------
def analyze(d, settle_band=2.0):
    ts = d["ts"]
    t = (ts - ts[0]) / 60.0
    dt = np.median(np.diff(ts)) if len(ts) > 1 else 1.0
    x = d["temp"]
    w = d["set"]
    auto = d["type"] == 2
    m = {"duration_h": round((ts[-1] - ts[0]) / 3600.0, 2), "sample_s": round(float(dt), 1)}

    # disturbances end an evaluation window: lid, flame out (incl. 10 min
    # before the alarm, the drop starts earlier), autotune
    disturb = d["lid"] | d["fo"] | d["at_running"]
    for a, _ in edges(d["fo"]):
        disturb[max(0, a - int(600 / dt)):a] = True
    # the lid opens before the detection reacts
    for a, _ in edges(d["lid"]):
        disturb[max(0, a - int(60 / dt)):a] = True

    # ---- setpoint changes ----
    changes = [0] + [i for i in range(1, len(w)) if abs(w[i] - w[i - 1]) > 0.05]
    steps = []
    for k, i in enumerate(changes):
        end = changes[k + 1] if k + 1 < len(changes) else len(w)
        # an autotune at the start: evaluate the PID after it
        begin = i
        runs = [r for r in edges(d["at_running"][i:end]) if r[0] * dt < 600]
        if runs:
            begin = i + runs[-1][1]
        stop = begin
        while stop < end and not disturb[stop]:
            stop += 1
        if stop - begin < 2:
            steps.append({"t_min": round(t[i], 1), "set": float(w[i]), "from": None if k == 0 else float(w[i - 1]),
                          "overshoot": None, "startup_overshoot": None, "settling_min": None,
                          "window_min": 0.0, "after_autotune": begin > i})
            continue
        e = x[begin:stop] - w[i]
        rising = (k == 0) or (w[i] > w[i - 1])
        over = float(np.nanmax(e)) if rising else float(-np.nanmin(e))
        # settling: first time after which |e| stays <= band until the window end (min 5 min)
        inside = np.abs(e) <= settle_band
        settle = None
        for j in range(len(inside)):
            if inside[j:].all() and (len(inside) - j) * dt >= 300:
                settle = (begin - i + j) * dt / 60.0
                break
        first = (k == 0) and begin == i
        steps.append({"t_min": round(t[i], 1), "set": float(w[i]), "from": None if k == 0 else float(w[i - 1]),
                      "overshoot": None if first else round(max(over, 0.0), 2),
                      "startup_overshoot": round(max(over, 0.0), 2) if first else None,
                      "settling_min": None if settle is None else round(settle, 1),
                      "window_min": round((stop - begin) * dt / 60.0, 1), "after_autotune": begin > i})
    m["setpoint_changes"] = steps

    # ---- steady state: auto, no lid/autotune/flame, settled, 15 min after changes ----
    steady = auto & ~disturb & ~np.isnan(x)
    for k, i in enumerate(changes):
        s = steps[k]["settling_min"]
        hold = (s if s is not None else 30.0) * 60.0
        steady[i:i + int(hold / dt)] = False
    for a, b in edges(d["lid"]):
        steady[a:b + int(900 / dt)] = False
    e = (x - w)[steady]
    if len(e):
        m["steady"] = {"minutes": round(len(e) * dt / 60.0, 1), "mean": round(float(np.mean(e)), 2),
                       "mean_abs": round(float(np.mean(np.abs(e))), 2),
                       "p95_abs": round(float(np.percentile(np.abs(e), 95)), 2),
                       "max_abs": round(float(np.max(np.abs(e))), 2)}
    else:
        m["steady"] = None

    # ---- oscillation in steady state (zero crossings of the error) ----
    osc = []
    for a, b in edges(steady):
        if (b - a) * dt < 20 * 60:
            continue
        seg = (x - w)[a:b]
        seg = seg - np.mean(seg)
        up = [j for j in range(1, len(seg)) if seg[j - 1] < 0 <= seg[j]]
        if len(up) < 3:
            continue
        periods = np.diff(up) * dt
        amps = [(np.max(seg[up[j]:up[j + 1]]) - np.min(seg[up[j]:up[j + 1]])) / 2 for j in range(len(up) - 1)]
        amp, per = float(np.median(amps)), float(np.median(periods))
        regular = np.std(periods) / max(per, 1e-9) < 0.35
        if amp >= 1.0 and regular and per >= 60:
            osc.append({"t_min": round(t[a], 1), "period_s": round(per), "amplitude": round(amp, 2)})
    m["oscillation"] = osc

    # ---- servo wear and limits ----
    if not np.all(np.isnan(d["servo_us"])):
        us = d["servo_us"]
        valid = us > 0
        travel_us = float(np.nansum(np.abs(np.diff(us[valid])))) if valid.sum() > 1 else 0.0
        hours = max(valid.sum() * dt / 3600.0, 1e-9)
        diffs = np.diff(us[valid])
        diffs = diffs[diffs != 0]
        reversals = int(np.sum(np.diff(np.sign(diffs)) != 0)) if len(diffs) > 1 else 0
        m["servo"] = {"travel_us_per_h": round(travel_us / hours), "travel_full_strokes_per_h":
                      round(travel_us / hours / (SERVO_US_MAX - SERVO_US_MIN), 1),
                      "reversals_per_h": round(reversals / hours)}
    out = d["out"]
    run = auto & ~np.isnan(out)
    if run.any():
        omin = float(np.nanmin(out[run]))
        m["output"] = {"min": round(omin, 1), "mean": round(float(np.nanmean(out[run])), 1),
                       "time_at_min_pct": round(100.0 * float(np.mean(out[run] <= omin + 0.5)), 1),
                       "time_at_max_pct": round(100.0 * float(np.mean(out[run] >= 99.5)), 1)}

    # ---- lid events ----
    m["lid_events"] = [{"t_min": round(t[a], 1), "duration_s": round((b - a) * dt),
                        "drop": round(float(np.nanmax(x[max(a - 5, 0):a + 1]) - np.nanmin(x[a:b + 1])), 1)}
                       for a, b in edges(d["lid"])]

    # ---- flame out alarms with assessment ----
    alarms = []
    for a, b in edges(d["fo"]):
        after = x[a:a + int(600 / dt)]
        fall = float(after[0] - np.nanmin(after)) if len(after) else 0.0
        rise = float(np.nanmax(after) - after[0]) if len(after) else 0.0
        # real flame out: temperature keeps falling towards ambient after the alarm
        verdict = "echt (Temperatur fällt weiter)" if fall >= 8 and rise < 1 else "vermutlich Fehlalarm (Temperatur fing sich)"
        alarms.append({"t_min": round(t[a], 1), "temp": round(float(x[a]), 1), "fall_10min": round(fall, 1),
                       "verdict": verdict})
    m["flame_out_alarms"] = alarms
    m["flame_suspect_minutes"] = round(float(np.sum(d["fo_susp"]) * dt / 60.0), 1)

    # ---- P/I/D share of the output ----
    if not np.all(np.isnan(d["p"])):
        sel = auto & ~d["at_running"] & ~np.isnan(d["p"])
        tot = np.abs(d["p"][sel]) + np.abs(d["i"][sel]) + np.abs(d["d"][sel])
        tot = np.where(tot == 0, np.nan, tot)
        m["pid_share_pct"] = {k: round(100.0 * float(np.nanmean(np.abs(d[k][sel]) / tot)), 1) for k in ("p", "i", "d")}

    # ---- autotune ----
    ats = edges(d["at_running"])
    m["autotune_runs"] = [{"t_min": round(t[a], 1), "minutes": round((b - a) * dt / 60.0, 1),
                           "end_state": int(d["at_state"][min(b, len(d["at_state"]) - 1)]),
                           "cycles": int(np.nanmax(d["at_cycle"][a:min(b + 1, len(d["at_cycle"]))]))} for a, b in ats]
    return m


# ---------------------------------------------------------------------------
def plot(d, m, path):
    t = (d["ts"] - d["ts"][0]) / 60.0
    has_pid = not np.all(np.isnan(d["p"]))
    n = 3 if has_pid else 2
    fig, axes = plt.subplots(n, 1, figsize=(11, 2.4 * n + 1.5), sharex=True,
                             gridspec_kw={"height_ratios": [3, 2, 2][:n]})
    for ax in axes:
        for a, b in edges(d["lid"]):
            ax.axvspan(t[a], t[min(b, len(t) - 1)], color="#ecebe6", lw=0)
        ax.grid(True, color="#e2e1dc", lw=0.6)
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
        ax.tick_params(colors=INK, labelsize=9)
    ax = axes[0]
    ax.plot(t, d["temp"], color=BLUE, lw=1.4, label="Garraum (Fühler)")
    ax.plot(t, d["set"], color=INK, lw=1.0, ls="--", label="Soll")
    for a, _ in edges(d["fo"]):
        ax.axvline(t[a], color=MUTED, lw=1, ls=":")
        ax.annotate("Flamme aus?", (t[a], np.nanmax(d["temp"])), fontsize=8, color=INK, ha="right", va="top")
    ax.set_ylabel("°C", color=INK)
    ax.legend(loc="lower right", fontsize=9, frameon=False)
    ax.set_title("Grillabend (grau: Deckel offen)", loc="left", fontsize=11, color=INK)
    axes[1].plot(t, d["out"], color=AQUA, lw=1.2, label="Ausgang")
    axes[1].set_ylabel("Ausgang %", color=INK)
    axes[1].set_ylim(-3, 103)
    axes[1].legend(loc="upper right", fontsize=9, frameon=False)
    if has_pid:
        axes[2].plot(t, d["p"], color=BLUE, lw=1.0, label="P")
        axes[2].plot(t, d["i"], color=ORANGE, lw=1.0, label="I")
        axes[2].plot(t, d["d"], color=AQUA, lw=1.0, label="D")
        axes[2].set_ylabel("Anteil %", color=INK)
        axes[2].set_ylim(-60, 120)
        axes[2].legend(loc="upper right", fontsize=9, frameon=False, ncol=3)
    axes[-1].set_xlabel("Zeit [min]", color=INK)
    fig.tight_layout()
    fig.savefig(path, dpi=90)
    plt.close(fig)


def report(m, source, title):
    L = [f"# {title}", "", f"Quelle: {source}, Dauer {m['duration_h']} h, Raster {m['sample_s']} s.", ""]
    L += ["![Verlauf](cook.png)", "", "## Sollwerte", "",
          "| ab [min] | Soll [°C] | von | Überschwingen [°C] | Einschwingzeit ±2 °C [min] | ausgewertet [min] |",
          "|---|---|---|---|---|---|"]
    for s in m["setpoint_changes"]:
        if s["overshoot"] is not None:
            over = s["overshoot"]
        elif s["startup_overshoot"] is not None:
            over = f"{s['startup_overshoot']} (Anheizen)"
        else:
            over = "-"
        if s.get("after_autotune"):
            over = f"{over} (nach Autotune)"
        settle = s["settling_min"] if s["settling_min"] is not None else "nicht erreicht"
        L.append(f"| {s['t_min']} | {s['set']} | {s['from'] or '-'} | {over} | {settle} | {s['window_min']} |")
    L += ["", "Ausgewertet wird bis zur nächsten Störung (Deckel, Flamme aus, Autotune) oder Sollwertänderung."]
    st = m["steady"]
    L += ["", "## Regelabweichung (eingeschwungen)", ""]
    if st:
        L += [f"- {st['minutes']} min ausgewertet: Mittel {st['mean']} °C, |e| Mittel {st['mean_abs']} °C, "
              f"P95 {st['p95_abs']} °C, max {st['max_abs']} °C"]
    else:
        L += ["- keine eingeschwungene Phase gefunden"]
    L += ["", "## Schwingungen", ""]
    L += [f"- ab {o['t_min']} min: Periode {o['period_s']} s, Amplitude ±{o['amplitude']} °C" for o in m["oscillation"]] or \
         ["- keine regelmäßige Schwingung ≥ 1 °C erkannt"]
    L += ["", "## Servo und Ausgang", ""]
    if "servo" in m:
        s = m["servo"]
        L += [f"- Servo-Weg: {s['travel_us_per_h']} µs/h (≈ {s['travel_full_strokes_per_h']} volle Hübe/h), "
              f"{s['reversals_per_h']} Richtungswechsel/h"]
    if "output" in m:
        o = m["output"]
        L += [f"- Ausgang im Auto-Betrieb: Mittel {o['mean']} %, Minimum {o['min']} %, "
              f"{o['time_at_min_pct']} % der Zeit an der Mindeststufe, {o['time_at_max_pct']} % am Anschlag (100 %)"]
    if "pid_share_pct" in m:
        p = m["pid_share_pct"]
        L += [f"- Anteil am Ausgang (|P|:|I|:|D|): P {p['p']} %, I {p['i']} %, D {p['d']} %"]
    L += ["", "## Deckel offen", ""]
    L += [f"- {e['t_min']} min: {e['duration_s']} s erkannt, Abfall {e['drop']} °C" for e in m["lid_events"]] or ["- keine"]
    L += ["", "## Flamme aus", ""]
    L += [f"- {a['t_min']} min bei {a['temp']} °C: danach −{a['fall_10min']} °C in 10 min → {a['verdict']}"
          for a in m["flame_out_alarms"]] or ["- kein Alarm"]
    L += [f"- Verdacht (Teilbedingung) insgesamt {m['flame_suspect_minutes']} min"]
    if m["autotune_runs"]:
        L += ["", "## Autotune", ""]
        L += [f"- ab {a['t_min']} min, {a['minutes']} min, Zyklen {a['cycles']}, Endzustand {a['end_state']} "
              f"(2 = fertig, 3 = abgebrochen)" for a in m["autotune_runs"]]
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logger", default="http://192.168.178.250")
    ap.add_argument("--session", type=int)
    ap.add_argument("--csv")
    ap.add_argument("--out", required=True)
    ap.add_argument("--title", default="Grillabend")
    args = ap.parse_args()
    if not args.csv and args.session is None:
        ap.error("--session or --csv required")
    os.makedirs(args.out, exist_ok=True)
    source, d = load(args)
    m = analyze(d)
    plot(d, m, os.path.join(args.out, "cook.png"))
    with open(os.path.join(args.out, "metrics.json"), "w") as f:
        json.dump(m, f, indent=1)
    with open(os.path.join(args.out, "report.md"), "w") as f:
        f.write(report(m, source, args.title))
    print(f"report: {os.path.join(args.out, 'report.md')}")


if __name__ == "__main__":
    main()
