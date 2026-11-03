/*
 * stats.h - Beacon statistics (PDR, latency, jitter, goodput) computed inside the
 * simulation, because logging every beacon would create huge files.
 *
 * Output files (in --outDir):
 *   beacons.csv          one row per simulated second (time_ms = start of that second):
 *                        time_ms,generated,expected,received,pdr,lat_ms,jitter_ms,goodput_kbps,dropped_deadline
 *   pdr_by_jamdist.csv   beacon PDR binned by the receiver's distance to the jammer,
 *                        before and during jamming (the "does the jammer hurt?" sanity check)
 *
 * Definitions:
 *   expected = for every generated beacon, the number of vehicles within pdrRadius of the sender
 *   received = beacons received by vehicles within pdrRadius of the sender
 *   pdr      = received / expected
 */
#ifndef V2V_STATS_H
#define V2V_STATS_H

#include "common.h"

namespace ns3
{
namespace v2v
{

class Stats
{
  public:
    static void Init(const std::string& outDir);
    static void BeaconGenerated(uint32_t src);
    static void BeaconReceived(uint32_t src, uint32_t rx, double latencyS, uint32_t bytes);
    static void BeaconDroppedDeadline();
    static void Finish(const std::string& outDir);
};

} // namespace v2v
} // namespace ns3

#endif
