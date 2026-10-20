# Team work map

| Area | Primary files / responsibility | Interface to preserve |
| --- | --- | --- |
| Scenario and mobility | `scratch/vanet-jamming.cc`, node placement and movement | Expose vehicle count, road type, and seed as command-line options. |
| Jammer | jammer application/model | Record jammer state changes; do not hard-code scenario timings outside options. |
| Detection | detector class/logging | Emit one jam flag per link/window with timestamp and reason. |
| L3 rerouting | alert forwarding logic | Accept an alert and next-hop selection; keep direct delivery available for comparison. |
| L4 and metrics | queue/deadline logic and CSV output | Include alert ID, send time, receive/drop time, outcome, and path. |

Shared conventions:

- Time units are milliseconds in CSV output and seconds in ns-3 command-line arguments.
- Emergency alert deadline defaults to 100 ms.
- Use a fixed RNG seed/run when debugging; report both in experiment results.
- Do not commit `results/`, pcap files, or ns-3 build output.
