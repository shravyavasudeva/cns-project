# ns-3.47 patches

ns-3.47 has a bug that can abort a simulation when the WiFi channel is saturated. We fixed it with a one-line patch. **Every teammate must apply it**, or some runs (in our batch, the `congestion` confounder) abort, and results are not comparable to the ones in `results/`.

```bash
bash scripts/apply_ns3_patch.sh            # ns-3 in ~/ns-3.47 (applies and rebuilds)
bash scripts/apply_ns3_patch.sh /path/to/ns-3.47
```
The script is safe to run more than once. `scripts/setup_ns3.sh` already runs it for new installs.

## `ns-3.47-cca-idle-during-rx.patch`

**File:** `src/wifi/model/phy-entity.cc`, `PhyEntity::SwitchMaybeToCcaBusy()`

**Symptom.** With an optimized build, a run aborts a few seconds in:
```
aborted. cond="it == niIt->second.end()", ... file=.../src/wifi/model/interference-helper.cc, line=465
   0# ns3::InterferenceHelper::CalculateNoiseInterferenceW(...)
```
With a debug build it fails much earlier, and closer to the cause:
```
NS_ASSERT failed, cond="!m_wifiPhy->m_currentEvent", ... phy-entity.cc, line=493   (StartReceivePreamble)
```
It is deterministic. In our batch it hit exactly one run (`confounders/highway_jam-none_conf-congestion_m0_s2`: 60 vehicles, 50 Hz × 500-byte beacons). The same abort has been reported by another ns-3.46/3.47 user with multi-hop 802.11n traffic ([ns-3-users thread](https://groups.google.com/g/ns-3-users/c/tCgI2wx1fcs)). No upstream fix existed when we checked (Nov 2026).

**Cause (found with a debug build and `NS_LOG`).**
1. A receiver locks onto a frame's preamble. From then until the payload starts, ns-3 keeps the PHY in `CCA_BUSY` (not `RX`) and remembers the frame in `m_currentEvent`.
2. During that window a weak frame from a distant vehicle arrives (below the RX sensitivity). `SpectrumWifiPhy::StartRx` adds it as interference and calls `SwitchMaybeToCcaBusy()` *without* a PPDU.
3. That function re-checks CCA using **energy detection only** (−62 dBm). The frame being decoded was at −83 dBm: decodable, but below the energy threshold. So the function concluded the channel was free and set the PHY to **IDLE**, in the middle of a reception.
4. The next frame to arrive saw an IDLE PHY and started a second reception. Because the interference helper believed nothing was being received, it pruned the history of the first frame.
5. When the first frame finished, ns-3 looked up its start time in that pruned history and aborted.

**Fix.** Don't end the CCA-busy period while a frame is still being received:
```cpp
-    if (m_wifiPhy->IsStateCcaBusy())
+    if (m_wifiPhy->IsStateCcaBusy() && !m_wifiPhy->m_currentEvent)
```
This matches what ns-3 already does once the payload starts: in the `RX` state, `WifiPhyStateHelper::SwitchMaybeToCcaBusy` ignores CCA updates. `m_currentEvent` is cleared whenever a reception ends, aborts or fails its header, so the PHY still returns to IDLE normally afterwards.

**Effect on results.** It only changes behaviour in the rare window when a reception is in progress and a sub-threshold signal arrives. Before the fix, the PHY could wrongly start a parallel reception, which isn't physically possible on a single radio. All committed results in `results/` (Nov 25, 2026) were produced with the patch. The earlier, unpatched Nov 24 batch is in git history (commit `35d3e0e`).

**Verified:** the crashing configuration completes with a debug build and all ns-3 asserts enabled, and with the optimized build. The smoke and sanity checks pass.
