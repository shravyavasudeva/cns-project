#!/usr/bin/env python3
"""
run_batch.py - Run every experiment of the project with one command.

Usage (inside WSL / Linux, from this repository folder):
    python3 run_batch.py --ns3 ~/ns-3.47 --preset smoke
    python3 run_batch.py --ns3 ~/ns-3.47 --preset main --jobs 4
    python3 run_batch.py --ns3 ~/ns-3.47 --preset all  --jobs 4
    python3 run_batch.py --ns3 ~/ns-3.47 --preset main --dry-run   (just print the commands)

Presets:
    smoke        1 short run to check that the build works            (~1 min)
    sanity       PDR vs jammer power (does the jammer really hurt?)   (12 runs)
    main         3 scenarios x 3 jammer types x 4 mitigations x 5 seeds (180 runs)
    density      vehicle density sweep per scenario, constant jammer   (108 runs)
    confounders  congestion / weak channel / interference, no jammer   (36 runs)
    rsu          with vs without RSUs, constant jammer                 (24 runs)
    all          everything above except smoke

Each run writes into  <out>/<preset>/<run-name>/ . Finished runs (node_summary.csv exists)
are skipped, so you can stop the script and start it again later.
After the runs it calls analysis/metrics.py to build results/summary_runs.csv.
"""
import argparse
import glob
import itertools
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))

SCENARIOS = ["highway", "local", "urban"]
MITIGATIONS = [0, 1, 2, 3]


def grid(**axes):
    """Cartesian product of the given axes -> list of flag dicts."""
    keys = list(axes)
    return [dict(zip(keys, vals)) for vals in itertools.product(*(axes[k] for k in keys))]


def preset_runs(name):
    if name == "smoke":
        return [dict(scenario="highway", jammer="constant", mitigation=3, vehicles=20,
                     simTime=15, alertStart=11, seed=1)]
    if name == "sanity":
        runs = grid(scenario=["highway"], jammer=["constant"], mitigation=[0],
                    jamPower=[-10, 0, 10, 20, 30], seed=[1, 2], simTime=[30])
        runs += grid(scenario=["highway"], jammer=["none"], mitigation=[0], seed=[1, 2], simTime=[30])
        return runs
    if name == "main":
        return grid(scenario=SCENARIOS, jammer=["none", "constant", "reactive"],
                    mitigation=MITIGATIONS, seed=[1, 2, 3, 4, 5])
    if name == "density":
        dens = {"highway": [30, 60, 120], "local": [6, 12, 24], "urban": [20, 40, 80]}
        runs = []
        for sc, vs in dens.items():
            runs += grid(scenario=[sc], vehicles=vs, jammer=["constant"],
                         mitigation=MITIGATIONS, seed=[1, 2, 3])
        return runs
    if name == "confounders":
        return grid(scenario=["highway", "local"], jammer=["none"],
                    confounder=["congestion", "weak", "interference"],
                    mitigation=[0, 3], seed=[1, 2, 3])
    if name == "rsu":
        return grid(scenario=["highway", "local"], rsu=[0, 1], jammer=["constant"],
                    mitigation=[2, 3], seed=[1, 2, 3])
    raise SystemExit(f"unknown preset {name}")


def run_name(flags):
    order = ["scenario", "jammer", "confounder", "mitigation", "vehicles", "rsu", "jamPower", "seed"]
    short = {"scenario": "", "jammer": "jam-", "confounder": "conf-", "mitigation": "m",
             "vehicles": "n", "rsu": "rsu", "jamPower": "p", "seed": "s"}
    parts = [f"{short[k]}{flags[k]}" for k in order if k in flags]
    return "_".join(parts)


def find_binary(ns3_dir):
    pats = [os.path.join(ns3_dir, "build", "scratch", "v2v-jam", "*v2v-jam*")]
    for pat in pats:
        for f in sorted(glob.glob(pat)):
            if os.path.isfile(f) and os.access(f, os.X_OK):
                return f
    raise SystemExit(
        "Could not find the v2v-jam program in build/scratch/v2v-jam/.\n"
        "Did you copy scratch/v2v-jam into ns-3 and run ./ns3 build ?  (see README.md)")


def run_one(binary, ns3_dir, flags, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    cmd = [binary] + [f"--{k}={v}" for k, v in flags.items()] + [f"--outDir={out_dir}"]
    env = dict(os.environ)
    lib = os.path.join(ns3_dir, "build", "lib")
    env["LD_LIBRARY_PATH"] = lib + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    env["DYLD_LIBRARY_PATH"] = lib + os.pathsep + env.get("DYLD_LIBRARY_PATH", "")
    t0 = time.time()
    with open(os.path.join(out_dir, "stdout.txt"), "w") as log:
        rc = subprocess.call(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
    return rc, time.time() - t0, cmd


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ns3", required=True, help="path to the ns-3.47 folder (contains ./ns3)")
    ap.add_argument("--preset", default="smoke",
                    choices=["smoke", "sanity", "main", "density", "confounders", "rsu", "all"])
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2),
                    help="runs in parallel (default: half your CPU cores)")
    ap.add_argument("--out", default=os.path.join(HERE, "results"), help="results folder")
    ap.add_argument("--simTime", type=float, default=None, help="override simulation time (s)")
    ap.add_argument("--dry-run", action="store_true", help="print the commands only")
    ap.add_argument("--no-metrics", action="store_true", help="do not run analysis/metrics.py at the end")
    args = ap.parse_args()

    ns3_dir = os.path.abspath(os.path.expanduser(args.ns3))
    presets = ["sanity", "main", "density", "confounders", "rsu"] if args.preset == "all" else [args.preset]

    jobs = []
    for pr in presets:
        for flags in preset_runs(pr):
            if args.simTime is not None:
                flags["simTime"] = args.simTime
            out_dir = os.path.join(os.path.abspath(args.out), pr, run_name(flags))
            jobs.append((pr, flags, out_dir))

    todo = [j for j in jobs if not os.path.exists(os.path.join(j[2], "node_summary.csv"))]
    print(f"{len(jobs)} runs in preset(s) {presets}, {len(jobs) - len(todo)} already done, {len(todo)} to run")

    if args.dry_run:
        for pr, flags, out_dir in todo:
            fl = " ".join(f"--{k}={v}" for k, v in flags.items())
            print(f'./ns3 run "v2v-jam {fl} --outDir={out_dir}"')
        return

    binary = find_binary(ns3_dir) if todo else None
    failed = 0
    t_start = time.time()
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = {pool.submit(run_one, binary, ns3_dir, flags, out_dir): (pr, flags, out_dir)
                for pr, flags, out_dir in todo}
        for i, fut in enumerate(as_completed(futs), 1):
            pr, flags, out_dir = futs[fut]
            rc, dt, cmd = fut.result()
            status = "ok" if rc == 0 else f"FAILED (exit {rc}, see {out_dir}/stdout.txt)"
            failed += rc != 0
            print(f"[{i}/{len(todo)}] {pr}/{os.path.basename(out_dir)}  {dt:.0f}s  {status}", flush=True)
    print(f"finished in {(time.time() - t_start) / 60:.1f} min, {failed} failed")

    if not args.no_metrics:
        subprocess.call([sys.executable, os.path.join(HERE, "analysis", "metrics.py"),
                         "--results", os.path.abspath(args.out)])


if __name__ == "__main__":
    main()
