#!/usr/bin/env python3
"""
animate.py - Demo animation of one run (vehicles, RSUs, jammer, alerts, detector state).

The run must be made with --posLog=1, e.g.
    ./ns3 run "v2v-jam --scenario=highway --jammer=constant --mitigation=3 --posLog=1 --outDir=results/demo"
    python3 analysis/animate.py --run ns-3.47/results/demo --out demo.gif

Colours: blue dot = vehicle, red outline = its detector says JAMMED,
         green square = RSU, red X = jammer (shaded circle = area where the jammer power is
         above the ground-truth threshold), star = alert source, yellow ring = received the
         current alert in time.
"""
import argparse
import json
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402
from matplotlib.animation import FuncAnimation, PillowWriter  # noqa: E402


def parse_extra(s):
    return dict(kv.split("=", 1) for kv in str(s).split(";") if "=" in kv)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True, help="run folder (made with --posLog=1)")
    ap.add_argument("--out", default="demo.gif")
    ap.add_argument("--start", type=float, default=8.0, help="first second to show")
    ap.add_argument("--end", type=float, default=25.0, help="last second to show")
    ap.add_argument("--fps", type=int, default=4)
    args = ap.parse_args()

    prm = json.load(open(os.path.join(args.run, "params.json")))
    pos = pd.read_csv(os.path.join(args.run, "positions.csv"))
    ev = pd.read_csv(os.path.join(args.run, "events.csv"), keep_default_na=False, dtype={"extra": str})
    pos["time"] = pos.time_ms / 1000.0  # CSVs are in ms; the animation works in seconds
    ev["time"] = ev.time_ms / 1000.0
    times = sorted(t for t in pos.time.unique() if args.start <= t <= args.end)
    if not times:
        raise SystemExit("no positions in that time range (did you use --posLog=1?)")

    det = ev[ev.event == "DET_STATE"].copy()
    det["to"] = det.extra.map(lambda s: parse_extra(s).get("to"))
    gen = ev[ev.event == "ALERT_GEN"]
    rx = ev[ev.event == "RX"].copy()
    rx["lat"] = rx.extra.map(lambda s: float(parse_extra(s).get("lat_ms", "nan")))

    # Radius where jammer power >= gtJamDbm (log-distance model, same as the simulator).
    gt_dbm = -92
    r_jam = 10 ** ((prm["jamPower"] - 47.8 - gt_dbm) / (10 * prm["pathLossExp"]))

    fig, ax = plt.subplots(figsize=(10, 4 if prm["scenario"] != "urban" else 8))

    def jammed_set(t):
        d = det[det.time <= t].groupby("node").last()
        return set(d[d.to == "JAMMED"].index)

    def frame(t):
        ax.clear()
        snap = pos[pos.time == t]
        veh = snap[snap.kind == "vehicle"]
        rsu = snap[snap.kind == "rsu"]
        jam = snap[snap.kind.isin(["jammer", "interferer"])]
        jammed = jammed_set(t)
        if len(jam) and prm["jammer"] != "none" and t >= prm["jamStart"]:
            jx, jy = jam.x.iloc[0], jam.y.iloc[0]
            ax.add_patch(plt.Circle((jx, jy), r_jam, color="#e34948", alpha=0.08))
        ax.scatter(veh.x, veh.y, s=28, color="#2a78d6", zorder=3)
        jv = veh[veh.node.isin(jammed)]
        ax.scatter(jv.x, jv.y, s=80, facecolors="none", edgecolors="#e34948", linewidths=1.5, zorder=4)
        ax.scatter(rsu.x, rsu.y, s=70, marker="s", color="#008300", zorder=3)
        if len(jam):
            ax.scatter(jam.x, jam.y, s=120, marker="X", color="#e34948", zorder=5)
        # current alert = latest alert generated in the last second
        recent = gen[(gen.time <= t) & (gen.time > t - 1.0)]
        title = f"{prm['scenario']}  jammer={prm['jammer']}  mitigation={prm['mitigation']}   t={t:.1f}s"
        if len(recent):
            a = recent.iloc[-1]
            src = snap[snap.node == a.node]
            ax.scatter(src.x, src.y, s=220, marker="*", color="#eda100", edgecolors="#0b0b0b", zorder=6)
            ok = rx[(rx.seq == a.seq) & (rx.lat <= prm["deadlineMs"])].node
            got = veh[veh.node.isin(ok)]
            ax.scatter(got.x, got.y, s=140, facecolors="none", edgecolors="#eda100", linewidths=2, zorder=4)
            title += f"   alert #{a.seq}: {len(got)} vehicles reached in time"
        ax.set_title(title, fontsize=10)
        L = prm["roadLength"]
        if prm["scenario"] == "urban":
            ax.set_xlim(0, L)
            ax.set_ylim(0, L)
        else:
            ax.set_xlim(0, L)
            ax.set_ylim(-60, 60)
        ax.set_xlabel("x (m)")
        ax.set_aspect("auto")

    anim = FuncAnimation(fig, frame, frames=times, interval=1000 / args.fps)
    anim.save(args.out, writer=PillowWriter(fps=args.fps))
    print("wrote", args.out)


if __name__ == "__main__":
    main()
