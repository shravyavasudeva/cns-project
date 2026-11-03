# Cross-Layer Resilience for Jammed V2V Safety Communication

**ECE 6612 Project, Fall 2026.** Ragamayee Kothapalli, Shravya Vasudeva, Ankitha Kottamthody, Aashish Rahate, Veena Tharini Rajesh

This is an ns-3 simulation of vehicles exchanging safety messages over IEEE 802.11p while an RF jammer attacks the channel. It asks one question: **does adding Layer 3 rerouting (via an RSU or relay vehicle) and Layer 4 deadline/priority handling on top of PHY/MAC jamming detection get emergency alerts to nearby vehicles in time, and how does the answer change between a highway, a local road and an urban intersection?**

> **Status (Nov 3, 2026):** built and tested on **ns-3.40** (macOS, Apple clang 14). It builds with no errors or warnings, and the smoke, sanity and per-configuration check runs complete with sensible results. It has **not been built on ns-3.47 yet**. All APIs used were checked against the 3.47 source, so it should build, but report any compile error (see [Troubleshooting](#troubleshooting)).
>
> **ns-3.40 only:** 3.40 has a bug that crashes any 10 MHz (802.11p) spectrum PHY (`interference-helper.cc line 337`). ns-3.47 fixed it. If you must use 3.40, apply the one-line fix described in [Troubleshooting](#troubleshooting).

---

## Test results so far

Measured on ns-3.40 with the default settings (highway unless noted), **one seed per run**. Treat these as a first look, not final results.

| Check | Result |
|---|---|
| Build | Clean: no errors or warnings |
| Jammer sanity (`sanity` preset) | Beacon PDR falls from 1.00 to 0.50 as jammer power rises from −10 to 30 dBm. Near the jammer (< 50 m) PDR is 0.08, and beyond about 600 m it is unaffected. ✅ The jammer really hurts |
| Detector, constant jammer | Catches 96% of truly-jammed samples (TPR 0.96), 2% false alarms (FPR 0.02) |
| Detector, reactive jammer | TPR 0.26: harder to detect (stealthier attack) |
| Detector, confounders | Congestion, weak channel, interference: false alarms ≤ 0.5%. Correctly labelled CONGESTED / WEAK |
| On-time alert reachability, constant jammer, mitigation 0 → 1 → 2 → 3 | Highway 0.50 → 0.53 → 0.52 → **0.56**. Local road 0.55 → 0.55 → 0.55 → **0.56** |
| Layer 3 on the local road | No relay or RSU available for **89%** of alerts (mitigation 2). Sparse roads leave little to reroute through |
| Layer 4 | Stale alerts accepted: 3.5% without L4 → **0%** with L4 |
| Runtime | 10–25 s per default highway run on a laptop (congestion runs about 45 s) |

**Things to watch / discuss in the report:**
- **Mitigation gains are modest.** Vehicles deep inside the jammer's range cannot decode anything, whoever sends the alert. Rerouting only helps vehicles near the edge of the jammed area. Check whether this holds across seeds, densities and jammer positions.
- **L4 slows down detection.** At mitigation 3, L4 lowers the beacon rate to 2 Hz while jamming, so the detector has fewer frames to judge from. On the local road its TPR dropped from 0.97 to 0.39. Consider testing `--beaconHzJam=5` or `10` to measure this trade-off.
- **Two code changes from testing are already included:**
  - The detector separates jamming from congestion by *CCA_BUSY-only* time (non-frame energy) plus an elevated noise floor.
  - Beacons advertise only a node's *own* jamming detection. Earlier, suspicion spread node to node and caused network-wide false alarms.

## Next steps

1. **Build on ns-3.47** (§3) and run the smoke test. Report any compile error. The code was checked against the 3.47 source but has only been compiled on 3.40.
2. **Run `sanity`** and confirm the two sanity plots look like the results above.
3. **Run `--preset all`** (360 runs). You can split presets across laptops and merge the `results/` folders (§5).
4. **Calibrate the detector** (§10), then re-run the presets if the thresholds change.
5. **Make the figures** (`analysis/plots.py`) and write the report sections.

## Contents
- [Test results so far](#test-results-so-far) · [Next steps](#next-steps)
1. [What the simulation does](#1-what-the-simulation-does)
2. [Repository layout](#2-repository-layout)
3. [Setup on Windows (WSL2 + Ubuntu 22.04)](#3-setup-on-windows-wsl2--ubuntu-2204)
4. [Running one simulation](#4-running-one-simulation)
5. [Running all experiments](#5-running-all-experiments)
6. [Metrics and plots](#6-metrics-and-plots)
7. [How the code works (module by module)](#7-how-the-code-works-module-by-module)
8. [All command-line flags](#8-all-command-line-flags)
9. [Output files](#9-output-files)
10. [Calibrating the detector](#10-calibrating-the-detector)
11. [Modelling assumptions and limitations](#11-modelling-assumptions-and-limitations)
12. [Troubleshooting](#troubleshooting)

---

## 1. What the simulation does

### Features at a glance

| Area | Feature |
|---|---|
| Radio | IEEE 802.11p, channel 178 (5.890 GHz, 10 MHz), 6 Mb/s, log-distance path loss (+ optional Nakagami fading), spectrum-based PHY so the jammer is real interference |
| Scenarios | **highway** (2 km, 6 lanes, 30 m/s, 60 vehicles, RSUs every 500 m), **local** (1 km, 2 lanes, 12 m/s, 12 vehicles, no RSU), **urban** (two crossing 1 km streets, 10 m/s, 40 vehicles, RSU at the junction, higher path loss) |
| Attack | **Constant jammer** (continuous noise), **reactive jammer** (senses a frame, then jams it after 20 µs) |
| Confounders | **congestion** (5× beacon rate, bigger beacons), **weak channel** (10 dBm TX, harsher path loss), **interference** (non-malicious periodic interferer) |
| Safety app | 10 Hz beacons on every vehicle and RSU. One emergency alert per second from a vehicle near the jammer, with a 100 ms deadline |
| L1/2 detection | Busy ratio, decode-error ratio, neighbour PDR, noise+interference level and RSSI over a 0.5 s window. Each node is classified NORMAL / CONGESTED / WEAK / JAMMED, with hysteresis. Jammed links are also detected (neighbours that went silent) |
| L3 rerouting | Neighbour table from beacons. When jamming is suspected: **RSU first**, then **best relay vehicle** (scored on link quality, distance from the jammed area and predicted link lifetime), otherwise **NO_RELAY**. Implicit ACK, route-break detection, path switching. RSU backhaul to neighbouring RSUs |
| L4 handling | Priority queue (EMERGENCY before ROUTINE). Messages are released to the radio only when it is idle. Stale messages are dropped at the sender, relay and receiver. Beacon rate drops to 2 Hz while jamming is suspected |
| Ground truth | Every 0.1 s, each node is labelled "jammed" if the jammer power there is ≥ −92 dBm. Used to score the detector (TPR, FPR, delay) |
| Metrics | Reachability, deadline-miss ratio, stale-accept ratio, latency/p95/jitter, NO_RELAY rate, relay success, route recovery time, route breaks, path switches, routing overhead, beacon PDR, goodput, connection interruption time, detector accuracy |
| Tooling | One program with flags, a batch runner with 6 presets (360 runs), metrics and plot scripts, a GIF demo animation, and an optional NetAnim trace |

### The four mitigation configurations (`--mitigation`)
Each level adds to the one before it, so the results show what each layer contributes:

| Value | Name | Behaviour |
|---|---|---|
| `0` | (a) no mitigation | The alert is broadcast once. Everything goes straight into the MAC queue |
| `1` | (b) detection | The detector runs. If the source suspects jamming, it re-broadcasts the alert every 20 ms until the deadline |
| `2` | (c) + L3 | Also hands the alert to an RSU or relay vehicle, which re-broadcasts it |
| `3` | (d) + L3 + L4 | Also adds the priority queue, stale-message dropping and beacon-rate reduction |

The detector **always runs and logs**, even at level 0, so detection accuracy is measured in every run.

### How it covers the proposal and the professor's feedback
- **Proposal configurations** (a) no mitigation, (b) detection only and (c) detection + L3 + L4 map to `--mitigation=0`, `1` and `3`. Level `2` is extra, so L3 and L4 can be evaluated separately.
- **Professor's feedback** ("highway: many vehicles but fast; local road: few relays"):
  - The highway, local and urban scenarios have different densities, speeds and RSU availability.
  - The `density` preset sweeps the number of vehicles, and the `rsu` preset compares runs with and without RSUs.
  - On the highway, relay choice uses **predicted link lifetime** (relative speed), and route breaks and path switches are measured.
  - On the local road, `NO_RELAY` events measure how often no relay exists.

---

## 2. Repository layout

```
v2v-jam-sim/                  (folder inside the cns-project repo)
├── README.md                 ← this file
├── CLAUDE.md                 ← guidance for Claude Code when editing this repo
├── requirements.txt          ← Python packages for analysis
├── run_batch.py              ← runs all experiments (presets)
├── scripts/
│   ├── setup_ns3.sh          ← one-time: install ns-3.47, copy code, build, smoke test
│   └── sync_code.sh          ← after every code change: copy code into ns-3 and rebuild
├── scratch/v2v-jam/          ← THE SIMULATION (C++), copied into ns-3.47/scratch/v2v-jam/
│   ├── v2v-jam.cc            ← main(): flags, nodes, WiFi, wiring, alert scheduler, ground truth
│   ├── common.h/.cc          ← Params (all settings), V2vHeader (packet header), EventLog, node registry
│   ├── scenario.h/.cc        ← highway / local / urban roads, mobility, wrap-around, RSU positions
│   ├── jammer.h/.cc          ← constant jammer, reactive jammer, non-malicious interferer
│   ├── detector.h/.cc        ← Layer 1/2 jamming detector
│   ├── neighbor-table.h      ← neighbour table built from beacons
│   ├── relay.h/.cc           ← Layer 3 RSU/relay rerouting (Router)
│   ├── safety-app.h/.cc      ← beacons, alerts, Layer 4 queue, relay/RSU forwarding
│   └── stats.h/.cc           ← beacon PDR / latency / jitter / goodput counters
└── analysis/
    ├── metrics.py            ← logs → summary_runs.csv, summary_grouped.csv
    ├── plots.py              ← summary → figures/*.png, detector_accuracy.csv
    └── animate.py            ← one run → demo GIF
```

---

## 3. Setup on Windows (WSL2 + Ubuntu 22.04)

ns-3 does not run natively on Windows in a practical way. Everyone uses **WSL2**, a real Ubuntu Linux inside Windows. All commands below run in the **Ubuntu terminal**, not PowerShell, except step 3.1.

### 3.1 Install WSL2 with Ubuntu 22.04 (once per laptop)
Open **PowerShell as Administrator** and run:
```powershell
wsl --install -d Ubuntu-22.04
```
Restart when asked. Then open **"Ubuntu 22.04"** from the Start menu and choose a Linux username and password (you'll need the password for `sudo`).

> Ubuntu **22.04 or 24.04** both work: ns-3.47 needs g++ ≥ 11.1 and CMake ≥ 3.20. Give WSL at least 8 GB RAM. The first build is heavy.

### 3.2 Get the project into WSL
This project lives in the team repo [`shravyavasudeva/cns-project`](https://github.com/shravyavasudeva/cns-project), in the folder **`v2v-jam-sim/`** on branch **`feature/full-simulation`** (until it is merged into `main`). Clone it **inside the Linux file system** (e.g. `~/cns-project`), not under `/mnt/c/...`, because building and running from `/mnt/c` is 5–10× slower.
```bash
git clone https://github.com/shravyavasudeva/cns-project.git ~/cns-project
cd ~/cns-project
git switch feature/full-simulation     # not needed once it is merged into main
cd v2v-jam-sim                         # all commands below run from here
```

### 3.3 Install ns-3.47 and build (one command, 10–30 min)

> **Already have ns-3.47 installed?** Skip the script. Copy our folder into its `scratch/` and build:
> ```bash
> cp -r ~/cns-project/v2v-jam-sim/scratch/v2v-jam <your-ns-3.47-folder>/scratch/
> cd <your-ns-3.47-folder> && ./ns3 configure --build-profile=optimized --disable-werror && ./ns3 build
> ```
> (With `ns-allinone-3.47`, the folder is `ns-allinone-3.47/ns-3.47`.) Then pass that folder to `run_batch.py --ns3 ...` and `scripts/sync_code.sh ...`.

```bash
bash scripts/setup_ns3.sh
```
This script:
1. installs `g++ cmake ninja python3 …`;
2. downloads **ns-3.47** to `~/ns-3.47`;
3. copies `scratch/v2v-jam` into `~/ns-3.47/scratch/v2v-jam`;
4. configures an **optimized** build with only the needed modules (`core network mobility propagation spectrum wifi netanim`) and builds it;
5. runs a 15-second smoke test that writes into `~/ns-3.47/results/smoke/`.

If you prefer to do it by hand:
```bash
cd ~
wget https://gitlab.com/nsnam/ns-3-dev/-/archive/ns-3.47/ns-3-dev-ns-3.47.tar.gz
tar xzf ns-3-dev-ns-3.47.tar.gz && mv ns-3-dev-ns-3.47 ns-3.47
cp -r ~/cns-project/v2v-jam-sim/scratch/v2v-jam ~/ns-3.47/scratch/
cd ~/ns-3.47
./ns3 configure --build-profile=optimized --disable-werror \
     --enable-modules="core;network;mobility;propagation;spectrum;wifi;netanim"
./ns3 build
```

### 3.4 Python packages for the analysis
```bash
cd ~/cns-project/v2v-jam-sim
pip3 install -r requirements.txt
```

### 3.5 After changing the C++ code
ns-3 compiles the **copy** inside `~/ns-3.47/scratch/v2v-jam`. After editing (or `git pull`), sync and rebuild:
```bash
bash scripts/sync_code.sh
```

> **Debug build:** if the program crashes, rebuild with asserts and logging enabled:
> `./ns3 configure --build-profile=debug --disable-werror --enable-modules="core;network;mobility;propagation;spectrum;wifi;netanim" && ./ns3 build`.
> This runs about 3× slower. Switch back to `optimized` before the big batch.

---

## 4. Running one simulation

All `./ns3 run` commands run **from `~/ns-3.47`**. Relative `--outDir` paths are relative to that folder.

```bash
cd ~/ns-3.47
./ns3 run "v2v-jam --PrintHelp"                      # every flag with its default

# Highway, constant jammer, full mitigation, seed 1
./ns3 run "v2v-jam --scenario=highway --jammer=constant --mitigation=3 --seed=1 --outDir=results/test1"

# Local road, reactive jammer, no mitigation
./ns3 run "v2v-jam --scenario=local --jammer=reactive --mitigation=0 --outDir=results/test2"

# No jammer, congested channel (detector false-positive test)
./ns3 run "v2v-jam --scenario=highway --confounder=congestion --mitigation=0 --outDir=results/test3"

# Demo run with positions for the animation
./ns3 run "v2v-jam --scenario=highway --jammer=constant --mitigation=3 --posLog=1 --outDir=results/demo"
python3 ~/cns-project/v2v-jam-sim/analysis/animate.py --run results/demo --out ~/demo.gif

# Metrics of one run
python3 ~/cns-project/v2v-jam-sim/analysis/metrics.py --run results/test1
```

The default run lasts 40 simulated seconds:
- 0–10 s: warm-up, no jamming.
- 10 s: the jammer turns on.
- 12–39 s: one emergency alert per second, each with a random 0–200 ms offset so alerts don't line up with periodic signals. The offsets are the same for every mitigation level.

Measured on a 2019-era Mac (optimized build): a default highway run takes 10–25 s of real time, and congestion runs about 45 s. To try things quickly, use `--simTime=20 --vehicles=20`.

---

## 5. Running all experiments

```bash
cd ~/cns-project/v2v-jam-sim
python3 run_batch.py --ns3 ~/ns-3.47 --preset smoke            # 1 run, checks everything works
python3 run_batch.py --ns3 ~/ns-3.47 --preset sanity --jobs 4  # do this FIRST (see below)
python3 run_batch.py --ns3 ~/ns-3.47 --preset main   --jobs 4
python3 run_batch.py --ns3 ~/ns-3.47 --preset all    --jobs 4  # everything (360 runs)
python3 run_batch.py --ns3 ~/ns-3.47 --preset main --dry-run   # only print the commands
```

| Preset | What it varies | Runs |
|---|---|---|
| `smoke` | one 15 s highway run | 1 |
| `sanity` | jammer power −10…30 dBm vs no jammer (highway, mitigation 0) | 12 |
| `main` | scenario {highway, local, urban} × jammer {none, constant, reactive} × mitigation {0,1,2,3} × 5 seeds | 180 |
| `density` | vehicles (highway 30/60/120, local 6/12/24, urban 20/40/80) × mitigation × 3 seeds, constant jammer | 108 |
| `confounders` | {congestion, weak, interference} × {highway, local} × mitigation {0,3} × 3 seeds, no jammer | 36 |
| `rsu` | RSU {0,1} × {highway, local} × mitigation {2,3} × 3 seeds, constant jammer | 24 |

- Results go to `results/<preset>/<run-name>/`.
- **Finished runs are skipped**, so you can stop with Ctrl+C and restart later.
- `--jobs` sets how many runs go in parallel. Use about half your CPU cores, and watch RAM.
- To split the work across teammates' laptops, give each person a different preset, then copy every `results/<preset>/` folder into one `results/` folder before running the metrics.

> ⚠️ **Run `sanity` first and look at `sanity_pdr_vs_power.png`.** PDR must clearly drop as jammer power rises. If it doesn't, nothing else means anything: adjust `--jamPower` or the jammer position before the big batch.

---

## 6. Metrics and plots

```bash
python3 analysis/metrics.py --results results    # → results/summary_runs.csv, summary_grouped.csv
python3 analysis/plots.py   --results results    # → results/figures/*.png, detector_accuracy.csv
```
(`run_batch.py` runs `metrics.py` automatically at the end.)

### Metric definitions

| Metric | Definition |
|---|---|
| **ontime_ratio** (reachability) | targets that got the alert within the deadline ÷ targets. *Targets* = vehicles within 200 m of the source when the alert is created |
| **deadline_miss_ratio** | 1 − ontime_ratio (late or never) |
| reach_ratio | targets that got the alert at all (even late) ÷ targets |
| **stale_accept_ratio** | targets that accepted an alert older than its deadline. This is the safety hazard L4 removes |
| alert_lat_ms / p95 / jitter | latency of received alerts: mean, 95th percentile, standard deviation |
| via_alt_ratio | on-time receptions whose first copy came via an RSU or relay |
| **no_relay_rate** | alerts where L3 found no RSU or relay ÷ alerts that started recovery |
| relay_ok_rate | alerts where the source overheard the relay's re-broadcast |
| **recovery_ms** | time from starting recovery to overhearing the relay (route recovery time) |
| route_breaks / path_switches per alert | relay not overheard in 15 ms → break → switch to the next candidate |
| alert_tx_per_alert, routing_tx_per_alert | radio transmissions per alert, total and L3 extra (routing overhead) |
| **beacon_pdr** | beacons received ÷ beacons expected (receivers within 200 m), for t ≥ jamStart |
| beacon_lat_ms, beacon_jitter_ms, goodput_kbps | beacon latency, mean \|Δlatency\| per link, received beacon bytes per second |
| **interrupt_ms** | per vehicle: total time in beacon gaps longer than 1 s (connection interruption). The threshold is above the 0.5 s gap that L4's 2 Hz jammed beacon rate creates by design |
| **det_tpr / det_fpr / det_delay_ms** | detector vs ground truth, sampled every 0.1 s, and first detection minus first ground-truth jamming |
| share_normal/congested/weak/jammed | how the detector classified the channel (shows confounder confusion) |

### Figures produced
`sanity_pdr_vs_power`, `sanity_pdr_vs_distance`, `ontime_by_config`, `deadline_miss_by_config`, `latency_by_config`, `beacon_pdr_by_config`, `routing_by_scenario`, `ontime_vs_density`, `rsu_effect`, `detector_states`, plus `detector_accuracy.csv`.

---

## 7. How the code works (module by module)

### Event flow of one emergency alert
```
AlertTick (v2v-jam.cc)  every 1 s, picks a vehicle near the jammer
  ├─ logs ALERT_TARGET for each vehicle within 200 m
  └─ SafetyApp::TriggerAlert
       ├─ Submit(direct broadcast)                         ── all levels
       ├─ if mitigation ≥ 1 and Detector::JamSuspected():
       │     RepeatAlert every 20 ms until the deadline    ── level 1+
       └─ if mitigation ≥ 2 and jam suspected:
             Router::StartRecovery                         ── level 2+
               ├─ RSU in range?  → unicast to RSU → RSU re-broadcasts + backhaul to other RSUs
               ├─ else best relay vehicle → unicast → relay re-broadcasts once
               ├─ else NO_RELAY
               └─ not overheard in 15 ms → ROUTE_BREAK → PATH_SWITCH (next candidate)
Submit (Layer 4)
  ├─ mitigation < 3: straight into the MAC queue (FIFO)
  └─ mitigation = 3: priority queue, EMERGENCY first, released only when the radio is idle,
                     expired messages dropped (DROP_DEADLINE)
Receiver: first copy → RX (latency, path, stale flag). With mitigation 3, a stale copy is dropped instead.
```

### `common.h/.cc`: shared definitions
- **`Params`**: every setting, with defaults. `ApplyScenarioDefaults()` fills the scenario-specific ones and applies confounders. Access it anywhere with `P()`.
- **`V2vHeader`**: the packet header everyone uses:
  - `type` (BEACON/ALERT), `priority` (ROUTINE/EMERGENCY)
  - `pathType` (DIRECT/RSU/RELAY), `hopCount`
  - `seq` (alert id for alerts), `srcId` (creator), `senderId` (last hop), `dstId`
  - `genTimeNs`, `deadlineUs`
  - sender position and velocity, beacon rate
  - flags (is RSU, suspects jamming)
- **`EventLog`**: writes `events.csv` lines `time_ms,event,node,seq,extra` (extra = `k=v;k=v`).
- **Node registry**: `Info(id)` gives the node, device, mobility, kind and app. Ids are vehicles first, then RSUs, then the jammer and interferer.
- Frames are sent with EtherType `0x88DC` (WSMP) directly on the WiFi device. There is no IP stack, which is closer to real WAVE/WSMP and avoids ARP delays.

### `scenario.h/.cc`: roads and mobility
- Each vehicle moves along its lane at constant speed (±15% per vehicle) and **wraps around** at the road end, so density stays constant.
- A fixed random stream makes traffic **identical across mitigation levels** for the same seed, so comparisons are fair.
- RSUs sit 15 m off the road every `rsuSpacing` metres (urban: one at the junction).

### `jammer.h/.cc`: attacker and confounders
- All noise sources are `WaveformGenerator`s on the same `SpectrumChannel` as the radios. 802.11p PHYs treat their energy as interference, which lowers SINR. Above −62 dBm it also makes CCA busy, so nearby radios cannot transmit.
- **Constant**: 5 ms back-to-back noise segments from `jamStart` to `jamStop`.
- **Reactive**: a listening WiFi PHY on the jammer node. On `PhyRxPayloadBegin` it waits `reactDelayUs`, then sends a `burstUs` burst, which corrupts the payload of the frame in flight.
- **Interferer** (confounder): legitimate periodic interference, 10 dBm, 10% duty cycle, 20 ms period.

### `detector.h/.cc`: Layer 1/2 detection
- **Inputs:**
  - PHY state sampled every 2 ms → busy ratio
  - `RxOk` / `RxError` traces → decode-error ratio
  - `MonitorSnifferRx` → RSSI and noise+interference (= signal − SINR)
  - the neighbour table → expected vs received beacons (PDR)
- **Rules** (thresholds are flags):

| State | Rule |
|---|---|
| JAMMED | (busy ≥ 0.8 and < 5 frames/s decoded) **or** (CCA-only busy ≥ 0.3 and noise > −92 dBm) **or** (busy ≥ 0.1 but < 2 frames/s decoded) **or** (PDR < 0.5 and noise > −92 dBm) **or** (PDR < 0.5 and error ratio > 0.3 on a quiet channel) |
| CONGESTED | busy ≥ 0.5. Congestion keeps the radio busy *receiving frames* (RX state). A jammer keeps it busy with *non-frame energy* (CCA_BUSY), which is how the two are told apart |
| WEAK | PDR < 0.5 |
| NORMAL | otherwise |

- **Hysteresis:** 2 consecutive bad evaluations raise JAMMED, and 3 good ones clear it.
- **`JamSuspected()`** is true if the node is locally jammed, **or** ≥ 2 neighbours predicted close by went silent (jammed links), **or** a neighbour's beacon says it suspects jamming.

### `neighbor-table.h`
- Holds each neighbour's position, velocity, last-heard time, smoothed RSSI, advertised beacon rate, RSU flag and jam flag.
- `PredictPos()` estimates where a neighbour is now (dead reckoning).
- `LinkLifetime()` computes how long two moving nodes stay within range.

### `relay.h/.cc`: Layer 3 (`Router`)
- **Candidate order:** RSUs heard in the last second with RSSI ≥ −88 dBm come first. Then relay vehicles heard in the last 0.5 s, not reporting jamming and not on a jammed link.
- **Relay score:** `0.4·quality + 0.3·distance-from-jammed-area + 0.3·link-lifetime`.
- **Implicit ACK:** if the source overhears the relayed copy within 15 ms, it logs `RELAY_OK`. Otherwise it logs `ROUTE_BREAK` and `PATH_SWITCH`, then tries the next candidate (at most 3 switches, never past the deadline).

### `safety-app.h/.cc`: application + Layer 4
- Sends beacons (10 Hz, ±5% jitter, 2 Hz while jammed at level 3) and alerts.
- Handles reception: updates the neighbour table, logs `RX` for the first copy only, and relays or re-broadcasts at RSUs when the alert is addressed to this node.
- **Layer 4 queue:** `Submit()` → `ServiceQueue()` every 1 ms. Each tick it drops expired messages, then hands the highest-priority, oldest message to the MAC when the PHY is idle.
- Tracks beacon gaps to compute the connection interruption time.

### `stats.h/.cc`
Per-second beacon counters (written to `beacons.csv`) and PDR by distance to the jammer (written to `pdr_by_jamdist.csv`). Beacons are not logged one by one, which keeps files small.

### `v2v-jam.cc`
Parses flags, creates nodes, and builds the channel and 802.11p devices: `SpectrumWifiPhy`, `AdhocWifiMac` (no association, like OCB), 6 Mb/s on 10 MHz, and preamble detection and CCA at −90 dBm. It then:
- installs apps and jammers;
- schedules alerts (`AlertTick`) and ground truth (`GroundTruthTick`);
- writes `node_summary.csv` at the end.

---

## 8. All command-line flags

Run `./ns3 run "v2v-jam --PrintHelp"` for the authoritative list. The most important ones:

| Flag | Default | Meaning |
|---|---|---|
| `--scenario` | highway | highway / local / urban |
| `--mitigation` | 3 | 0 none, 1 detect+repeat, 2 +L3, 3 +L3+L4 |
| `--jammer` | none | none / constant / reactive |
| `--confounder` | none | none / congestion / weak / interference |
| `--seed` | 1 | random run number |
| `--simTime` | 40 | seconds |
| `--outDir` | results/run | output folder |
| `--vehicles` | auto (60/12/40) | number of vehicles |
| `--speed` | auto (30/12/10) | mean speed m/s |
| `--rsu`, `--rsuSpacing` | auto (1/0/1), 500 | RSUs on/off, spacing |
| `--txPower`, `--rsuTxPower` | 23, 30 | dBm |
| `--pathLossExp` | auto (2.5/2.7/3.0) | log-distance exponent |
| `--fading` | false | Nakagami fading |
| `--jamPower` | 20 | jammer dBm |
| `--jamX`, `--jamY` | road middle, 20 m off road | jammer position |
| `--jamStart`, `--jamStop` | 10, end | jamming window (s) |
| `--reactDelayUs`, `--burstUs` | 20, 200 | reactive jammer timing |
| `--alertStart`, `--alertInterval` | 12, 1 | alert schedule (s) |
| `--alertRadius` | 200 | target radius (m) |
| `--deadlineMs` | 100 | safety deadline |
| `--repeatMs` | 20 | repeat interval (level ≥ 1) |
| `--beaconHz`, `--beaconHzJam` | 10, 2 | beacon rate normal / jammed (level 3) |
| detector: `--detWindow --detPeriod --edBusyThr --congBusyThr --ccaJamThr --pdrThr --errThr --noiseThr --linkRange --minLinkJam --enterCount --exitCount` | see `common.h` | detector thresholds |
| routing: `--ackTimeoutMs --maxSwitches --backhaulMs --relayMinRssi` | 15, 3, 2, −88 | L3 parameters |
| `--posLog`, `--anim` | false | write positions.csv / NetAnim anim.xml |

---

## 9. Output files

**Units:** all times in output CSVs are in **milliseconds** (columns end in `_ms`), following the team convention (`docs/TEAM-WORK.md` in the team repo). Command-line flags that take times (`--simTime`, `--jamStart`, `--alertStart` …) stay in **seconds**, except flags whose name ends in `Ms`/`Us`.

Each run folder contains:

| File | Content |
|---|---|
| `params.json` | the settings of the run |
| `nodes.csv` | node id, kind, initial position |
| `events.csv` | **the event log**: `time_ms,event,node,seq,extra` |
| `beacons.csv` | one row per simulated second (`time_ms` = start of that second): generated, expected, received, pdr, lat_ms, jitter_ms, goodput, deadline drops |
| `pdr_by_jamdist.csv` | beacon PDR in 50 m bins of distance to the jammer, before/during jamming |
| `node_summary.csv` | per node: beacons sent/received, `max_gap_ms`, `interrupt_ms`, detector TP/FN/FP/TN, `first_gt_ms` / `first_detect_ms` (−1 = never), time in each state |
| `stdout.txt` | console output (from run_batch.py) |
| `positions.csv` / `anim.xml` | only with `--posLog=1` / `--anim=1` |

### Event types in `events.csv`

| Event | node | seq | extra |
|---|---|---|---|
| `ALERT_GEN` | source | alert id | x, y, jam_suspected, det_state |
| `ALERT_TARGET` | target vehicle | alert id | src, dist |
| `TX` | sender | alert id | path, hop, dst, bytes |
| `RX` | receiver | alert id | lat_ms, path, hop, from, stale |
| `REPEAT` | source | alert id | rep |
| `DROP_DEADLINE` | node | alert id | where (tx/queue/rx/relay/rsu), age_ms |
| `REROUTE` | source | alert id | path (RSU/RELAY), via, score, dist |
| `NO_RELAY` | source | alert id | neighbors, tried |
| `RELAY_OK` | source | alert id | via, path, rec_ms, switches |
| `ROUTE_BREAK`, `PATH_SWITCH`, `RECOVERY_TIMEOUT` | source | alert id | via / from,to / reason |
| `RELAY_FWD`, `RSU_FWD`, `BACKHAUL` | relay / RSU | alert id | src / to |
| `DET_STATE`, `JAM_DETECTED` | node | −1 | from, to, busy, pdr, err, noise, rssi, linkjam |
| `GT_ON`, `GT_OFF` | node | −1 | jam_dbm (ground truth) |
| `JAM_ON`, `JAM_OFF`, `INTERF_ON`, `JAM_STATS` | 0 (unused) | −1 | type, power, position / bursts |

---

## 10. Calibrating the detector

The default thresholds are reasonable starting points, not tuned values. Tuning them is part of the project:
1. Run `confounders` and the `main` runs with `--mitigation=0`.
2. Open `results/figures/detector_accuracy.csv` and `detector_states.png`.
3. If congestion or weak-channel runs show JAMMED often (false positives), make the rules stricter: raise `--noiseThr`, lower `--pdrThr`, raise `--errThr`, or raise `--enterCount`. If jamming runs are missed (low TPR), loosen them.
4. Use `DET_STATE` lines in `events.csv`. They contain the measured busy/pdr/err/noise values at every state change, which shows what the detector actually saw.
5. Report the final thresholds and the accuracy table. Don't silently change them between experiments: re-run all presets after calibrating.

---

## 11. Modelling assumptions and limitations

- **MAC:** 802.11p PHY (10 MHz, 5.9 GHz) with ns-3's ad-hoc MAC instead of the WAVE/OCB module (removed from ns-3; 3.47 has no `wave` module). Broadcasts need no association or ACK in both, so behaviour for safety messages is equivalent. There is no EDCA. Priority is handled by our L4 queue.
- **Mobility:** synthetic constant-velocity traffic with wrap-around, not SUMO traces. There are no lane changes or braking dynamics.
- **RSU backhaul:** modelled as a fixed 2 ms delay, not a simulated wired network.
- **Ground truth:** "jammed" means the jammer's received power is ≥ −92 dBm (deterministic path loss). This is a modelling choice; report it.
- **Implicit ACK:** a source that is itself jammed may not overhear its relay, which shows up as a route break even though the relay worked. Mention this when interpreting `relay_ok_rate`.
- **Detector:** a threshold rule, not ML. Its accuracy is bounded by the ns-3 channel/jamming model.
- **Validation:** results are simulation-based, not over-the-air.
- **Expected outcome:** sparse local roads may show little L3 gain. That is a legitimate finding. Report it with `no_relay_rate`.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| **Compile error** in `scratch/v2v-jam/...` | Most likely a small API mismatch. Paste the error into Claude Code (see `CLAUDE.md`) or fix by hand, then run `bash scripts/sync_code.sh`. Make sure you are on **ns-3.47** exactly |
| `./ns3 run v2v-jam` says program not found | Run `./ns3 build` first. Check that `~/ns-3.47/scratch/v2v-jam/v2v-jam.cc` exists. `./ns3 show targets` lists program names |
| Error about `NetAnim` / `netanim-module.h` | The netanim module wasn't enabled. Re-run the `./ns3 configure` line from §3.3 (it includes `netanim`) |
| Attribute error at start (e.g. `CcaSensitivity`, `ChannelSettings`) | Wrong ns-3 version. Use 3.47 |
| **ns-3.40 only:** run aborts at ~0.1 s with `cond="niIt == m_niChanges.end()" ... interference-helper.cc, line=337` | Bug in ns-3.40 with 10 MHz channels (fixed in 3.47). In `src/wifi/model/non-ht/ofdm-phy.cc`, function `OfdmPhy::GetMeasurementChannelWidth`, change `return 20;` to `return std::min<uint16_t>(m_wifiPhy->GetChannelWidth(), 20);`, then `./ns3 build` |
| Build killed / very slow | WSL ran out of RAM. Build with `./ns3 build -j2`, and/or create `C:\Users\<you>\.wslconfig` with `[wsl2]` and `memory=8GB` |
| Everything is slow | Keep the project and ns-3 in `~/`, not `/mnt/c/` |
| `run_batch.py` can't find the program | Check `ls ~/ns-3.47/build/scratch/v2v-jam/`. The binary is named `ns3.47-v2v-jam-optimized` (or `-default`) |
| PDR barely drops with the jammer | Increase `--jamPower`, move the jammer (`--jamY=5`), check the `JAM_ON` line in events.csv |
| `pip3 install` fails with "externally managed" | `python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt` |
