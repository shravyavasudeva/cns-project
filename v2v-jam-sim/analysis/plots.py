#!/usr/bin/env python3
"""
plots.py - Report figures from results/summary_runs.csv (run analysis/metrics.py first).

Usage:
    python3 analysis/plots.py --results results

Writes PNGs to results/figures/ (only the figures whose runs exist):
    sanity_pdr_vs_power.png      beacon PDR vs jammer power             (preset sanity)
    sanity_pdr_vs_distance.png   beacon PDR vs distance to the jammer   (preset sanity)
    ontime_by_config.png         on-time alert reachability, per jammer x scenario x mitigation (main)
    deadline_miss_by_config.png  deadline-miss ratio                    (main)
    latency_by_config.png        alert latency                          (main)
    beacon_pdr_by_config.png     beacon PDR during jamming              (main)
    routing_by_scenario.png      NO_RELAY rate and path switches, mitigation >= 2 (main)
    ontime_vs_density.png        on-time reachability vs number of vehicles (density)
    rsu_effect.png               with vs without RSUs                   (rsu)
    detector_states.png          detector state shares per condition    (confounders + main)
and detector_accuracy.csv (TPR / FPR / delay per condition).
"""
import argparse
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

# Categorical palette (fixed order; identity always also given by the legend).
MIT_COLORS = {0: "#2a78d6", 1: "#eb6834", 2: "#1baf7a", 3: "#eda100"}
MIT_LABELS = {0: "(a) no mitigation", 1: "(b) detection", 2: "(c) + L3 reroute", 3: "(d) + L3 + L4"}
STATE_COLORS = {"normal": "#2a78d6", "congested": "#eb6834", "weak": "#1baf7a", "jammed": "#e34948"}
INK = "#0b0b0b"
INK2 = "#52514e"
GRID = "#e4e3df"
SCEN_ORDER = ["highway", "local", "urban"]
JAM_ORDER = ["none", "constant", "reactive"]


def style():
    plt.rcParams.update({
        "figure.dpi": 150, "savefig.dpi": 150, "font.size": 10,
        "axes.edgecolor": GRID, "axes.labelcolor": INK2, "axes.titlecolor": INK,
        "axes.titlesize": 11, "axes.spines.top": False, "axes.spines.right": False,
        "axes.grid": True, "axes.grid.axis": "y", "grid.color": GRID, "grid.linewidth": 0.8,
        "xtick.color": INK2, "ytick.color": INK2, "legend.frameon": False,
        "axes.axisbelow": True,
    })


def ci95(x):
    x = x.dropna()
    return 1.96 * x.std(ddof=1) / np.sqrt(len(x)) if len(x) > 1 else 0.0


