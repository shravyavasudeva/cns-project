# VANET jamming-mitigation simulation

An ns-3.47 simulation project evaluating whether jamming detection, alternate-path forwarding, and deadline-aware delivery improve the timely delivery of emergency safety alerts.

## Project layout

- `scratch/vanet-jamming.cc` — buildable baseline: vehicles, RSU, an interference source, periodic safety traffic, and a single emergency alert.
- `scripts/run-baseline.sh` — runs the scenario against a local ns-3 checkout.
- `docs/TEAM-WORK.md` — ownership boundaries and collaboration rules.
- `results/` — ignored local output directory for traces and metrics.

## Prerequisites

This project targets **ns-3.47**. Each contributor needs a configured ns-3 checkout; it is intentionally not committed to this repository.

Set `NS3_DIR` to that checkout. On the current development machine:

```bash
export NS3_DIR=/home/shrav/ns-3.47
```

## Run the baseline

```bash
./scripts/run-baseline.sh
```

Or run it directly:

```bash
cp scratch/vanet-jamming.cc "$NS3_DIR/scratch/"
cd "$NS3_DIR"
./ns3 run "scratch/vanet-jamming --vehicles=15 --jammer=true"
```

The baseline is deliberately small. It provides the shared starting point; feature work should be added in focused branches:

1. Mobility/scenario: replace fixed positions with highway and local-road mobility.
2. Jammer: refine the interference model and add reactive behavior.
3. Detection: sliding-window PDR/SINR detection and logs.
4. Rerouting: RSU/relay forwarding after a jam flag.
5. Deadline/metrics: priority handling, stale-alert drops, CSV metrics and plots.

## Team workflow

```bash
git clone <YOUR-REMOTE-URL>
cd vanet-jamming-mitigation
git switch -c feature/detection
# edit, test, commit, push
git push -u origin feature/detection
```

Keep changes reviewable: one feature per branch, no committed trace files, and document any added command-line option in this README.

## Publish the repository

Create an empty repository on GitHub/GitLab, then from this directory run:

```bash
git remote add origin <YOUR-REMOTE-URL>
git push -u origin main
```
