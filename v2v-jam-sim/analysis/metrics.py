#!/usr/bin/env python3
"""
metrics.py - Turn the raw simulation outputs into the project's metrics.

Usage:
    python3 analysis/metrics.py --results results            (all runs below results/)
    python3 analysis/metrics.py --run results/main/highway_jam-constant_m3_s1   (one run, printed)

Writes:
    results/summary_runs.csv     one row per run (every metric below)
    results/summary_grouped.csv  mean, std and 95% CI over seeds for each configuration

Metric definitions (all computed from events.csv / beacons.csv / node_summary.csv):
  Safety alerts (targets = vehicles within alertRadius of the source when the alert is created)
    reach_ratio          targets that received the alert at all / targets
    ontime_ratio         targets that received it within the deadline / targets   (= reachability)
    deadline_miss_ratio  1 - ontime_ratio  (late OR never)
    stale_accept_ratio   targets that accepted an alert older than its deadline / targets
                         (only possible without Layer 4; L4 drops stale copies instead)
    alert_lat_ms         mean latency of received alerts, alert_lat_p95_ms, alert_jitter_ms (std)
    via_alt_ratio        on-time receptions whose first copy came via an RSU or relay
  Layer 3 routing (alerts whose source started a recovery)
    recovery_alerts, reroutes, no_relay_rate, relay_ok_rate, recovery_ms,
    route_breaks_per_alert, path_switches_per_alert
    alert_tx_per_alert   radio transmissions of alert frames per alert (all copies)
    routing_tx_per_alert extra transmissions caused by L3 (unicast to relay + re-broadcasts)
  Detection (Layer 1/2), ground truth = jammer power at the node >= gtJamDbm
    det_tpr, det_fpr, det_delay_ms, share_normal/congested/weak/jammed, jam_suspected_at_alert
  Beacons / connectivity (during the jamming window, t >= jamStart)
    beacon_pdr, beacon_lat_ms, beacon_jitter_ms, goodput_kbps, interrupt_ms (mean per vehicle),
    max_gap_ms (mean per vehicle)
  All times in output CSVs are milliseconds (columns end in _ms).
"""
import argparse
import json
import math
import os
import sys

import numpy as np
import pandas as pd

GROUP_KEYS = ["preset", "scenario", "jammer", "confounder", "mitigation", "vehicles", "rsu", "jamPower"]


def parse_extra(s):
    out = {}
    if not isinstance(s, str) or not s:
        return out
    for kv in s.split(";"):
        if "=" in kv:
            k, v = kv.split("=", 1)
            out[k] = v
    return out


def extra_col(df, key, num=True):
    vals = df["extra"].map(lambda s: parse_extra(s).get(key))
    return pd.to_numeric(vals, errors="coerce") if num else vals


def safe_div(a, b):
    return float(a) / b if b else float("nan")


