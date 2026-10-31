#!/usr/bin/env python3
# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
"""Fits the gas smoker model of the simulator (test/sim/plant.cpp) to a real
cook session and stores it as a new simulator scenario.

    python3 tools/fit_plant.py --logger http://192.168.178.250 --session 3 --name elfuego --out test/sim/plants
    python3 tools/fit_plant.py --csv session.csv --name elfuego --out test/sim/plants

Model (same as plant.cpp):
    Tss(u)   = tMin + (tMax - tMin) * (min(u, valveSat)/valveSat)^valveExp
                                                     u = servo opening %
    chamber: tau * dT/dt = Tss(u(t - L)) - T
    sensor:  first order lag tauSensor
Fitted: L, tau, tMin, tMax, valveExp, valveSat, tauSensor. Lid and flame out phases are
left out (the model has no lid). Heat up (100 %) and the autotune relay are the
best excitation for the valve curve and should be part of the session. Each clean segment of at least
20 min is simulated from its measured start value. Besides the logger telemetry
a cloud format CSV works if it is sampled every 10 s or faster (e.g. a recorder
polling /data every 5 s).

Writes <out>/<name>.plant (input for: test/sim/gas_sim --plant <file>),
<out>/<name>.json and <out>/<name>_fit.png. With --simulate the simulator is
run with the fitted plant and the result is added to <name>.md.
"""
import argparse
import json
import math
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze_cook  # noqa: E402

PARAMS = ["deadTimeS", "tauS", "tMin", "tMax", "valveExp", "tauSensorS", "valveSat"]
BOUNDS = {"deadTimeS": (5, 180), "tauS": (60, 1800), "tMin": (25, 200), "tMax": (120, 400),
          "valveExp": (0.2, 3.0), "tauSensorS": (2, 120), "valveSat": (20, 100)}
REFERENCE = {  # simulator sets P1-P3 (docs/autotune-simulation.md)
    "P1": [20, 240, 70, 220, 0.5, 10, 100], "P2": [40, 420, 75, 250, 0.5, 20, 100],
    "P3": [60, 600, 80, 280, 0.5, 30, 100]}
MAX_CLOUD_STEP_S = 10.0
T_AMB = 20.0
STEP_S = 5.0


