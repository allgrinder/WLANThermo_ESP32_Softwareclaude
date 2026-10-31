#!/usr/bin/env python3
# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
"""Plots and 10 s CSV extracts of the simulator traces for the docs.

    python3 tools/plot_sim.py test/sim/out docs/sim
"""
import csv
import glob
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

BLUE = "#2a78d6"      # device temperature
ORANGE = "#eb6834"    # chamber (model)
AQUA = "#1baf7a"      # servo opening
INK = "#3d3d3a"       # setpoint (text color, reference line)
MUTED = "#8c8b84"
RELAY_BG = "#ecebe6"


def read(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    return rows


def downsample(rows, out_path, step_s=10):
    with open(out_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        for r in rows:
            if int(float(r["t_s"])) % step_s == 0:
                w.writerow(r)


def spans(t, flags):
    """[(start, end)] where flags is true"""
    out, start = [], None
    for ti, f in zip(t, flags):
        if f and start is None:
            start = ti
        elif not f and start is not None:
            out.append((start, ti))
            start = None
    if start is not None:
        out.append((start, t[-1]))
    return out


def plot(rows, title, out_png):
    t = [float(r["t_s"]) / 60.0 for r in rows]
    temp = [float(r["temp"]) if float(r["temp"]) < 900 else float("nan") for r in rows]
    setp = [float(r["set"]) for r in rows]
    chamber = [float(r["chamber"]) for r in rows]
    opening = [float(r["opening"]) for r in rows]
    relay = [r["at_phase"] == "2" for r in rows]
    approach = [r["at_phase"] == "1" for r in rows]
    flame = [r["flame_out"] == "1" for r in rows]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(11, 6), sharex=True,
                                   gridspec_kw={"height_ratios": [3, 2]})
    for ax in (ax1, ax2):
        for a, b in spans(t, relay):
            ax.axvspan(a, b, color=RELAY_BG, lw=0)
        ax.grid(True, color="#e2e1dc", lw=0.6)
        for s in ("top", "right"):
            ax.spines[s].set_visible(False)
        ax.spines["left"].set_color(MUTED)
        ax.spines["bottom"].set_color(MUTED)
        ax.tick_params(colors=INK, labelsize=9)

    ax1.plot(t, chamber, color=ORANGE, lw=1.2, label="Garraum (Modell)")
    ax1.plot(t, temp, color=BLUE, lw=1.6, label="Fühler (Gerät)")
    ax1.plot(t, setp, color=INK, lw=1.0, ls="--", label="Soll")
    ax1.set_ylabel("Temperatur [°C]", color=INK)
    ax1.legend(loc="lower right", fontsize=9, frameon=False)
    ax1.set_title(title, loc="left", fontsize=11, color=INK)

    ax2.plot(t, opening, color=AQUA, lw=1.4, label="Servo-Öffnung")
    ax2.set_ylabel("Öffnung [%]", color=INK)
    ax2.set_ylim(-3, 103)
    ax2.set_xlabel("Zeit [min]  (grau hinterlegt: Relay-Phase des Autotune)", color=INK)
    ax2.legend(loc="upper right", fontsize=9, frameon=False)

    fl = spans(t, flame)
    if fl:
        ax1.axvline(fl[0][0], color=MUTED, lw=1.0, ls=":")
        ax1.annotate("Flamme aus erkannt", (fl[0][0], ax1.get_ylim()[1]), fontsize=8,
                     color=INK, ha="right", va="top", xytext=(-4, -4), textcoords="offset points")
    ap = spans(t, approach)
    if ap:
        ax1.annotate("Anfahren", (ap[0][0], ax1.get_ylim()[0]), fontsize=8, color=INK,
                     va="bottom", xytext=(4, 4), textcoords="offset points")

    fig.tight_layout()
    fig.savefig(out_png, dpi=90)
    plt.close(fig)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "test/sim/out"
    dst = sys.argv[2] if len(sys.argv) > 2 else "docs/sim"
    os.makedirs(os.path.join(dst, "csv"), exist_ok=True)
    for path in sorted(glob.glob(os.path.join(src, "*.csv"))):
        name = os.path.splitext(os.path.basename(path))[0]
        rows = read(path)
        if not rows:
            continue
        downsample(rows, os.path.join(dst, "csv", name + ".csv"))
        if name.startswith("main_") or name.startswith("abort_"):
            plot(rows, name, os.path.join(dst, name + ".png"))
        print("ok", name)


if __name__ == "__main__":
    main()