def run_metrics(run_dir):
    with open(os.path.join(run_dir, "params.json")) as f:
        prm = json.load(f)
    ev = pd.read_csv(os.path.join(run_dir, "events.csv"), keep_default_na=False,
                     dtype={"extra": str, "event": str})
    deadline = prm["deadlineMs"]
    m = {k: prm.get(k) for k in ["scenario", "jammer", "confounder", "mitigation", "vehicles",
                                  "rsu", "jamPower", "seed", "speed", "simTime"]}
    m["run_dir"] = run_dir

    # ------------------------------- alerts -------------------------------
    tgt = ev[ev.event == "ALERT_TARGET"][["node", "seq"]]
    rx = ev[ev.event == "RX"].copy()
    rx["lat_ms"] = extra_col(rx, "lat_ms")
    rx["path"] = extra_col(rx, "path", num=False)
    merged = tgt.merge(rx[["node", "seq", "lat_ms", "path"]], on=["node", "seq"], how="left")
    n_t = len(merged)
    reached = merged.lat_ms.notna()
    ontime = reached & (merged.lat_ms <= deadline)
    m["alerts"] = int((ev.event == "ALERT_GEN").sum())
    m["targets"] = n_t
    m["reach_ratio"] = safe_div(reached.sum(), n_t)
    m["ontime_ratio"] = safe_div(ontime.sum(), n_t)
    m["deadline_miss_ratio"] = 1 - m["ontime_ratio"] if n_t else float("nan")
    m["stale_accept_ratio"] = safe_div((reached & ~ontime).sum(), n_t)
    lat = merged.lat_ms[reached]
    m["alert_lat_ms"] = lat.mean() if len(lat) else float("nan")
    m["alert_lat_p95_ms"] = lat.quantile(0.95) if len(lat) else float("nan")
    m["alert_jitter_ms"] = lat.std() if len(lat) > 1 else float("nan")
    m["via_alt_ratio"] = safe_div((ontime & merged.path.isin(["RSU", "RELAY"])).sum(), ontime.sum())

    gen = ev[ev.event == "ALERT_GEN"]
    m["jam_suspected_at_alert"] = extra_col(gen, "jam_suspected").mean() if len(gen) else float("nan")

    # ------------------------------ routing -------------------------------
    route_ev = ev[ev.event.isin(["REROUTE", "NO_RELAY"])]
    rec_alerts = route_ev.seq.unique()
    n_rec = len(rec_alerts)
    first = route_ev.sort_values("time_ms").groupby("seq").first()
    m["recovery_alerts"] = n_rec
    m["reroutes"] = int((ev.event == "REROUTE").sum())
    m["no_relay_rate"] = safe_div((first.event == "NO_RELAY").sum(), n_rec)
    ok = ev[ev.event == "RELAY_OK"]
    m["relay_ok_rate"] = safe_div(ok.seq.nunique(), n_rec)
    m["recovery_ms"] = extra_col(ok, "rec_ms").mean() if len(ok) else float("nan")
    m["route_breaks_per_alert"] = safe_div((ev.event == "ROUTE_BREAK").sum(), n_rec)
    m["path_switches_per_alert"] = safe_div((ev.event == "PATH_SWITCH").sum(), n_rec)
    tx = ev[ev.event == "TX"].copy()
    tx["path"] = extra_col(tx, "path", num=False)
    m["alert_tx_per_alert"] = safe_div(len(tx), m["alerts"])
    m["routing_tx_per_alert"] = safe_div((tx.path != "DIRECT").sum(), m["alerts"])
    m["drop_deadline"] = int((ev.event == "DROP_DEADLINE").sum())

    # ------------------------------ detection -----------------------------
    ns = pd.read_csv(os.path.join(run_dir, "node_summary.csv"))
    tp, fn, fp, tn = ns.tp.sum(), ns.fn.sum(), ns.fp.sum(), ns.tn.sum()
    m["det_tpr"] = safe_div(tp, tp + fn)
    m["det_fpr"] = safe_div(fp, fp + tn)
    d = ns[(ns.first_gt_ms >= 0) & (ns.first_detect_ms >= 0)]
    m["det_delay_ms"] = (d.first_detect_ms - d.first_gt_ms).mean() if len(d) else float("nan")
    tot = ns[["n_normal", "n_congested", "n_weak", "n_jammed"]].sum().sum()
    for s in ["normal", "congested", "weak", "jammed"]:
        m[f"share_{s}"] = safe_div(ns[f"n_{s}"].sum(), tot)

    # ---------------------------- beacons ---------------------------------
    b = pd.read_csv(os.path.join(run_dir, "beacons.csv"))
    bj = b[b.time_ms >= prm["jamStart"] * 1000]
    m["beacon_pdr"] = safe_div(bj.received.sum(), bj.expected.sum())
    w = bj.received
    m["beacon_lat_ms"] = safe_div((bj.lat_ms * w).sum(), w.sum())
    m["beacon_jitter_ms"] = bj.jitter_ms[bj.received > 0].mean() if len(bj) else float("nan")
    m["goodput_kbps"] = bj.goodput_kbps.mean() if len(bj) else float("nan")
    veh = ns[ns.kind == "vehicle"]
    m["interrupt_ms"] = veh.interrupt_ms.mean()
    m["max_gap_ms"] = veh.max_gap_ms.mean()
    return m


def find_runs(root):
    for dirpath, _, files in os.walk(root):
        if "params.json" in files and "node_summary.csv" in files:
            yield dirpath


def ci95(x):
    x = x.dropna()
    return 1.96 * x.std(ddof=1) / math.sqrt(len(x)) if len(x) > 1 else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="results", help="folder containing run folders")
    ap.add_argument("--run", help="compute and print the metrics of one run folder")
    args = ap.parse_args()

    if args.run:
        for k, v in run_metrics(args.run).items():
            print(f"{k:26s} {v}")
        return

    rows = []
    for rd in sorted(find_runs(args.results)):
        try:
            r = run_metrics(rd)
            r["preset"] = os.path.relpath(rd, args.results).split(os.sep)[0]
            rows.append(r)
        except Exception as e:  # keep going if one run is broken
            print(f"skipping {rd}: {e}", file=sys.stderr)
    if not rows:
        sys.exit(f"no finished runs found under {args.results}")
    df = pd.DataFrame(rows)
    out_runs = os.path.join(args.results, "summary_runs.csv")
    df.to_csv(out_runs, index=False)

    metric_cols = [c for c in df.columns if c not in GROUP_KEYS + ["seed", "run_dir", "speed", "simTime"]
                   and pd.api.types.is_numeric_dtype(df[c])]
    g = df.groupby(GROUP_KEYS, dropna=False)[metric_cols]
    agg = g.mean().add_suffix("_mean").join(g.std().add_suffix("_std")).join(
        g.agg(ci95).add_suffix("_ci95")).join(g.size().rename("n_seeds"))
    out_grp = os.path.join(args.results, "summary_grouped.csv")
    agg.reset_index().to_csv(out_grp, index=False)

    print(f"{len(df)} runs -> {out_runs}\n{len(agg)} configurations -> {out_grp}\n")
    show = ["ontime_ratio", "deadline_miss_ratio", "alert_lat_ms", "no_relay_rate", "beacon_pdr", "det_tpr", "det_fpr"]
    pd.set_option("display.width", 200)
    print(df.groupby(["preset", "scenario", "jammer", "confounder", "mitigation"])[show].mean().round(3))


if __name__ == "__main__":
    main()