def resample(d, step=STEP_S):
    """average to STEP_S, keep masks"""
    ts = d["ts"] - d["ts"][0]
    idx = (ts // step).astype(int)
    n = idx[-1] + 1
    out = {}
    for k in ("temp", "out"):
        s = np.bincount(idx, weights=np.nan_to_num(d[k]), minlength=n)
        c = np.bincount(idx, weights=~np.isnan(d[k]), minlength=n)
        out[k] = np.where(c > 0, s / np.maximum(c, 1), np.nan)
    # the autotune relay and the heat up are the best excitation, they stay in
    bad = d["lid"] | d["fo"] | np.isnan(d["temp"])
    # a lid disturbs the chamber longer than it is detected, a flame out starts earlier
    dt = float(np.median(np.diff(d["ts"]))) if len(d["ts"]) > 1 else 1.0
    bad = bad.copy()
    for a, b in analyze_cook.edges(d["lid"]):
        bad[max(0, a - int(60 / dt)):b + int(600 / dt)] = True
    for a, _ in analyze_cook.edges(d["fo"]):
        bad[max(0, a - int(900 / dt)):] = True
    out["bad"] = np.bincount(idx, weights=bad, minlength=n) > 0
    # empty bins (sample interval a bit above STEP_S, short gaps) keep the last
    # opening; 0 % there would feed the model short false gas cuts
    u = out["out"].copy()
    for k in range(1, len(u)):
        if np.isnan(u[k]):
            u[k] = u[k - 1]
    # the servo moves in whole percent (Arduino map() on long)
    out["u"] = np.floor(np.nan_to_num(u, nan=0.0))
    return out


def segments(r, min_len_s=1200):
    good = ~r["bad"]
    return [(a, b) for a, b in analyze_cook.edges(good) if (b - a) * STEP_S >= min_len_s]


def simulate(p, u, x0, step=STEP_S):
    L, tau, tmin, tmax, vexp, tsen, vsat = p
    n = len(u)
    delay = max(1, int(round(L / step)))
    chamber = sensor = x0
    y = np.empty(n)
    span = tmax - tmin
    for k in range(n):
        uk = u[k - delay] if k >= delay else u[0]
        uk = 0.0 if uk < 0 else (vsat if uk > vsat else uk)
        target = tmin + span * (uk / vsat) ** vexp
        chamber += step * (target - chamber) / tau
        sensor += step * (chamber - sensor) / tsen
        y[k] = sensor
    return y


def cost(p, r, segs):
    for name, value in zip(PARAMS, p):
        lo, hi = BOUNDS[name]
        if not lo <= value <= hi:
            return 1e9
    if p[3] <= p[2] + 20:
        return 1e9
    err, n = 0.0, 0
    for a, b in segs:
        y = simulate(p, r["u"][a:b], r["temp"][a])
        e = y - r["temp"][a:b]
        e = e[~np.isnan(e)]
        with np.errstate(over="ignore", invalid="ignore"):
            err += float(np.sum(e * e))
        n += len(e)
    if not math.isfinite(err):
        return 1e9
    return math.sqrt(err / max(n, 1))


def fit(r, segs, fixed=None):
    """fixed: {param: value} for parameters the session can't determine
    (e.g. tMin without a phase at the smallest stage)"""
    fixed = fixed or {}
    free = [k for k, name in enumerate(PARAMS) if name not in fixed]

    def full(x):
        p = np.array([fixed.get(name, 0.0) for name in PARAMS], float)
        p[free] = x
        return p

    def cost_free(x):
        return cost(full(x), r, segs)

    try:
        from scipy.optimize import minimize
    except ImportError:  # pragma: no cover
        minimize = None
    best = None
    starts = list(REFERENCE.values())
    # start value for the steady state curve from the data
    lo, hi = float(np.nanpercentile(r["temp"], 10)) - 10, float(np.nanmax(r["temp"]))
    starts.append([30, 400, lo, hi + 40, 0.5, 20, 100])
    for sat in (40, 60):  # saturating valve (El Fuego 2026-10-30)
        for vexp in (0.5, 1.5):
            starts.append([30, 400, lo, hi, vexp, 20, sat])
    for x0 in starts:
        x0 = np.array(x0, float)[free]
        if minimize is not None:
            res = minimize(cost_free, x0, method="Nelder-Mead",
                           options={"maxiter": 1500, "xatol": 0.05, "fatol": 0.001, "adaptive": True})
            cand = (res.fun, full(res.x))
        else:
            cand = (cost_free(x0), full(x0))
        if best is None or cand[0] < best[0]:
            best = cand
    return best


def plot(r, segs, p, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    t = np.arange(len(r["temp"])) * STEP_S / 60.0
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 5.5), sharex=True, gridspec_kw={"height_ratios": [3, 2]})
    ax1.plot(t, r["temp"], color="#2a78d6", lw=1.3, label="gemessen")
    for k, (a, b) in enumerate(segs):
        ax1.plot(t[a:b], simulate(p, r["u"][a:b], r["temp"][a]), color="#eb6834", lw=1.3,
                 label="Modell" if k == 0 else None)
    ax1.set_ylabel("°C")
    ax1.legend(loc="lower right", frameon=False, fontsize=9)
    ax1.set_title("Streckenmodell gegen Messung (nur ungestörte Abschnitte)", loc="left", fontsize=11)
    ax2.plot(t, r["u"], color="#1baf7a", lw=1.2, label="Servo-Öffnung")
    ax2.set_ylabel("%")
    ax2.set_xlabel("Zeit [min]")
    ax2.legend(loc="upper right", frameon=False, fontsize=9)
    for ax in (ax1, ax2):
        ax.grid(True, color="#e2e1dc", lw=0.6)
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(path, dpi=90)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--logger", default="http://192.168.178.250")
    ap.add_argument("--session", type=int)
    ap.add_argument("--csv")
    ap.add_argument("--name", default="fitted")
    ap.add_argument("--out", default="test/sim/plants")
    ap.add_argument("--simulate", action="store_true", help="run test/sim/gas_sim with the fitted plant")
    ap.add_argument("--fix", action="append", default=[], metavar="NAME=VALUE",
                    help="keep a parameter at a value, e.g. --fix tMin=103 (repeatable)")
    args = ap.parse_args()
    fixed = {}
    for item in args.fix:
        name, _, value = item.partition("=")
        if name not in PARAMS:
            ap.error(f"--fix: unknown parameter {name}, one of {', '.join(PARAMS)}")
        fixed[name] = float(value)
    if not args.csv and args.session is None:
        ap.error("--session or --csv required")
    os.makedirs(args.out, exist_ok=True)

    source, d = analyze_cook.load(args)
    step = float(np.median(np.diff(d["ts"]))) if len(d["ts"]) > 1 else math.inf
    if source != "telemetry" and step > MAX_CLOUD_STEP_S:
        raise SystemExit("fit needs telemetry data (1-5 s), the cloud data (30 s) is too coarse")
    r = resample(d)
    segs = segments(r)
    if not segs:
        raise SystemExit("no clean segment of at least 20 min")
    rmse, p = fit(r, segs, fixed)
    params = dict(zip(PARAMS, [round(float(v), 3) for v in p]))
    result = {"name": args.name, "rmse": round(rmse, 3), "params": params, "fixed": sorted(fixed),
              "segments_min": [round((b - a) * STEP_S / 60.0, 1) for a, b in segs],
              "reference_rmse": {k: round(cost(v, r, segs), 3) for k, v in REFERENCE.items()}}

    plant_file = os.path.join(args.out, args.name + ".plant")
    with open(plant_file, "w") as f:
        f.write(f"{args.name} {p[0]:.1f} {p[1]:.1f} {p[2]:.1f} {p[3]:.1f} {p[5]:.1f} {p[4]:.3f} {p[6]:.1f}\n")
    with open(os.path.join(args.out, args.name + ".json"), "w") as f:
        json.dump(result, f, indent=1)
    plot(r, segs, p, os.path.join(args.out, args.name + "_fit.png"))

    md = [f"# Streckenmodell {args.name}", "",
          f"RMSE {result['rmse']} °C über {sum(result['segments_min'])} min ungestörte Abschnitte {result['segments_min']}.", "",
          "| Parameter | angepasst | P1 | P2 | P3 |", "|---|---|---|---|---|"]
    for k, name in enumerate(PARAMS):
        md.append(f"| {name} | {params[name]} | {REFERENCE['P1'][k]} | {REFERENCE['P2'][k]} | {REFERENCE['P3'][k]} |")
    md += ["", "RMSE der Referenzmodelle auf denselben Daten: " +
           ", ".join(f"{k} {v} °C" for k, v in result["reference_rmse"].items()), "",
           f"![Fit]({args.name}_fit.png)", ""]

    if args.simulate:
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "test", "sim")
        subprocess.run(["make", "-s", "-C", root], check=True)
        simout = os.path.join(args.out, args.name + "_sim")
        os.makedirs(simout, exist_ok=True)
        run = subprocess.run([os.path.join(root, "gas_sim"), simout, "--plant", plant_file],
                             capture_output=True, text=True)
        with open(os.path.join(simout, "summary.md"), encoding="utf-8") as f:
            table = f.read()
        fails = [l for l in run.stdout.splitlines() if "FAIL" in l]
        md += ["## Simulator mit dem angepassten Modell", "",
               "Alle Szenarien (P1-P3 und das angepasste Modell, letzte Zeilen).", "", table, "",
               "Fehlgeschlagene Prüfungen: " + (", ".join(fails) if fails else "keine"), ""]
    with open(os.path.join(args.out, args.name + ".md"), "w", encoding="utf-8") as f:
        f.write("\n".join(md))
    print(json.dumps(result, indent=1))


if __name__ == "__main__":
    main()
