# Role B: jammer and sanity plots

`scratch/v2v-jam/jammer.{h,cc}` provides a same-channel interference source for ns-3.47.
It intentionally sends UDP packets via its assigned 802.11p `WifiNetDevice`, so its traffic
contends at the PHY/MAC layer with V2V safety traffic.

The standalone sanity harness additionally raises the jammer's CCA threshold so the jammer does
not politely defer to legitimate 802.11p traffic. Keep that setting for attack experiments; using
the normal CCA threshold models ordinary contention instead of deliberate jamming.

## Integration contract

Create the application on the jammer node after the shared 802.11p and IP setup:

```cpp
Ptr<JammerApplication> jammer = CreateObject<JammerApplication>();
jammer->Setup(InetSocketAddress(receiverAddress, safetyPort), JammerMode::CONSTANT);
jammerNode.Get(0)->AddApplication(jammer);
jammer->SetStartTime(Seconds(2));
jammer->SetStopTime(Seconds(duration));
```

For a reactive jammer, use `JammerMode::REACTIVE` and connect a *vehicle* PHY's
`PhyRxBegin` trace to `ObserveFrame`:

```cpp
auto vehiclePhy = DynamicCast<WifiNetDevice>(vehicleDevices.Get(0))->GetPhy();
vehiclePhy->TraceConnectWithoutContext("PhyRxBegin",
    MakeCallback(&JammerApplication::ObserveFrame, jammer));
```

Do not attach this callback to the jammer's own PHY. The model coalesces triggers during a
pending burst and exposes `PacketSize`, `Interval`, `ReactiveDelay`, `BurstSpacing`, and
`BurstPackets` as ns-3 attributes for experiments.

## Required week-1 sanity sweep

Before detection or rerouting work, vary jammer power and jammer-to-receiver distance. Record:

```text
jammer_mode,jammer_power_dbm,distance_m,tx,rx
constant,18,20,100,22
```

Then run:

```bash
export NS3_DIR=/home/shrav/ns-3.47
./scripts/run-pdr-sweep.sh
```

The two resulting figures must show a meaningful PDR reduction for the constant jammer.
If they do not, calibrate transmit power, packet interval, distance, and propagation loss before
any mitigation result is trusted.
