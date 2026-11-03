/*
 * stats.cc - Beacon statistics (see stats.h).
 */
#include "stats.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>

namespace ns3
{
namespace v2v
{

namespace
{
struct Sec
{
    uint64_t generated = 0;
    uint64_t expected = 0;
    uint64_t received = 0;
    double latSum = 0;
    uint64_t latCnt = 0;
    double jitSum = 0;
    uint64_t jitCnt = 0;
    uint64_t bytes = 0;
    uint64_t dropped = 0;
};

const double kBin = 50.0;  // metres per distance bin
const int kNumBins = 30;   // 0 .. 1500 m

Sec g_sec;
std::ofstream g_out;
std::map<uint64_t, double> g_lastLat; // (src,rx) -> last latency, for jitter
uint64_t g_exp[2][kNumBins] = {};     // [phase][bin], phase 0 = before jamming, 1 = during
uint64_t g_rcv[2][kNumBins] = {};

int
Phase()
{
    double t = Simulator::Now().GetSeconds();
    return (t >= P().jamStart && t < P().jamStop) ? 1 : 0;
}

int
JamBin(uint32_t node)
{
    Vector p = PosOf(node);
    double d = std::hypot(p.x - P().jamX, p.y - P().jamY);
    int b = int(d / kBin);
    return b < kNumBins ? b : -1;
}

void
Flush()
{
    double t = Simulator::Now().GetSeconds();
    char line[256];
    double pdr = g_sec.expected ? double(g_sec.received) / g_sec.expected : 0;
    double lat = g_sec.latCnt ? 1000 * g_sec.latSum / g_sec.latCnt : 0;
    double jit = g_sec.jitCnt ? 1000 * g_sec.jitSum / g_sec.jitCnt : 0;
    double kbps = g_sec.bytes * 8 / 1000.0;
    std::snprintf(line, sizeof(line), "%.0f,%llu,%llu,%llu,%.4f,%.3f,%.3f,%.2f,%llu\n", (t - 1) * 1000,
                  (unsigned long long)g_sec.generated, (unsigned long long)g_sec.expected,
                  (unsigned long long)g_sec.received, pdr, lat, jit, kbps,
                  (unsigned long long)g_sec.dropped);
    g_out << line;
    g_sec = Sec();
    Simulator::Schedule(Seconds(1), &Flush);
}
} // namespace

void
Stats::Init(const std::string& outDir)
{
    g_out.open(outDir + "/beacons.csv");
    g_out << "time_ms,generated,expected,received,pdr,lat_ms,jitter_ms,goodput_kbps,dropped_deadline\n";
    Simulator::Schedule(Seconds(1), &Flush);
}

void
Stats::BeaconGenerated(uint32_t src)
{
    g_sec.generated++;
    int ph = Phase();
    for (uint32_t v : VehicleIds())
    {
        if (v == src || DistBetween(src, v) > P().pdrRadius)
            continue;
        g_sec.expected++;
        int b = JamBin(v);
        if (b >= 0)
            g_exp[ph][b]++;
    }
}

void
Stats::BeaconReceived(uint32_t src, uint32_t rx, double latencyS, uint32_t bytes)
{
    if (Info(src).kind != KIND_VEHICLE || Info(rx).kind != KIND_VEHICLE ||
        DistBetween(src, rx) > P().pdrRadius)
        return;
    g_sec.received++;
    g_sec.latSum += latencyS;
    g_sec.latCnt++;
    g_sec.bytes += bytes;
    uint64_t key = (uint64_t(src) << 32) | rx;
    auto it = g_lastLat.find(key);
    if (it != g_lastLat.end())
    {
        g_sec.jitSum += std::fabs(latencyS - it->second);
        g_sec.jitCnt++;
    }
    g_lastLat[key] = latencyS;
    int b = JamBin(rx);
    if (b >= 0)
        g_rcv[Phase()][b]++;
}

void
Stats::BeaconDroppedDeadline()
{
    g_sec.dropped++;
}

void
Stats::Finish(const std::string& outDir)
{
    g_out.close();
    std::ofstream o(outDir + "/pdr_by_jamdist.csv");
    o << "dist_m,phase,expected,received,pdr\n";
    for (int ph = 0; ph < 2; ++ph)
        for (int b = 0; b < kNumBins; ++b)
        {
            if (g_exp[ph][b] == 0)
                continue;
            o << b * kBin << ',' << (ph ? "during" : "before") << ',' << g_exp[ph][b] << ','
              << g_rcv[ph][b] << ',' << double(g_rcv[ph][b]) / g_exp[ph][b] << '\n';
        }
}

} // namespace v2v
} // namespace ns3
