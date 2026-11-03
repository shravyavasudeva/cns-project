/*
 * common.h - Shared definitions used by every module of the v2v-jam simulation.
 *
 *   - Params      : every tunable setting (filled from command-line flags in main.cc)
 *   - V2vHeader   : the one packet header all messages carry (beacons and alerts)
 *   - EventLog    : the one CSV event log every module writes to
 *   - NodeRegistry: id -> node / device / mobility / kind lookup
 *
 * Project: Cross-Layer Resilience for Jammed V2V Safety Communication (ECE 6612)
 */
#ifndef V2V_COMMON_H
#define V2V_COMMON_H

#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"

#include <fstream>
#include <string>
#include <vector>

namespace ns3
{
namespace v2v
{

// EtherType used for all our frames (IEEE 1609.3 WSMP uses 0x88DC).
static const uint16_t kV2vProtocol = 0x88DC;
// dstId value meaning "broadcast to everyone in range".
static const uint32_t kBroadcastId = 0xFFFFFFFF;

enum MsgType : uint8_t
{
    MSG_BEACON = 0, // periodic status message (position, speed), ROUTINE priority
    MSG_ALERT = 1   // emergency warning (e.g. hard braking), EMERGENCY priority
};

enum Priority : uint8_t
{
    PRIO_ROUTINE = 0,
    PRIO_EMERGENCY = 1
};

enum PathType : uint8_t
{
    PATH_DIRECT = 0, // sent by the source itself
    PATH_RSU = 1,    // delivered via a Road-Side Unit
    PATH_RELAY = 2   // delivered via a relay vehicle
};

enum NodeKind : uint8_t
{
    KIND_VEHICLE = 0,
    KIND_RSU = 1,
    KIND_JAMMER = 2,
    KIND_INTERFERER = 3
};

const char* PathName(uint8_t p);
const char* KindName(uint8_t k);

/* ------------------------------------------------------------------------ */
/*  Params: all simulation settings. Defaults marked "auto" are filled in by */
/*  ApplyScenarioDefaults() depending on --scenario.                          */
/* ------------------------------------------------------------------------ */
struct Params
{
    // --- experiment selection ---
    std::string scenario = "highway"; // highway | local | urban
    int mitigation = 3;               // 0 none, 1 detect(+repeat), 2 +L3 reroute, 3 +L4 deadline/priority
    std::string jammer = "none";      // none | constant | reactive
    std::string confounder = "none";  // none | congestion | weak | interference
    uint32_t seed = 1;                // run number for the random generator
    double simTime = 40.0;            // seconds
    std::string outDir = "results/run";

    // --- road / traffic ---
    uint32_t vehicles = 0;  // auto: highway 60, local 12, urban 40
    double speed = -1;      // m/s, auto: highway 30, local 12, urban 10
    int rsu = -1;           // 1 = place RSUs, 0 = none. auto: highway 1, local 0, urban 1
    double rsuSpacing = -1; // m, auto: 500
    double roadLength = 0;  // m (set by scenario)

    // --- radio ---
    double txPower = 23.0;     // dBm, vehicle transmit power
    double rsuTxPower = 30.0;  // dBm, RSU transmit power
    double pathLossExp = -1;   // auto: highway 2.5, local 2.7, urban 3.0
    double refLoss = 47.8;     // dB at 1 m (free space at 5.9 GHz)
    bool fading = false;       // add Nakagami fading on top of log-distance
    double rxThreshold = -90;  // dBm, minimum RSSI for preamble detection / CCA
    uint32_t channel = 178;    // 802.11p control channel (5.890 GHz, 10 MHz)

    // --- jammer ---
    double jamPower = 20.0;   // dBm, jammer transmit power
    double jamX = -1;         // m, auto: hotspot (middle of the road)
    double jamY = -1;         // m
    double jamStart = 10.0;   // s
    double jamStop = -1;      // s, auto: simTime
    double reactDelayUs = 20; // reactive jammer: reaction delay after sensing a frame
    double burstUs = 200;     // reactive jammer: noise burst length
    double gtJamDbm = -92;    // ground truth: a node counts as "under jamming" if
                              // the jammer's received power there is >= this

    // --- confounders ---
    double interfPower = 10.0;   // dBm, non-malicious interferer power
    double interfPeriodMs = 20;  // interferer period
    double interfDuty = 0.1;     // interferer duty cycle (fraction of period on air)

    // --- safety application ---
    double beaconHz = 10;         // routine beacon rate
    uint32_t beaconBytes = 200;   // beacon size on the wire (header + padding)
    uint32_t alertBytes = 100;    // alert size on the wire
    double alertStart = 12.0;     // s, first emergency alert
    double alertInterval = 1.0;   // s between alerts
    double alertRadius = 200;     // m, vehicles inside this radius are the alert's targets
    double alertSrcRadius = 250;  // m, alert sources are picked near the hotspot
    double deadlineMs = 100;      // safety deadline for every message
    double repeatMs = 20;         // mitigation>=1: re-broadcast interval for an alert
    double pdrRadius = 200;       // m, beacon PDR counts receivers inside this radius