def save(fig, out, name):
    path = os.path.join(out, name)
    fig.savefig(path, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    print("wrote", path)


def grouped_bars(ax, sub, metric, xs, xkey):
    """Bars grouped by xkey, one bar per mitigation level, 95% CI whiskers."""
    mits = [m for m in MIT_COLORS if m in set(sub.mitigation)]
    width = 0.8 / max(1, len(mits))
    for i, mit in enumerate(mits):
        means, errs = [], []
        for x in xs:
            v = sub[(sub[xkey] == x) & (sub.mitigation == mit)][metric]
            means.append(v.mean() if len(v) else np.nan)
            errs.append(ci95(v) if len(v) else 0)
        pos = np.arange(len(xs)) + (i - (len(mits) - 1) / 2) * width
        ax.bar(pos, means, width * 0.92, color=MIT_COLORS[mit], label=MIT_LABELS[mit],
               yerr=errs, error_kw=dict(ecolor=INK2, elinewidth=1, capsize=2))
    ax.set_xticks(np.arange(len(xs)))
    ax.set_xticklabels(xs)


def by_config(df, metric, ylabel, out, name, ylim=None):
    main = df[(df.preset == "main") & (df.confounder == "none")]
    if main.empty:
        return
    jams = [j for j in JAM_ORDER if j in set(main.jammer)]
    scens = [s for s in SCEN_ORDER if s in set(main.scenario)]
    fig, axes = plt.subplots(1, len(jams), figsize=(4.2 * len(jams), 3.6), sharey=True)
    axes = np.atleast_1d(axes)
    for ax, jam in zip(axes, jams):
        grouped_bars(ax, main[main.jammer == jam], metric, scens, "scenario")
        ax.set_title(f"jammer: {jam}")
        if ylim:
            ax.set_ylim(*ylim)
    axes[0].set_ylabel(ylabel)
    axes[-1].legend(loc="upper left", bbox_to_anchor=(1.0, 1.0))
    save(fig, out, name)


def sanity(df, results, out):
    s = df[df.preset == "sanity"]
    if s.empty:
        return
    # PDR vs jammer power (jammer=none plotted as a reference line)
    jam = s[s.jammer == "constant"].groupby("jamPower").beacon_pdr.agg(["mean", ci95]).reset_index()
    fig, ax = plt.subplots(figsize=(5, 3.5))
    ax.errorbar(jam.jamPower, jam["mean"], yerr=jam["ci95"], color=MIT_COLORS[0], lw=2,
                marker="o", ms=6, capsize=3, label="constant jammer")
    base = s[s.jammer == "none"].beacon_pdr.mean()
    if not np.isnan(base):
        ax.axhline(base, color=INK2, lw=1, ls="--", label="no jammer")
    ax.set_xlabel("jammer power (dBm)")
    ax.set_ylabel("beacon PDR during jamming")
    ax.set_ylim(0, 1.05)
    ax.set_title("Sanity check: PDR vs jammer power (highway)")
    ax.legend()
    save(fig, out, "sanity_pdr_vs_power.png")

    # PDR vs distance to jammer, before vs during jamming (default power 20 dBm)
    frames = []
    for rd in s[(s.jammer == "constant") & (s.jamPower == 20)].run_dir:
        f = os.path.join(rd, "pdr_by_jamdist.csv")
        if os.path.exists(f):
            frames.append(pd.read_csv(f))
    if frames:
        d = pd.concat(frames).groupby(["phase", "dist_m"])[["expected", "received"]].sum().reset_index()
        d["pdr"] = d.received / d.expected
        fig, ax = plt.subplots(figsize=(5, 3.5))
        for ph, col, lab in [("before", INK2, "before jamming"), ("during", MIT_COLORS[1], "during jamming (20 dBm)")]:
            dd = d[d.phase == ph]
            ax.plot(dd.dist_m + 25, dd.pdr, color=col, lw=2, marker="o", ms=5, label=lab)
        ax.set_xlabel("receiver distance to jammer (m)")
        ax.set_ylabel("beacon PDR")
        ax.set_ylim(0, 1.05)
        ax.set_title("Sanity check: PDR vs distance to jammer")
        ax.legend()
        save(fig, out, "sanity_pdr_vs_distance.png")


def routing(df, out):
    main = df[(df.preset == "main") & (df.mitigation >= 2) & (df.jammer != "none")]
    if main.empty:
        return
    scens = [s for s in SCEN_ORDER if s in set(main.scenario)]
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.5))
    for ax, (metric, lab) in zip(axes, [("no_relay_rate", "NO_RELAY rate"),
                                        ("path_switches_per_alert", "path switches per alert"),
                                        ("recovery_ms", "route recovery time (ms)")]):
        grouped_bars(ax, main, metric, scens, "scenario")
        ax.set_title(lab)
    axes[-1].legend(loc="upper left", bbox_to_anchor=(1.0, 1.0))
    save(fig, out, "routing_by_scenario.png")


def density(df, out):
    d = df[df.preset == "density"]
    if d.empty:
        return
    scens = [s for s in SCEN_ORDER if s in set(d.scenario)]
    fig, axes = plt.subplots(1, len(scens), figsize=(4.2 * len(scens), 3.5), sharey=True)
    axes = np.atleast_1d(axes)
    for ax, sc in zip(axes, scens):
        sub = d[d.scenario == sc]
        for mit in sorted(sub.mitigation.unique()):
            g = sub[sub.mitigation == mit].groupby("vehicles").ontime_ratio.agg(["mean", ci95]).reset_index()
            ax.errorbar(g.vehicles, g["mean"], yerr=g["ci95"], color=MIT_COLORS[mit], lw=2,
                        marker="o", ms=6, capsize=3, label=MIT_LABELS[mit])
        ax.set_title(f"{sc} (constant jammer)")
        ax.set_xlabel("vehicles on the road")
        ax.set_ylim(0, 1.05)
    axes[0].set_ylabel("on-time reachability")
    axes[-1].legend(loc="upper left", bbox_to_anchor=(1.0, 1.0))
    save(fig, out, "ontime_vs_density.png")


