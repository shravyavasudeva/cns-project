# Results: full experiment batch on ns-3.47

These are the results of the complete experiment run of `v2v-jam-sim` (all presets: 471 simulation runs, plus the smoke run and 2 demo runs for the animation). This file explains **what was run, how to read every file and figure, what the numbers say, and what is odd or limited about them**.

- **Run date:** 25 Nov 2026. The `urbanradius` and `edgerelay` presets were added the same evening (this replaces the unpatched 24 Nov batch, which is in git history at commit `35d3e0e`)
- **Platform:** ns-3.47 (optimized build) **with our bug-fix patch** [`patches/ns-3.47-cca-idle-during-rx.patch`](../patches/README.md), WSL2 on Windows, Ubuntu 26.04.1, g++ 15.2.0, CMake 4.2.3, Python 3.14 + pandas 2.3.3
- **Code:** branch `feature/full-simulation`, the commit that adds this file. Simulation logic is unchanged from `1e0becd`. C++ changes: `beaconHzJam`, `relayEdge` and `relayEdgeMargin` are written into `params.json`, and a new `--relayEdge` option (default off) is used only by the `edgerelay` preset. With the default, output is byte-identical to before (test 7d)
- **Command:** `python3 run_batch.py --ns3 ~/ns-3.47 --preset all --jobs 2` (after `smoke`), the two demo runs, then `analysis/metrics.py` and `analysis/plots.py`
- **Outcome:** **471 of 471 preset runs succeeded**, plus smoke and both demo runs. The ns-3.47 crash from the 24 Nov batch is fixed ([§7.1](#71-ns-347-crash-found-and-fixed)).

---

## Contents
1. [Headline findings](#1-headline-findings)
2. [What was run](#2-what-was-run)
3. [Folder layout and files](#3-folder-layout-and-files)
4. [Tests and checks performed](#4-tests-and-checks-performed)
5. [Results in detail](#5-results-in-detail)
6. [Every figure explained](#6-every-figure-explained)
7. [Anomalies, failures and caveats](#7-anomalies-failures-and-caveats)
8. [Comparison with the earlier ns-3.40 results](#8-comparison-with-the-earlier-ns-340-results)
9. [Suggested next steps](#9-suggested-next-steps)
10. [How to reproduce](#10-how-to-reproduce)

---

## 1. Headline findings

1. **Every run now completes.** The 24 Nov batch hit an ns-3.47 bug: one congestion run aborted inside ns-3's WiFi code. We traced it to `PhyEntity::SwitchMaybeToCcaBusy()` setting the radio IDLE in the middle of a reception, and fixed it with a one-line patch ([§7.1](#71-ns-347-crash-found-and-fixed)). The fix barely moves the numbers: 316 of 362 runs are identical to the unpatched batch, and no table value changes by more than 0.01 except the repaired cell.
2. **The jammer works as intended.** Beacon PDR falls from 1.00 to 0.51 as jammer power rises from −10 to 30 dBm. At 20 dBm, PDR is 0.08 within 50 m of the jammer and back to about 1.0 beyond about 600 m.
3. **Full mitigation (detection + L3 + L4) always helps, but only a little.** On-time alert reachability improves in **every** scenario × jammer combination, and on **every seed** (paired comparison, same traffic):

   | On-time reachability, mitigation 0 → 3 | constant jammer | reactive jammer |
   |---|---|---|
   | Highway | 0.477 → **0.569** (+0.092, 5/5 seeds better) | 0.548 → **0.582** (+0.034, 5/5) |
   | Local road | 0.503 → **0.532** (+0.029, 5/5) | 0.506 → **0.528** (+0.022, 3/5, 2 tied) |
   | Urban | 0.282 → **0.320** (+0.037, 5/5) | 0.343 → **0.400** (+0.057, 5/5) |

   No mitigation level comes close to the no-jammer baseline. Vehicles deep inside the jammed zone cannot decode anything, whoever sends the alert. Mitigation only rescues receivers near the edge of the jammed area.
4. **Different layers help in different places:**
   - **Detection + repeat (level 1)** gives the biggest single gain on the **highway** under a constant jammer (+0.072).
   - **L3 rerouting (level 2)** helps most in **urban** (+0.023 constant, +0.056 reactive), where the RSU at the intersection is almost always available (NO_RELAY only 2–3%). On the **local road** it does nothing (−0.006): no relay or RSU exists for **80%** of alerts.
   - **L4 (level 3)** adds little to reachability (−0.008 to +0.016). Its value shows up elsewhere: **stale alerts accepted drop to 0** everywhere (from 1.0% on the highway at level 2), and **mean alert latency on the jammed highway falls from 20.0 ms to 3.1 ms** (p95 45 ms → 9 ms).
5. **The detector is accurate against a constant jammer but weak against a reactive one:**
   - **Constant jammer:** TPR 0.97 / 0.89 / 0.98 (highway / local / urban), FPR ≤ 3.4%, detection delay 330–520 ms.
   - **Reactive jammer:** TPR only 0.23–0.40, delay 3.6–5.1 s. The reactive jammer is the stealthier attack.
   - **Confounders** (congestion, weak signal, non-malicious interference): never called "jammed" more than 4.7% of the time. Highway congestion is correctly labelled CONGESTED 94% of the time.
6. **L4 reduces detector sensitivity, and a 5 Hz jammed beacon rate mostly fixes it.** At mitigation 3, L4 lowers the beacon rate to 2 Hz while jamming, so the detector has fewer frames to judge from. In the main grid (constant jammer, m0 → m3) TPR drops from 0.89 to 0.51 on the local road and from 0.98 to 0.69 in urban, while the dense highway keeps 0.96. The new `beaconrate` experiment ([§5.8](#58-l4-beacon-rate-while-jammed-beaconrate-preset), seeds 1–3, where 2 Hz gives TPR 0.49 local / 0.74 urban) shows:
   - **5 Hz** restores urban detection fully (0.74 → **0.97**), costs nothing anywhere in on-time delivery, and helps the local road only a little (0.49 → 0.54).
   - **10 Hz** also restores the local road (→ **0.93**), but on the dense highway it hurts: on-time 0.57 → **0.50** and mean latency 3 → **18 ms**.
   - On-time delivery on the local road and in urban is the same at 2, 5 and 10 Hz (±0.01).
7. **Urban's low 0.70 baseline is only a range effect** ([§5.9](#59-urban-target-radius-urbanradius-preset)). With a 150 m target radius instead of 200 m, urban without a jammer reaches **0.98**. Jammed results rise too (constant 0.32 → 0.43, reactive 0.40 → 0.52 at m3), but relative to their own baseline they barely move (constant jammer: m3 delivers 46% of baseline at 200 m, 44% at 150 m). Mitigation still helps on 5/5 seeds. No conclusion depends on the radius.
8. **Relaying through jammed vehicles near the zone edge doesn't help** ([§5.10](#510-edge-relays-edgerelay-preset)). With `--relayEdge=1`, the highway without an RSU finds a relay for about 40% of alerts (NO_RELAY 100% → 57–63%). But at mitigation 2, on-time delivery **drops** by 0.06–0.08, worse on every seed, with and without an RSU, and mean latency rises from 13–16 ms to about 51 ms. L4 at mitigation 3 absorbs the cost (±0.005). On the local road there's no effect. The original rule of never relaying through jammed vehicles is justified.
9. **Density matters on the highway.** As traffic grows (30 → 120 vehicles), unmitigated reachability falls (0.61 → 0.40) while full mitigation holds at about 0.60. At 120 vehicles, **L3 on its own is worse than detection alone** (0.48 vs 0.55), because relay transmissions add load to an already busy channel. Adding L4 brings it back to 0.60.

---

## 2. What was run

The default run lasts **40 simulated seconds**: warm-up from 0–10 s, the jammer switches on at 10 s, and one emergency alert per second from 12–39 s (28 alerts per run). Sanity runs last 30 s, the smoke run 15 s.

| Preset | Varies | Runs | Folder |
|---|---|---|---|
| `smoke` | one short highway run, constant jammer, mitigation 3, 20 vehicles | 1 | `smoke/` |
| `sanity` | jammer power −10, 0, 10, 20, 30 dBm and no jammer; highway, mitigation 0; seeds 1–2 | 12 | `sanity/` |
| `main` | scenario {highway, local, urban} × jammer {none, constant, reactive} × mitigation {0,1,2,3} × seeds 1–5 | 180 | `main/` |
| `density` | vehicles (highway 30/60/120, local 6/12/24, urban 20/40/80) × mitigation {0–3} × seeds 1–3; constant jammer | 108 | `density/` |
| `confounders` | {congestion, weak, interference} × {highway, local} × mitigation {0,3} × seeds 1–3; **no jammer** | 36 | `confounders/` |
| `rsu` | RSU {0,1} × {highway, local} × mitigation {2,3} × seeds 1–3; constant jammer | 24 | `rsu/` |
| `beaconrate` | L4 beacon rate while jammed `beaconHzJam` {2, 5, 10} Hz × {highway, local, urban} × seeds 1–3; constant jammer, mitigation 3 | 27 | `beaconrate/` |
| `urbanradius` | urban, jammer {none, constant, reactive} × mitigation {0–3} × seeds 1–5, **alert target radius 150 m** (main uses 200 m) | 60 | `urbanradius/` |
| `edgerelay` | same grid as `rsu` (RSU {0,1} × {highway, local} × mitigation {2,3} × seeds 1–3, constant jammer) with **`--relayEdge=1`** | 24 | `edgerelay/` |
| demo | highway, constant jammer, seed 1, mitigation 0 and 3, with position logging for the animation | 2 | `demo/` |

**Mitigation levels are cumulative:**
- **0:** no mitigation. The detector still runs, so its accuracy is measured at every level.
- **1:** detection + alert repetition.
- **2:** level 1 + **L3** rerouting via an RSU or relay vehicle.
- **3:** level 2 + **L4** deadline and priority handling (drops stale alerts, prioritises alerts over beacons, lowers the beacon rate while jammed).

**Scenario defaults:**

| | Road | Vehicles | Speed | RSU | Path-loss exponent |
|---|---|---|---|---|---|
| highway | 2000 m straight | 60 | 30 m/s | 1 | 2.5 |
| local | 1000 m two-lane | 12 | 12 m/s | 0 | 2.7 |
| urban | two crossing 1000 m streets | 40 | 10 m/s | 1 (at the intersection) | 3.0 |

All scenarios also use Nakagami fading. The jammer sits at the road centre or intersection.

**Fair comparison:** mobility and alert-source choice use fixed RNG streams. Runs with the same seed therefore have identical traffic and alert sources at every mitigation level, which is what makes the paired per-seed comparisons in §5.1 valid.

---

## 3. Folder layout and files

```
results/
├── README.md                ← this file
├── batch_run_log.txt        ← console log of the full batch (run order, wall time, ok/FAILED)
├── summary_runs.csv         ← ONE ROW PER RUN: all parameters + all metrics (474 rows: 471 preset runs + smoke + 2 demo)
├── summary_grouped.csv      ← mean/std per configuration (seeds pooled)
├── figures/                 ← all plots (PNG) + detector_accuracy.csv
├── smoke/  sanity/  main/  density/  confounders/  rsu/  beaconrate/  urbanradius/  edgerelay/
│   └── <run-name>/          ← one folder per simulation run (raw output)
└── demo/
    ├── highway_jam-constant_m0_s1_poslog/   ← demo run without mitigation (+ positions.csv)
    ├── highway_jam-constant_m3_s1_poslog/   ← demo run with full mitigation
    ├── demo_highway_m0.gif                  ← animation, no mitigation
    └── demo_highway_m3.gif                  ← animation, full mitigation
```

**Run-folder names** encode the parameters: `<scenario>_jam-<jammer>[_conf-<confounder>]_m<mitigation>[_n<vehicles>][_rsu<0|1>][_p<jamPower>][_bj<beaconHzJam>][_r<alertRadius>][_edge<relayEdge>]_s<seed>`. For example, `local_jam-constant_m3_n24_s2` is the local road, constant jammer, mitigation 3, 24 vehicles, seed 2.

**Files in every run folder:**

| File | Contents |
|---|---|
| `params.json` | every parameter of the run (after scenario defaults were applied) |
| `stdout.txt` | console output of the simulator (and the crash trace, if any) |
| `nodes.csv` | node id, role (vehicle / RSU / jammer / interferer), start position |
| `events.csv` | `time_ms,event,node,seq,extra`: one line per alert, routing, detector and ground-truth event (`ALERT_TARGET`, `RX`, `DET_STATE`, `JAM_DETECTED`, `GT_ON`, `REROUTE`, `NO_RELAY`, `ROUTE_BREAK`, `PATH_SWITCH`, `DROP_DEADLINE`, …). Event meanings are in the main README §9. |
| `beacons.csv` | per-second, per-node beacon counters (sent, expected, received, latency) |
| `pdr_by_jamdist.csv` | beacon PDR by receiver distance to the jammer, before and during jamming |
| `node_summary.csv` | per-node totals at the end of the run, including detector accuracy counters |
| `positions.csv` | (demo runs only) vehicle positions over time for the animation |

**All times in CSV files are milliseconds** (columns ending in `_ms`).

**`summary_runs.csv` columns:**
- Run parameters: `scenario, jammer, confounder, mitigation, vehicles, rsu, jamPower, seed, speed, simTime, beaconHzJam, alertRadius, relayEdge, preset, run_dir`.
- Counts: `alerts, targets`.
- Every metric in §4 of the main README: `ontime_ratio, reach_ratio, deadline_miss_ratio, stale_accept_ratio, alert_lat_ms, alert_lat_p95_ms, alert_jitter_ms, via_alt_ratio, jam_suspected_at_alert, recovery_alerts, reroutes, no_relay_rate, relay_ok_rate, recovery_ms, route_breaks_per_alert, path_switches_per_alert, alert_tx_per_alert, routing_tx_per_alert, drop_deadline, det_tpr, det_fpr, det_delay_ms, share_normal/congested/weak/jammed, beacon_pdr, beacon_lat_ms, beacon_jitter_ms, goodput_kbps, interrupt_ms, max_gap_ms`.

**Empty (NaN) cells are expected, not errors:**
- Routing metrics (`no_relay_rate`, `relay_ok_rate`, `recovery_ms`, `route_breaks_per_alert`, `path_switches_per_alert`) are empty in 214 runs. In those runs L3 is off (mitigation < 2) or recovery never triggered (no jammer).
- `det_tpr` and `det_delay_ms` are empty in exactly the 98 runs **without a jammer**: there is nothing to detect, so a true-positive rate is undefined. `det_fpr` is still reported there.

### Metric cheat-sheet

| Metric | Meaning | Good is |
|---|---|---|
| `ontime_ratio` | share of target vehicles (within 200 m of the alert source) that got the alert **before its deadline** | high |
| `reach_ratio` | the same, but late arrivals also count | high |
| `deadline_miss_ratio` | 1 − ontime_ratio | low |
| `stale_accept_ratio` | targets that **accepted an alert after its deadline** (a safety hazard) | 0 |
| `alert_lat_ms`, `alert_lat_p95_ms`, `alert_jitter_ms` | alert latency: mean, 95th percentile, standard deviation | low |
| `via_alt_ratio` | on-time receptions whose first copy came via an RSU or relay | context |
| `no_relay_rate` | alerts where L3 found **no RSU or relay** to reroute through | low |
| `relay_ok_rate` | the source overheard its relay's re-broadcast (implicit ACK) | high |
| `recovery_ms` | time from starting recovery to overhearing the relay | low |
| `routing_tx_per_alert` | extra radio transmissions per alert caused by L3 (overhead) | low |
| `beacon_pdr` | beacons received ÷ beacons expected (within 200 m), from jam start onward | high |
| `interrupt_ms` | per vehicle: total time in beacon gaps longer than 1 s | low |
| `det_tpr` / `det_fpr` | detector true / false positive rate against ground truth, sampled every 0.1 s | high / low |
| `det_delay_ms` | first detection minus first ground-truth jamming | low |
| `share_*` | what fraction of the time the detector said normal / congested / weak / jammed | context |

---

## 4. Tests and checks performed

| # | Check | How | Result |
|---|---|---|---|
| 1 | **Build on ns-3.47** | `./ns3 configure --build-profile=optimized` + `./ns3 build`, with `patches/` applied | ✅ Builds without errors |
| 2 | **Smoke test** | `--preset smoke` | ✅ All metrics are numbers (no NaN): ontime 0.833, beacon PDR 0.907, det TPR 0.547, FPR 0.004 |
| 3 | **Jammer sanity, power sweep** | `--preset sanity` | ✅ PDR falls monotonically 0.997 → 0.986 → 0.953 → 0.851 → 0.506 for −10…30 dBm. No jammer: 0.997 |
| 4 | **Jammer sanity, distance** | `sanity_pdr_vs_distance.png` | ✅ PDR 0.08 within 50 m of a 20 dBm jammer, recovers to about 1.0 beyond about 600 m. Before jamming: about 1.0 everywhere |
| 5 | **No-jammer baseline** | `main`, jammer none | ✅ Highway and local reachability 1.000 (the mitigation stack causes no harm). Urban 0.70: see §7.3 |
| 6 | **Full experiment batch** | `--preset all` | ✅ 387/387 runs, plus smoke and 2 demo runs. No run contains an ns-3 abort or assertion message |
| 7 | **ns-3 crash fix** | the formerly crashing run, in an ns-3.47 **debug** build (all `NS_ASSERT`s on) and in the optimized build | ✅ Unpatched debug build: assertion at 0.82 s. Patched: completes in both builds (§7.1) |
| 7b | **Effect of the patch** | every run paired with the unpatched 24 Nov batch | ✅ 316/362 runs bit-for-bit identical. The 46 that differ are all dense or congested runs (highway, or urban in the density sweep); the largest single-run change is 0.03 on-time ratio, and the mean change is about 0 |
| 7d | **`--relayEdge` option off by default changes nothing** | 2 existing runs (`rsu` highway no-RSU m2 s1, `main` urban reactive m3 s2) rerun with the new code | ✅ `events.csv`, `beacons.csv`, `node_summary.csv`, `pdr_by_jamdist.csv` byte-identical. Existing runs were not rerun; their `params.json` lacks the new keys, and `metrics.py` fills in the defaults (200 m, 0) |
| 7c | **Determinism** | `beaconrate` runs at 2 Hz (the default) vs the same seeds in `main` at mitigation 3 | ✅ Identical metrics |
| 8 | **Metrics sanity** | NaN audit of `summary_runs.csv` | ✅ Every NaN accounted for (§3) |
| 9 | **Paired mitigation test** | per-seed difference, same traffic | ✅ Mitigation 3 beats 0 on every seed in 5 of 6 scenario × jammer cells (3/5 + 2 ties in the sixth) |
| 10 | **Detector false alarms under confounders** | `--preset confounders` | ✅ FPR ≤ 4.7% (worst: highway congestion at mitigation 0) |

---

## 5. Results in detail

Numbers are means over seeds (5 seeds for `main`, 3 for the others). ± is the standard deviation across seeds.

### 5.1 Main grid: on-time alert reachability

| Scenario | Jammer | m0 | m1 | m2 | m3 | seed std |
|---|---|---|---|---|---|---|
| highway | none | 1.000 | 1.000 | 1.000 | 0.999 | ≈0 |
| highway | constant | 0.477 | 0.549 | 0.566 | **0.569** | ±0.04 |
| highway | reactive | 0.548 | 0.559 | 0.579 | **0.582** | ±0.03–0.04 |
| local | none | 1.000 | 1.000 | 1.000 | 1.000 | 0 |
| local | constant | 0.503 | 0.522 | 0.516 | **0.532** | ±0.10–0.11 |
| local | reactive | 0.506 | 0.518 | 0.532 | 0.528 | ±0.10–0.12 |
| urban | none | 0.696 | 0.696 | 0.696 | 0.694 | ±0.02 |
| urban | constant | 0.282 | 0.290 | 0.313 | **0.320** | ±0.05–0.07 |
| urban | reactive | 0.343 | 0.352 | **0.409** | 0.400 | ±0.07 |

The seed-to-seed spread (±0.04 to ±0.12) is **larger than most of the gains**, so the error bars in `ontime_by_config.png` overlap. The improvement is still real, because every configuration uses the **same traffic for the same seed**. Comparing level by level on each seed:

| Step | highway const | highway react | local const | local react | urban const | urban react |
|---|---|---|---|---|---|---|
| m0→m1 (detection + repeat) | **+0.072** (5/5 seeds) | +0.011 (5/5) | +0.019 (5/5) | +0.012 (3/5) | +0.008 (4/5) | +0.009 (5/5) |
| m1→m2 (+L3) | +0.018 (4/5) | +0.020 (5/5) | **−0.006** (0/5, 2 worse) | +0.014 (4/5) | +0.023 (5/5) | **+0.056** (5/5) |
| m2→m3 (+L4) | +0.002 | +0.004 | +0.016 (4/5) | −0.004 | +0.007 (5/5) | −0.008 |
| **m0→m3 (total)** | **+0.092 (5/5)** | +0.034 (5/5) | +0.029 (5/5) | +0.022 (3/5) | +0.037 (5/5) | +0.057 (5/5) |

(x/5 = number of seeds where the step improved reachability; the rest were tied or worse.)

**Interpretation:**
- **Detection + repeat** matters most on the dense highway with a constant jammer. Repeating the alert gives more chances for one copy to slip between jammer-induced collisions and fading dips.
- **L3** only helps where something exists to reroute through. Urban has an RSU at the intersection (the jam hotspot), so it is almost always reachable. The local road has no RSU and few vehicles.
- **L4** is about **timeliness and safety**, not reach: see §5.2.

### 5.2 Latency, staleness and overhead (constant jammer)

| Scenario | Metric | m0 | m1 | m2 | m3 |
|---|---|---|---|---|---|
| highway | mean alert latency (ms) | 15.6 | 17.4 | 20.0 | **3.1** |
| highway | p95 latency (ms) | 35.1 | 40.1 | 45.0 | **9.3** |
| highway | stale alerts accepted | 0.6% | 0.7% | 1.0% | **0%** |
| urban | mean / p95 latency (ms) | 2.0 / 7.9 | 3.6 / 15.4 | 9.5 / 58.0 | **3.1 / 25.3** |
| urban | stale alerts accepted | 0 | 0 | 0.3% | **0%** |
| local | mean / p95 latency (ms) | 1.2 / 5.6 | 3.0 / 10.7 | 2.8 / 9.5 | 2.9 / 15.2 |

- **Repetition and rerouting increase latency** (more copies and relay hops, queueing behind beacons). L4's priority queue cuts it back: on the highway it falls below the unmitigated level.
- **L4 removes stale accepts completely.** These are vehicles acting on an alert that has already expired. The number is small (≤1%), but each one is a safety hazard.
- **Routing overhead** (`routing_tx_per_alert`): about 1.8 extra transmissions per alert on the highway, 2.4–2.8 in urban, and only 0.4–0.6 on the local road (because no relay is found).
- **Route recovery time** (`recovery_ms`): 3–9 ms on the highway, 1–5 ms on the local road, but **21–23 ms in urban**, where path switches are frequent (≈0.8 per alert). The first relay choice often fails at the intersection and a second candidate is tried.

### 5.3 Layer 3 availability (`no_relay_rate`, mitigation 2 / 3)

| Scenario | constant | reactive |
|---|---|---|
| highway | 39% / 41% | 28% / 31% |
| local | **80% / 63%** | 51% / 56% |
| urban | 3% / 2% | 2% / 2% |

On the sparse local road, rerouting usually has **nothing to reroute through**. This is the expected result for sparse roads (main README §11): a legitimate finding about where L3 can and cannot help, not a bug.

### 5.4 Beacon delivery and connection interruption

| Scenario | Jammer | beacon PDR m0 → m3 | interrupt_ms m0 → m3 |
|---|---|---|---|
| highway | constant | 0.90 → 0.92 | 546 → 711 |
| local | constant | 0.70 → 0.80 | 3717 → 4095 |
| urban | constant | 0.39 → 0.56 | 722 → 866 |
| highway / local / urban | none | 0.998 / 0.998 / 0.700 | 0 / 543 / 220 |

- At m3, beacon PDR **rises** because L4 sends fewer beacons while jammed (2 Hz), so fewer are expected and less of the channel is used.
- `interrupt_ms` **also rises** at m3, because a 2 Hz rate means longer gaps between beacons. Gaps over 1 s count as interruptions.
- The local road has 543 ms of interruption even **without** a jammer. That is sparse-traffic disconnection: vehicles drift out of range of each other.

### 5.5 Detector accuracy (mitigation 0, from `figures/detector_accuracy.csv`)

| Scenario | Condition | TPR | FPR | Detection delay | Said "jammed" |
|---|---|---|---|---|---|
| highway | constant jammer | **0.972** | 0.027 | 428 ms | 22% |
| highway | reactive jammer | 0.228 | 0.000 | 5.1 s | 5% |
| local | constant jammer | **0.888** | 0.034 | 519 ms | 33% |
| local | reactive jammer | 0.329 | 0.000 | 3.7 s | 11% |
| urban | constant jammer | **0.979** | 0.029 | 329 ms | 24% |
| urban | reactive jammer | 0.398 | 0.004 | 3.6 s | 9% |
| highway | congestion (no jammer, 3 seeds) | – | 0.047 | – | 5% (94% **CONGESTED**) |
| highway | weak channel | – | 0.000 | – | 0% (9% **WEAK**) |
| highway | interference | – | 0.000 | – | 0% |
| local | congestion / interference | – | 0.000 | – | 0% |
| local | weak channel | – | 0.001 | – | 0% (17% **WEAK**) |
| all | no jammer | – | 0.000 | – | 0% |

- **Constant jammers are detected reliably and fast.**
- **Reactive jammers are hard to detect.** They only transmit while a frame is being received, so the noise floor and busy time look normal most of the time. This matches the ns-3.40 observation (TPR 0.26).
- The detector **separates confounders well**. Highway congestion is mostly labelled CONGESTED (the CCA_BUSY-only test works), and weak-signal areas are labelled WEAK. Neither is mistaken for jamming.
- On the **local road the congestion confounder does not actually congest** the channel (99.9% NORMAL). With only 12 vehicles, the extra traffic is not enough to saturate the channel. Treat local congestion results as "no stress" rather than a real test.

**Effect of L4 on detection (main grid, constant jammer, TPR at m0 → m3):** highway 0.97 → 0.96, local **0.89 → 0.51**, urban **0.98 → 0.69**. Density shows the same pattern: highway at 30 vehicles falls 0.97 → 0.59 at m3, while 120 vehicles stays at 0.97. The fewer beacons around, the more L4's rate reduction starves the detector. §5.8 tests higher jammed beacon rates.

### 5.6 Density

| Scenario | Vehicles | m0 | m1 | m2 | m3 | NO_RELAY (m2) |
|---|---|---|---|---|---|---|
| highway | 30 | 0.605 | 0.641 | 0.665 | 0.654 | 46% |
| highway | 60 | 0.487 | 0.547 | 0.568 | 0.574 | 41% |
| highway | 120 | 0.396 | 0.546 | **0.474** | **0.598** | 37% |
| local | 6 | 0.423 | 0.449 | 0.454 | 0.459 | 79% |
| local | 12 | 0.504 | 0.525 | 0.521 | 0.533 | 80% |
| local | 24 | 0.518 | 0.536 | 0.535 | 0.541 | 87% |
| urban | 20 | 0.300 | 0.315 | 0.349 | 0.359 | 4% |
| urban | 40 | 0.256 | 0.264 | 0.284 | 0.290 | 2% |
| urban | 80 | 0.261 | 0.282 | 0.275 | 0.319 | 2% |

- **Highway:** more vehicles means more contention, so unmitigated reachability falls (0.61 → 0.40). The mitigation stack matters most at high density: +0.20 at 120 vehicles vs +0.05 at 30. At 120 vehicles, **L3 alone hurts** compared with level 1 (relay transmissions add load), and **L4's prioritisation and beacon reduction are what make L3 pay off**.
- **Local:** even 24 vehicles on 1 km is too sparse for relays (NO_RELAY stays at 79–87%). Gains stay small at every density.
- **Urban:** reachability is low at every density. The RSU is always available, but the higher path loss (exponent 3.0) limits range (see §7.3).

### 5.7 RSU effect (`rsu` preset, constant jammer)

| Scenario | RSU | m2 ontime | m3 ontime | NO_RELAY (m2) | via RSU/relay (m2) |
|---|---|---|---|---|---|
| highway | 0 | 0.549 | 0.560 | **100%** | 0% |
| highway | 1 | 0.568 | 0.574 | 41% | 12% |
| local | 0 | 0.521 | 0.533 | 80% | 0% |
| local | 1 | 0.535 | 0.526 | 43% | 4% |

- **On the highway without an RSU, L3 never finds a relay** (NO_RELAY 100%), so all L3 help on the highway comes from the RSU. Cause: see §7.5.
- Adding an RSU to the local road halves NO_RELAY (80% → 43%), but reachability barely moves (+0.014 at m2, −0.007 at m3). The RSU is reachable, but the vehicles that missed the alert are mostly too deep in the jammed area to hear the RSU either.

### 5.8 L4 beacon rate while jammed (`beaconrate` preset)

L4 (mitigation 3) lowers the beacon rate while jamming is suspected, to leave the channel to alerts. The default is 2 Hz. This preset reruns the constant-jammer, mitigation-3 cell at 2, 5 and 10 Hz (seeds 1–3; 2 Hz is identical to `main` at m3). For reference, without the slow-down (m2) TPR is 0.97 / 0.93 / 0.98 (highway / local / urban).

| Scenario | `beaconHzJam` | Detector TPR (seed range) | FPR | On-time | Mean alert latency | Beacon PDR |
|---|---|---|---|---|---|---|
| highway | 2 | 0.96 (0.95–0.96) | 0.021 | **0.574** | **3.2 ms** | 0.93 |
| highway | 5 | 0.97 (0.97–0.97) | 0.025 | **0.574** | 5.0 ms | 0.93 |
| highway | 10 | 0.97 (0.97–0.97) | 0.026 | 0.499 | 18.0 ms | 0.91 |
| local | 2 | **0.49** (0.38–0.55) | 0.026 | 0.533 | 1.9 ms | 0.75 |
| local | 5 | 0.54 (0.48–0.62) | 0.007 | 0.525 | 1.1 ms | 0.70 |
| local | 10 | **0.93** (0.89–0.98) | 0.043 | 0.523 | 2.7 ms | 0.65 |
| urban | 2 | **0.74** (0.69–0.80) | 0.025 | 0.290 | 3.0 ms | 0.54 |
| urban | 5 | **0.97** (0.97–0.98) | 0.030 | 0.289 | 3.7 ms | 0.45 |
| urban | 10 | 0.98 (0.98–0.98) | 0.028 | 0.292 | 5.8 ms | 0.38 |

- **The detector starves at 2 Hz on sparse roads, as suspected.** Raising the rate brings TPR back to the no-L4 level: urban needs 5 Hz, the local road (12 vehicles) needs 10 Hz.
- **On the dense highway, 10 Hz costs real performance:** on-time delivery 0.574 → 0.499 and latency 3 → 18 ms, i.e. most of L4's latency benefit is lost. Paired by seed (same traffic), 10 Hz is worse than 2 Hz on **all 3 seeds** for both metrics. Beacons compete with alerts again on a busy channel. At 5 Hz the highway shows no loss.
- **On the local road and in urban, alert delivery doesn't depend on the beacon rate** (on-time within ±0.01). Beacon PDR falls as the rate rises because more beacons are expected, not because the channel is worse.
- **FPR stays ≤ 4.3%** at every rate.
- **Recommendation (team decision):** 5 Hz is the best single default: it fixes urban and costs nothing on the highway. The cleanest option is a density-adaptive rate, e.g. 10 Hz when the neighbour table is small (sparse road) and 2–5 Hz when it is large. The defaults were **not** changed for this batch: every other preset still uses 2 Hz.

### 5.9 Urban target radius (`urbanradius` preset)

Every scenario counts a vehicle as an alert target if it is within 200 m of the source. Urban uses heavier path loss (exponent 3.0, buildings), so many 200 m targets are out of reliable range even without a jammer (§7.3). This preset repeats the urban part of `main` with a **150 m** radius (same seeds, so the traffic is identical).

| Jammer | Radius | m0 | m1 | m2 | m3 | m3 as share of its no-jammer baseline |
|---|---|---|---|---|---|---|
| none | 200 m | 0.696 | 0.696 | 0.696 | 0.694 | – |
| none | **150 m** | **0.978** | 0.978 | 0.978 | 0.975 | – |
| constant | 200 m | 0.282 | 0.290 | 0.313 | 0.320 | 46% |
| constant | 150 m | 0.396 | 0.407 | 0.417 | **0.426** | 44% |
| reactive | 200 m | 0.343 | 0.352 | 0.409 | 0.400 | 58% |
| reactive | 150 m | 0.481 | 0.494 | 0.518 | **0.516** | 53% |

- **The 0.70 baseline is purely a range effect.** At 150 m, urban without a jammer is at 0.98, like the highway and local road.
- **The jammer's relative damage is the same at both radii** (unmitigated: 41% of baseline under the constant jammer and 49% under the reactive jammer, at either radius).
- **Mitigation still improves delivery on 5/5 seeds** at 150 m: m0 → m3 is +0.030 (constant) and +0.035 (reactive), slightly smaller than at 200 m (+0.037 / +0.057). The extra targets at 200 m sit further out, where an RSU re-broadcast helps most.
- Detection is identical at both radii (TPR 0.91 constant / 0.42 reactive, averaged over levels), as expected: the radius only changes who counts as a target.
- **For the report:** either keep 200 m and compare urban against its 0.70 baseline, or quote the 150 m numbers. The conclusions are the same.

### 5.10 Edge relays (`edgerelay` preset)

By default, L3 never picks a relay vehicle that is itself jammed. On the highway without an RSU, that means no relay is ever found (§7.5). With `--relayEdge=1`, a jammed neighbour is allowed if it is at least 50 m farther from the estimated jam centre than the source, i.e. nearer the edge of the zone. It re-broadcasts the alert once (single hop). This preset repeats the `rsu` grid with the option on; the comparison below is paired by seed.

| Scenario | RSU | Level | On-time (off → **on**) | Paired change | NO_RELAY | Mean alert latency | L3 transmissions / alert |
|---|---|---|---|---|---|---|---|
| highway | no | m2 | 0.549 → **0.491** | **−0.058** (3/3 seeds worse) | 100% → 57% | 12.9 → **51.0 ms** | 0 → 1.27 |
| highway | no | m3 | 0.560 → 0.554 | −0.005 (2 worse, 1 tie) | 100% → 63% | 4.0 → 3.0 ms | 0 → 0.85 |
| highway | yes | m2 | 0.568 → **0.494** | **−0.075** (3/3 worse) | 41% → 37% | 16.2 → **52.4 ms** | 1.79 → 2.58 |
| highway | yes | m3 | 0.574 → 0.576 | +0.002 (1 better, 2 ties) | 41% → 41% | 3.2 → 3.2 ms | 1.79 → 1.88 |
| local | no | m2 / m3 | 0.521 / 0.533 → 0.525 / 0.537 | +0.004 | 80% → 72% / 53% → 53% | ≈ same | small |
| local | yes | m2 / m3 | 0.535 / 0.526 → 0.532 / 0.529 | −0.003 / +0.002 | ≈ same | ≈ same | small |

- **Edge relays are found, but they don't deliver.** The chosen relay is itself jammed, so the hand-off to it often fails: the source overhears a relay's re-broadcast for only 19% of alerts (`relay_ok_rate`, highway, no RSU, m2). Each failure triggers a route break and a switch to the next candidate (0.31–0.46 path switches per alert), adding transmissions to a channel that is already jammed.
- **At mitigation 2 this extra traffic delays the source's own repeated broadcasts.** Latency quadruples and on-time delivery drops on every seed. It hurts even with an RSU, because once the RSU attempt fails, the source now keeps trying jammed vehicles instead of stopping.
- **At mitigation 3 the effect is neutral.** Most likely L4's priority queue and reduced beacon load absorb the extra traffic (not measured separately).
- **Local road:** few candidates, so almost nothing changes.
- **Conclusion for the report:** "Only infrastructure with a wired link out (the RSU) can carry alerts out of a jammed highway zone. Single-hop relaying through jammed vehicles near the edge does not help and, without L4, harms delivery." This supports the default design, which keeps `--relayEdge=0` for all other presets. A true multi-hop escape (relays forwarding again until outside the zone) was not tested.

---

## 6. Every figure explained

All figures are in `figures/`. Error bars are 95% confidence intervals of the mean across seeds (1.96 · std / √n). With 3–5 seeds they are wide, so use the paired per-seed comparisons in §5 to judge small differences.

| Figure | What it shows | How to read it / key takeaway |
|---|---|---|
| `sanity_pdr_vs_power.png` | Beacon PDR during jamming vs jammer power (highway, no mitigation). The dashed line is the no-jammer run | Must slope **down**: it does (1.00 → 0.51). Validates that the jammer model works before any other result can be trusted |
| `sanity_pdr_vs_distance.png` | Beacon PDR vs receiver distance to the jammer, before vs during jamming (20 dBm) | Shows the **jammed zone**: PDR 0.08 next to the jammer, back to about 1.0 beyond about 600 m. Explains why mitigation can only help near the edge |
| `ontime_by_config.png` | **Main result.** On-time reachability for every scenario × jammer × mitigation level (a–d) | Left panel: no harm without a jammer (urban baseline 0.70, §7.3). Middle and right: bars rise slightly from (a) to (d). Error bars overlap; use the paired table in §5.1 for the gain |
| `deadline_miss_by_config.png` | 1 − on-time reachability, same layout | Mirror image of the above. Easier to show "misses reduced from X to Y" |
| `latency_by_config.png` | Mean alert latency per configuration | L4 (d) **cuts latency** sharply on the jammed highway (20 → 3 ms). Repetition and rerouting (b, c) increase it |
| `beacon_pdr_by_config.png` | Beacon PDR from jam start onward | Shows the channel-level damage: urban worst (0.39), highway mildest (0.90). (d) rises because of L4's lower beacon rate |
| `routing_by_scenario.png` | Three L3 panels: NO_RELAY rate, path switches per alert, route recovery time (levels c and d) | Local road: NO_RELAY about 60–65% (nothing to reroute through). Urban: relays almost always available, but about 0.8 path switches per alert and about 22 ms recovery (the first choice often fails). Highway: in between |
| `ontime_vs_density.png` | Reachability vs number of vehicles, per scenario, per mitigation level | Highway: the gap between (a) and (d) **widens with density**, and (c) dips at 120 vehicles. Local and urban: lines are close together at every density |
| `rsu_effect.png` | Reachability with and without an RSU (levels c, d) on highway and local | Small differences. Read together with the NO_RELAY table in §5.7, which shows the RSU is essential for L3 on the highway |
| `beacon_rate.png` | Left: detector TPR vs L4 beacon rate while jammed (2/5/10 Hz) per scenario. Right: on-time reachability for the same runs (constant jammer, mitigation d) | Left: urban recovers by 5 Hz, local needs 10 Hz, highway is flat. Right: flat lines, except the highway drop at 10 Hz. See §5.8 |
| `urban_radius.png` | Urban on-time reachability per mitigation level, 200 m (default) vs 150 m target radius, for each jammer type | Left panel: the no-jammer baseline jumps 0.70 → 0.98 at 150 m. Middle and right: everything shifts up by a similar factor; the mitigation pattern (a → d) is the same. See §5.9 |
| `edge_relay.png` | Top: on-time reachability; bottom: NO_RELAY rate, for no RSU / RSU, without and with edge relays, at levels (c) and (d), on highway and local | Highway, bottom: edge relays cut NO_RELAY from 100% to about 60%. Top: (c) gets **worse** with edge relays; (d) is unchanged. Local: flat. See §5.10 |
| `detector_states.png` | Stacked bars: share of time the detector said normal / congested / weak / jammed, per scenario × condition (mitigation 0) | Shows confounders are **not** mistaken for jamming: congestion is orange, weak is green, red appears only with a jammer. (The x-axis labels overlap slightly; the same data is in `detector_accuracy.csv`) |
| `detector_accuracy.csv` | TPR, FPR, delay and state shares per scenario × condition (mitigation 0) | Table form of §5.5 |
| `demo/demo_highway_m0.gif`, `demo_highway_m3.gif` | Animation of highway seconds 8–25, constant jammer, without and with full mitigation | For the presentation: shows vehicles, the jammer's zone and alert propagation. Compare side by side |

---

## 7. Anomalies, failures and caveats

### 7.1 ns-3.47 crash: found and fixed
In the unpatched 24 Nov batch, `confounders/highway_jam-none_conf-congestion_m0_s2` aborted at simulated time 8.59 s inside ns-3's WiFi interference model:

```
aborted. cond="it == niIt->second.end()", file=src/wifi/model/interference-helper.cc, line=465
  in InterferenceHelper::CalculateNoiseInterferenceW  ← PhyEntity::EndOfMpdu
```

- **It was an ns-3.47 bug, not ours.** In a debug build, the same run fails much earlier (0.82 s) at `NS_ASSERT(!m_wifiPhy->m_currentEvent)` in `PhyEntity::StartReceivePreamble`. With `NS_LOG` on node 33 we saw the sequence:
  1. the radio locks onto a frame's preamble (−83 dBm), which keeps it in CCA_BUSY;
  2. 0.23 µs later a too-weak frame from a distant car arrives, and ns-3 re-checks CCA using energy detection only (−62 dBm);
  3. that check ignores the frame being decoded and sets the radio **IDLE mid-reception**;
  4. the next frame starts a second reception, and the interference helper prunes the first frame's history, so ns-3 aborts when the first frame ends.
- The same abort was reported by another ns-3.46/3.47 user ([ns-3-users thread](https://groups.google.com/g/ns-3-users/c/tCgI2wx1fcs)). No upstream fix existed when we checked.
- **Fix:** one condition in `PhyEntity::SwitchMaybeToCcaBusy()`: don't return to IDLE while a frame is still being received. That's already how ns-3 behaves once the payload starts. Full write-up: [`patches/README.md`](../patches/README.md). Apply it with `bash scripts/apply_ns3_patch.sh`.
- **Result:** the run now completes (debug and optimized builds). The highway-congestion cell has all **3 seeds**: on-time at m0 is 0.868 (the repaired seed 2 is the worst seed, 0.768), and FPR is still 4.7%.
- **Impact on everything else is negligible** (test 7b in §4): 316/362 runs are identical, and the 46 that changed are dense or congested runs, where the bug window occurs, with changes ≤ 0.03 per run.

### 7.2 Run times and machine stalls
A normal run takes 5–60 s of real time (urban and local about 5–9 s, highway about 30–60 s). In the 24 Nov batch a few runs took 650–3550 s. They always came **in pairs that finished within 2 s of each other**, and their outputs (event counts, file sizes) are identical in shape to their fast siblings. This is the machine sleeping or WSL being suspended, **not** a simulation problem. **The results are unaffected.** One pair of sanity runs in this batch took 1111 s each (finishing together, same pattern). The full batch took 69 min of wall time (132 min on 24 Nov, which had more stalls).

### 7.3 Urban baseline is only 0.70 without a jammer
With no jammer, the urban scenario delivers alerts on time to only **70%** of targets (beacon PDR also 0.70). That is because the urban scenario uses path-loss exponent 3.0 + Nakagami fading (modelling buildings), while the target radius is 200 m for every scenario. Many "targets" are simply out of reliable radio range, especially across the intersection.
- **Consequence:** compare urban jammed results against **0.70**, not 1.0. Under a constant jammer, urban reaches 0.28–0.32, i.e. about 40–46% of its own baseline.
- **Tested:** with a 150 m radius the urban baseline is 0.98 and every conclusion is unchanged (§5.9).

### 7.4 Detector false-positive rate at 30 dBm
In the sanity sweep, the detector FPR rises to **0.40 at 30 dBm**, from at most 0.03 at 20 dBm and below. Ground truth calls a node "jammed" only if the jammer's **deterministic** (no-fading) received power is ≥ −92 dBm. The most likely explanation: at 30 dBm, nodes just outside that boundary still see a strongly raised noise floor (fading, plus the jammer's sheer power), and the detector, correctly from their point of view, reports jamming. If so, this is a property of the ground-truth definition rather than a detector bug. It has not been verified node by node. It only affects the 30 dBm sanity point; every other experiment uses 20 dBm.

### 7.5 Highway relay vehicles never selected without an RSU
See §5.7: `rsu=0` on the highway gives NO_RELAY = 100%. On the local road, without an RSU, relays are sometimes found (20%). **This is by design, not a bug.** `relay.cc` (`Router`, relay-vehicle loop) skips any neighbour that reports jamming or whose link is jammed, because such a vehicle is itself inside the jammed area. Alert sources are picked within 250 m of the jammer, whose reach is about 600 m, so on a straight highway every neighbour a source can hear is also jammed. The `NO_RELAY` events confirm it: across 168 events, sources had a median of 6 neighbours (0–30), and none was eligible (`tried=0`). Only the RSU, with its wired backhaul, leads out of the zone. **Tested:** allowing jammed neighbours nearer the zone edge (`--relayEdge=1`) finds relays for about 40% of highway alerts, but doesn't improve delivery and makes it worse without L4 (§5.10). The original rule is kept.

### 7.6 Local-road congestion confounder doesn't congest
See §5.5. Twelve vehicles plus congestion traffic stays below the busy threshold, so this cell does not test the detector's congestion logic.

### 7.7 General modelling limitations
These are inherited from the design (main README §11):
- ad-hoc 802.11p MAC without EDCA;
- synthetic constant-velocity mobility, not SUMO;
- RSU backhaul as a fixed 2 ms delay;
- a threshold detector;
- simulation only.

Small seed counts (3–5) mean the gains of about 0.01–0.02 are within noise **unless** they are consistent across paired seeds (§5.1).

---

## 8. Comparison with the earlier ns-3.40 results

The main README reported first-look numbers from ns-3.40 (macOS, one seed). On ns-3.47, with 5 seeds:

| Check | ns-3.40 (1 seed) | ns-3.47 (this batch) | Agreement |
|---|---|---|---|
| Sanity PDR −10 → 30 dBm | 1.00 → 0.50 | 0.997 → 0.506 | ✅ same |
| PDR near jammer (< 50 m) | 0.08 | 0.08 | ✅ same |
| Unaffected beyond | about 600 m | about 600 m | ✅ same |
| Detector, constant jammer (highway) | TPR 0.96 / FPR 0.02 | TPR 0.97 / FPR 0.03 | ✅ |
| Detector, reactive jammer | TPR 0.26 | TPR 0.23 (highway) – 0.40 (urban) | ✅ |
| Confounder false alarms | ≤ 0.5% | 0–0.1%, except highway congestion **4.7%** | ⚠️ congestion FPR higher (3 seeds) |
| Highway on-time m0 → m3 | 0.50 → 0.56 | 0.48 → 0.57 | ✅ |
| Local on-time m0 → m3 | 0.55 → 0.56 | 0.50 → 0.53 | ✅ (similar small gain) |
| Local NO_RELAY at m2 | 89% | 80% | ✅ |
| Stale accepted, no L4 → L4 | 3.5% → 0% | ≤1.0% → 0% | ✅ (L4 eliminates it) |
| L4 lowers local TPR | 0.97 → 0.39 | 0.89 → 0.51 | ✅ same trade-off |

The ns-3.40 conclusions hold on ns-3.47 and across seeds.

---

## 9. Suggested next steps

1. ~~Detector vs L4 trade-off~~ ✅ done (§5.8). **Team decision:** keep 2 Hz, switch the default to 5 Hz, or make the jammed beacon rate density-adaptive. Changing the default means re-running the presets.
2. **Reactive-jammer detection:** TPR 0.23–0.40 is the weakest point. Try lower `--errThr` / `--enterCount` (calibration, main README §10), then re-run the presets.
3. ~~Highway relay selection~~ ✅ tested (§5.10): edge relays don't help, so the default is kept. Optional: a true multi-hop escape.
4. ~~Urban target radius~~ ✅ tested (§5.9): 150 m gives a 0.98 baseline and the same conclusions. Pick which radius to quote in the report.
5. ~~Replace the crashed seed~~ ✅ not needed: ns-3 bug fixed (§7.1).
6. **More seeds** (e.g. 10) for `main` if the report needs confidence intervals that don't overlap.

---

## 10. How to reproduce

```bash
# ns-3.47 with the code copied into scratch/ (see main README §3) AND our patch applied
bash scripts/apply_ns3_patch.sh ~/ns-3.47 --no-build
cd ~/ns-3.47 && ./ns3 configure --build-profile=optimized --disable-werror && ./ns3 build

cd ~/cns-project/v2v-jam-sim
python3 run_batch.py --ns3 ~/ns-3.47 --preset smoke
python3 run_batch.py --ns3 ~/ns-3.47 --preset sanity --jobs 2
python3 run_batch.py --ns3 ~/ns-3.47 --preset all    --jobs 2   # 471 runs incl. beaconrate, urbanradius, edgerelay; skips finished runs
python3 analysis/metrics.py --results results
python3 analysis/plots.py   --results results

# demo animation
B=~/ns-3.47/build/scratch/v2v-jam/ns3.47-v2v-jam-optimized
for m in 0 3; do
  $B --scenario=highway --jammer=constant --mitigation=$m --seed=1 --posLog=1 \
     --outDir=$PWD/results/demo/highway_jam-constant_m${m}_s1_poslog
  python3 analysis/animate.py --run results/demo/highway_jam-constant_m${m}_s1_poslog \
     --out results/demo/demo_highway_m${m}.gif
done
```

Runs are deterministic for a given seed, ns-3 version and patch set, so these commands reproduce the numbers above exactly. Without the patch, numbers differ slightly and one run aborts.
