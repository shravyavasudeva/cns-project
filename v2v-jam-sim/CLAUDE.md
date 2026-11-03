# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Project
ECE 6612 course project, *Cross-Layer Resilience for Jammed V2V Safety Communication*. It is an ns-3 simulation that tests whether Layer 3 rerouting (RSU or relay vehicle) and Layer 4 deadline/priority handling, added on top of PHY/MAC jamming detection, improve on-time delivery of V2V emergency alerts under RF jamming. It compares highway, local-road and urban settings (the professor asked for diverse vehicle settings: dense and fast vs sparse with few relays). `README.md` is the full user documentation. Keep it in sync when behaviour, flags, events or files change.

## Environment
- **ns-3.47 exactly** (CMake build, `./ns3` script, C++17). Team members run it in **WSL2 Ubuntu 22.04** on Windows. The C++ is plain ns-3 and is OS independent.
- The C++ lives in `scratch/v2v-jam/` in this repo. ns-3 compiles a **copy** at `~/ns-3.47/scratch/v2v-jam/`. After editing, run `bash scripts/sync_code.sh [ns3-dir]`, which rsyncs and runs `./ns3 build`.
- Build: `cd ~/ns-3.47 && ./ns3 build`. Run: `./ns3 run "v2v-jam --flag=value ..."`. Show flags: `./ns3 run "v2v-jam --PrintHelp"`.
- Configure (once): `./ns3 configure --build-profile=optimized --disable-werror --enable-modules="core;network;mobility;propagation;spectrum;wifi;netanim"`. Use `--build-profile=debug` when chasing crashes (enables NS_ASSERT / NS_LOG).
- Python analysis: `pip install -r requirements.txt` (pandas, numpy, matplotlib, pillow).
- Tested on **ns-3.40** (macOS, Apple clang 14): it builds cleanly and runs. It has not yet been built on ns-3.47, but its APIs were checked against the 3.47 headers. When fixing build errors, check the real signature in `~/ns-3.47/src/<module>/model/*.h` instead of guessing, and make the smallest fix.
- ns-3.40 needs a one-line patch to run 10 MHz spectrum PHYs (`OfdmPhy::GetMeasurementChannelWidth` must return `min(channelWidth, 20)` instead of `20`, see README Troubleshooting). ns-3.47 already has it.
- Detector design notes from testing:
  - Congestion and jamming are separated by *CCA_BUSY-only* time (non-frame energy) plus an elevated noise floor.
  - Beacons advertise only *local* jamming (`LocalJammed()`), never `JamSuspected()`. Advertising suspicion caused network-wide false alarms.
  - Neighbours predicted off the road (wrap-around) are ignored for link-jam evidence.

## Current status
See **README.md → "Test results so far"** and **"Next steps"**. The README is the single place for team-facing status. Keep it updated when something important changes (e.g. the first ns-3.47 build, detector calibration, the full batch run).
- **This repo is the source of truth.** After editing `scratch/v2v-jam/`, sync it into the ns-3 tree with `bash scripts/sync_code.sh <ns3-dir>` (or copy the files) and rebuild.

## Layout and responsibilities
| File | Role |
|---|---|
| `scratch/v2v-jam/v2v-jam.cc` | `main()`: flags → `Params`, nodes, spectrum channel + 802.11p devices, apps, jammers, `AlertTick`, `GroundTruthTick`, final summaries. **The only file that may contain the text `main(`.** ns-3's scratch CMake picks the program name from the file matching `main[( ]`, so don't write "main(" or "main " in comments elsewhere |
| `common.h/.cc` | `Params` + `P()` + `ApplyScenarioDefaults()`, `V2vHeader`, `EventLog`, node registry (`Info(id)`, `VehicleIds()`, `RsuIds()`) |
| `scenario.h/.cc` | road layouts, ConstantVelocity mobility with wrap-around, RSU positions, optional positions.csv |
| `jammer.h/.cc` | `ConstantJammer`, `ReactiveJammer` (PhyRxPayloadBegin-triggered), `Interferer`, all built on `WaveformGenerator` |
| `detector.h/.cc` | L1/2 detector: window bins, `Classify()`, hysteresis, `JamSuspected()`, `JamCenter()`, accuracy counters |
| `neighbor-table.h` | header-only neighbour table, `PredictPos`, `LinkLifetime` |
| `relay.h/.cc` | L3 `Router`: RSU-first / scored relay choice, implicit ACK, ROUTE_BREAK / PATH_SWITCH |
| `safety-app.h/.cc` | `SafetyApp` (ns-3 Application): beacons, alerts, L4 queue (`Submit`/`ServiceQueue`), receive path, relay/RSU forwarding, backhaul |
| `stats.h/.cc` | per-second beacon counters → beacons.csv, pdr_by_jamdist.csv |
| `run_batch.py` | presets → parallel runs into `results/<preset>/<run-name>/`, skips finished runs |
| `analysis/metrics.py` | run folders → `summary_runs.csv`, `summary_grouped.csv` (metric definitions in its docstring) |
| `analysis/plots.py` | summary → `results/figures/*.png` |
| `analysis/animate.py` | one run (with `--posLog=1`) → GIF |