def rsu_effect(df, out):
    r = df[df.preset == "rsu"]
    if r.empty:
        return
    r = r.assign(rsu_label=r.rsu.map({0: "no RSU", 1: "with RSUs"}))
    scens = [s for s in SCEN_ORDER if s in set(r.scenario)]
    fig, axes = plt.subplots(1, len(scens), figsize=(4.2 * len(scens), 3.5), sharey=True)
    axes = np.atleast_1d(axes)
    for ax, sc in zip(axes, scens):
        grouped_bars(ax, r[r.scenario == sc], "ontime_ratio", ["no RSU", "with RSUs"], "rsu_label")
        ax.set_title(f"{sc} (constant jammer)")
        ax.set_ylim(0, 1.05)
    axes[0].set_ylabel("on-time reachability")
    axes[-1].legend(loc="upper left", bbox_to_anchor=(1.0, 1.0))
    save(fig, out, "rsu_effect.png")


def detector(df, out):
    d = df[df.preset.isin(["confounders", "main"]) & (df.mitigation == 0)]
    if d.empty:
        return
    d = d.assign(condition=np.where(d.confounder != "none", "conf: " + d.confounder, "jam: " + d.jammer))
    acc = d.groupby(["scenario", "condition"])[["det_tpr", "det_fpr", "det_delay_ms", "share_normal",
                                                "share_congested", "share_weak", "share_jammed"]].mean()
    acc.round(3).to_csv(os.path.join(out, "detector_accuracy.csv"))
    print("wrote", os.path.join(out, "detector_accuracy.csv"))

    st = acc.reset_index()
    labels = st.scenario + "\n" + st.condition
    fig, ax = plt.subplots(figsize=(max(6, 0.9 * len(st)), 3.8))
    bottom = np.zeros(len(st))
    for s, col in STATE_COLORS.items():
        vals = st[f"share_{s}"].fillna(0).values
        ax.bar(np.arange(len(st)), vals, 0.7, bottom=bottom, color=col, label=s,
               edgecolor="white", linewidth=1)
        bottom += vals
    ax.set_xticks(np.arange(len(st)))
    ax.set_xticklabels(labels, fontsize=8)
    ax.set_ylabel("share of detector samples")
    ax.set_title("What the detector concluded under each condition")
    ax.legend(loc="upper left", bbox_to_anchor=(1.0, 1.0))
    save(fig, out, "detector_states.png")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="results")
    args = ap.parse_args()
    f = os.path.join(args.results, "summary_runs.csv")
    if not os.path.exists(f):
        raise SystemExit(f"{f} not found - run analysis/metrics.py first")
    df = pd.read_csv(f, keep_default_na=False, na_values=[""])
    for c in ["mitigation", "vehicles", "rsu"]:
        df[c] = pd.to_numeric(df[c], errors="coerce")
    out = os.path.join(args.results, "figures")
    os.makedirs(out, exist_ok=True)
    style()

    sanity(df, args.results, out)
    by_config(df, "ontime_ratio", "on-time reachability", out, "ontime_by_config.png", (0, 1.05))
    by_config(df, "deadline_miss_ratio", "deadline-miss ratio", out, "deadline_miss_by_config.png", (0, 1.05))
    by_config(df, "alert_lat_ms", "alert latency (ms)", out, "latency_by_config.png")
    by_config(df, "beacon_pdr", "beacon PDR (t >= jamStart)", out, "beacon_pdr_by_config.png", (0, 1.05))
    routing(df, out)
    density(df, out)
    rsu_effect(df, out)
    detector(df, out)


if __name__ == "__main__":
    main()