    // --- detector (Layer 1/2) ---
    double detWindow = 0.5;     // s, sliding window
    double detPeriod = 0.1;     // s, evaluation period
    double busySampleMs = 2;    // ms, channel state sampling period
    double edBusyThr = 0.8;     // busy ratio meaning "channel flooded by energy"
    double congBusyThr = 0.5;   // busy ratio meaning "congested"
    double ccaJamThr = 0.3;     // CCA_BUSY-only ratio (non-frame energy) that indicates a jammer
    double pdrThr = 0.5;        // neighbour PDR below this = degraded
    double errThr = 0.3;        // fraction of detected frames that failed to decode
    double noiseThr = -92;      // dBm, noise+interference above this = elevated
    double linkRange = 200;     // m, neighbours predicted closer than this should be heard
    uint32_t minLinkJam = 2;    // jammed links needed to suspect jamming without local evidence
    uint32_t enterCount = 2;    // consecutive "jammed" evaluations to raise the flag
    uint32_t exitCount = 3;     // consecutive "clean" evaluations to clear it

    // --- routing (Layer 3) ---
    double ackTimeoutMs = 15;   // wait to overhear the relay before declaring a route break
    uint32_t maxSwitches = 3;   // max path switches per alert
    double backhaulMs = 2;      // RSU-to-RSU wired backhaul latency
    double relayMinRssi = -88;  // dBm, minimum link quality to use a neighbour as relay

    // --- Layer 4 ---
    double beaconHzJam = 2;     // beacon rate while jamming is suspected (mitigation 3)
    double serviceMs = 1;       // L4 queue service tick

    // --- outputs ---
    bool posLog = false; // write positions.csv every 0.5 s (for animate.py)
    bool anim = false;   // write NetAnim XML

    // --- derived (do not set from the command line) ---
    Vector hotspot;
};

Params& P();
void ApplyScenarioDefaults(Params& p);
std::string ParamsToJson(const Params& p);

/* ------------------------------------------------------------------------ */
/*  V2vHeader: the single header every packet carries.                       */
/* ------------------------------------------------------------------------ */
class V2vHeader : public Header
{
  public:
    V2vHeader();
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    void Print(std::ostream& os) const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;

    // Public fields keep the code short and readable.
    uint8_t type = MSG_BEACON;
    uint8_t priority = PRIO_ROUTINE;
    uint8_t pathType = PATH_DIRECT;
    uint8_t hopCount = 0;
    uint8_t flags = 0;        // bit0: sender is an RSU, bit1: sender suspects jamming
    uint32_t seq = 0;         // beacon sequence number, or alert id for alerts
    uint32_t srcId = 0;       // original creator of the message
    uint32_t senderId = 0;    // node that transmitted this copy (last hop)
    uint32_t dstId = kBroadcastId;
    uint64_t genTimeNs = 0;   // creation time at the source
    uint32_t deadlineUs = 0;  // relative deadline (message is stale after genTime+deadline)
    float posX = 0, posY = 0; // sender position (alerts: source position at creation)
    float velX = 0, velY = 0; // sender velocity
    uint16_t rateDeciHz = 0;  // sender's current beacon rate x10

    static const uint8_t FLAG_RSU = 0x01;
    static const uint8_t FLAG_JAM = 0x02;

    Time GenTime() const { return NanoSeconds(genTimeNs); }
    Time Deadline() const { return MicroSeconds(deadlineUs); }
    bool Expired(Time now) const { return now - GenTime() > Deadline(); }
};

/* ------------------------------------------------------------------------ */
/*  EventLog: events.csv  -> time_ms,event,node,seq,extra                    */
/*  'extra' is a ';'-separated list of key=value pairs (never contains ',').  */
/* ------------------------------------------------------------------------ */
class EventLog
{
  public:
    static void Open(const std::string& path);
    static void Close();
    static void Log(const std::string& event, uint32_t node, int64_t seq, const std::string& extra = "");

  private:
    static std::ofstream s_out;
};

/* ------------------------------------------------------------------------ */
/*  Node registry                                                            */
/* ------------------------------------------------------------------------ */
class SafetyApp; // forward

struct NodeInfo
{
    Ptr<Node> node;
    Ptr<NetDevice> dev; // wifi device (null for noise sources)
    Ptr<MobilityModel> mob;
    NodeKind kind = KIND_VEHICLE;
    SafetyApp* app = nullptr;
};

std::vector<NodeInfo>& Registry();
NodeInfo& Info(uint32_t id);
Vector PosOf(uint32_t id);
double DistBetween(uint32_t a, uint32_t b);
double Dist2D(const Vector& a, const Vector& b);
std::vector<uint32_t> VehicleIds();
std::vector<uint32_t> RsuIds();

} // namespace v2v
} // namespace ns3

#endif // V2V_COMMON_H