## Conventions and invariants (do not break these)
- **One header, one log.** Every packet carries `V2vHeader`. Every metric comes from the output files (`events.csv`, `beacons.csv`, `node_summary.csv`, `pdr_by_jamdist.csv`), never from ad-hoc printing.
  - **All times in output CSVs are milliseconds** (column names end in `_ms`). Command-line time flags stay in seconds unless named `...Ms`/`...Us`. `events.csv` columns are `time_ms,event,node,seq,extra`. `extra` is `key=value;key=value` and must **never contain commas**.
  - If you add an event type or a field, update `analysis/metrics.py` and the event table in `README.md`.
- **Mitigation levels are cumulative:** 0 none → 1 detect + repeat → 2 + L3 → 3 + L4. Gate new behaviour with `P().mitigation >= N`. The detector must keep running at every level (its accuracy is measured even at level 0).
- **Fair comparisons:** mobility uses RNG stream 2000 and alert-source choice uses stream 1000, so all mitigation levels with the same `--seed` see identical traffic and alert sources. Don't consume those streams elsewhere, and don't make mobility depend on mitigation.
- **Node ids:** vehicles `0..V-1`, then RSUs, then jammer, then interferer (creation order in `v2v-jam.cc`). `Registry()` is indexed by ns-3 node id.
- **All tunables live in `Params`** with a matching `cmd.AddValue` in `v2v-jam.cc`. Also add new ones to `ParamsToJson()` if analysis needs them. No magic numbers in module code except clearly named file-local constants.
- **Alerts:** `seq` = global alert id, `srcId` = creator, `senderId` = last hop, `posX/posY` = source position (relays keep it). Receivers log only the **first** copy (`m_seenAlerts`). Targets are logged by `AlertTick` as `ALERT_TARGET`.
- Frames go directly on the WiFi NetDevice with EtherType `0x88DC`. There is no Internet stack. Don't add IP/UDP unless the whole team agrees (it would add ARP delays to unicast relaying).
- Use ns-3.47 APIs: `EventId::IsExpired()` (exists in both 3.40 and 3.47; `IsRunning()` was removed in 3.47 and `IsPending()` does not exist in 3.40, so the code also builds on ns-3.40 for local testing on older Macs), `WifiPhy::GetFrequency()` / `GetChannelWidth()` return `MHz_u` (an alias of `double`), `SignalNoiseDbm` fields are `dBm_u` (also `double`). Trace signatures are as in `wifi-phy.h` / `wifi-phy-state-helper.h` of 3.47. ns-3.47 builds with C++23 and needs g++ ≥ 11.1 and CMake ≥ 3.20 (Ubuntu 22.04 is fine).
- Code style: ns-3 style (4-space indent, braces on new lines for functions, `m_` members), short comments explaining *why*.

## Typical tasks
- **Fix a build error:** read the compiler message, open the cited ns-3 header in `~/ns-3.47/src/...`, adjust our call, then `bash scripts/sync_code.sh`.
- **Smoke test after any change:** `./ns3 run "v2v-jam --scenario=highway --jammer=constant --mitigation=3 --vehicles=20 --simTime=15 --alertStart=11 --outDir=results/smoke"` then `python3 analysis/metrics.py --run ~/ns-3.47/results/smoke`. Check that ontime_ratio, beacon_pdr and det_tpr are numbers, not NaN.
- **Sanity check the jammer:** `python3 run_batch.py --ns3 ~/ns-3.47 --preset sanity` then `python3 analysis/plots.py`. PDR must fall as `jamPower` rises.
- **Calibrate the detector:** change thresholds via flags first (`--noiseThr`, `--pdrThr`, `--errThr`, `--edBusyThr`, `--congBusyThr`, `--enterCount`). Change defaults in `common.h` only once agreed, then re-run all presets.
- **Add a scenario:** add a branch in `ApplyScenarioDefaults()` (common.cc), `BuildVehicleMobility()` and `RsuPositions()` (scenario.cc). Add it to `SCENARIOS` in `run_batch.py` and `SCEN_ORDER` in `plots.py`.
- **Add a metric:** compute it in `analysis/metrics.py` (`run_metrics`) from existing logs if possible. Only add a new event in C++ if the information isn't logged yet.

## Don'ts
- Don't log every beacon to `events.csv` (files explode). Aggregate in `stats.cc`.
- Don't change the output file formats without updating `metrics.py`, `plots.py` and `README.md` together.
- Don't commit `results/`, GIFs or ns-3 build folders.
