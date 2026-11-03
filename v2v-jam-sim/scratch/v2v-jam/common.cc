/*
 * common.cc - Implementation of the shared pieces declared in common.h.
 */
#include "common.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace ns3
{
namespace v2v
{

NS_OBJECT_ENSURE_REGISTERED(V2vHeader);

const char*
PathName(uint8_t p)
{
    switch (p)
    {
    case PATH_DIRECT:
        return "DIRECT";
    case PATH_RSU:
        return "RSU";
    case PATH_RELAY:
        return "RELAY";
    }
    return "?";
}

const char*
KindName(uint8_t k)
{
    switch (k)
    {
    case KIND_VEHICLE:
        return "vehicle";
    case KIND_RSU:
        return "rsu";
    case KIND_JAMMER:
        return "jammer";
    case KIND_INTERFERER:
        return "interferer";
    }
    return "?";
}

/* ------------------------------- Params --------------------------------- */

Params&
P()
{
    static Params params;
    return params;
}

void
ApplyScenarioDefaults(Params& p)
{
    if (p.scenario == "highway")
    {
        // Long straight multi-lane road, fast and dense traffic, open environment.
        p.roadLength = 2000;
        if (p.vehicles == 0)
            p.vehicles = 60;
        if (p.speed < 0)
            p.speed = 30; // ~108 km/h
        if (p.rsu < 0)
            p.rsu = 1;
        if (p.pathLossExp < 0)
            p.pathLossExp = 2.5;
        p.hotspot = Vector(p.roadLength / 2, 0, 0);
    }
    else if (p.scenario == "local")
    {
        // Two-lane local/rural road, slow and sparse traffic, few relays, usually no RSU.
        p.roadLength = 1000;
        if (p.vehicles == 0)
            p.vehicles = 12;
        if (p.speed < 0)
            p.speed = 12; // ~43 km/h
        if (p.rsu < 0)
            p.rsu = 0;
        if (p.pathLossExp < 0)
            p.pathLossExp = 2.7;
        p.hotspot = Vector(p.roadLength / 2, 0, 0);
    }
    else if (p.scenario == "urban")
    {
        // Two crossing streets with an RSU at the intersection, buildings -> higher path loss.
        p.roadLength = 1000;
        if (p.vehicles == 0)
            p.vehicles = 40;
        if (p.speed < 0)
            p.speed = 10; // ~36 km/h
        if (p.rsu < 0)
            p.rsu = 1;
        if (p.pathLossExp < 0)
            p.pathLossExp = 3.0;
        p.hotspot = Vector(p.roadLength / 2, p.roadLength / 2, 0);
    }
    else
    {
        NS_FATAL_ERROR("Unknown --scenario=" << p.scenario << " (use highway|local|urban)");
    }

    if (p.rsuSpacing < 0)
        p.rsuSpacing = 500;
    if (p.jamX < 0)
        p.jamX = p.hotspot.x;
    if (p.jamY < 0)
        p.jamY = p.hotspot.y + 20; // jammer sits 20 m off the road (e.g. in a parked car)
    if (p.jamStop < 0)
        p.jamStop = p.simTime;

    if (p.jammer != "none" && p.jammer != "constant" && p.jammer != "reactive")
        NS_FATAL_ERROR("Unknown --jammer=" << p.jammer);
    if (p.confounder != "none" && p.confounder != "congestion" && p.confounder != "weak" &&
        p.confounder != "interference")
        NS_FATAL_ERROR("Unknown --confounder=" << p.confounder);
    if (p.mitigation < 0 || p.mitigation > 3)
        NS_FATAL_ERROR("--mitigation must be 0..3");

    // Confounders: degraded conditions that are NOT an attack.
    if (p.confounder == "congestion")
    {
        p.beaconHz = 50;      // 5x the normal beacon rate
        p.beaconBytes = 500;  // and bigger beacons -> channel heavily loaded
    }
    else if (p.confounder == "weak")
    {
        p.txPower = 10;         // low transmit power
        p.pathLossExp += 0.4;   // and a harsher environment
    }
}

std::string
ParamsToJson(const Params& p)
{
    std::ostringstream o;
    o << "{\n"
      << "  \"scenario\": \"" << p.scenario << "\",\n"
      << "  \"mitigation\": " << p.mitigation << ",\n"
      << "  \"jammer\": \"" << p.jammer << "\",\n"
      << "  \"confounder\": \"" << p.confounder << "\",\n"
      << "  \"seed\": " << p.seed << ",\n"
      << "  \"simTime\": " << p.simTime << ",\n"
      << "  \"vehicles\": " << p.vehicles << ",\n"
      << "  \"speed\": " << p.speed << ",\n"
      << "  \"rsu\": " << p.rsu << ",\n"
      << "  \"rsuSpacing\": " << p.rsuSpacing << ",\n"
      << "  \"roadLength\": " << p.roadLength << ",\n"
      << "  \"txPower\": " << p.txPower << ",\n"
      << "  \"rsuTxPower\": " << p.rsuTxPower << ",\n"
      << "  \"pathLossExp\": " << p.pathLossExp << ",\n"
      << "  \"fading\": " << (p.fading ? 1 : 0) << ",\n"
      << "  \"jamPower\": " << p.jamPower << ",\n"
      << "  \"jamX\": " << p.jamX << ",\n"
      << "  \"jamY\": " << p.jamY << ",\n"
      << "  \"jamStart\": " << p.jamStart << ",\n"
      << "  \"jamStop\": " << p.jamStop << ",\n"
      << "  \"beaconHz\": " << p.beaconHz << ",\n"
      << "  \"beaconBytes\": " << p.beaconBytes << ",\n"
      << "  \"alertStart\": " << p.alertStart << ",\n"
      << "  \"alertInterval\": " << p.alertInterval << ",\n"
      << "  \"alertRadius\": " << p.alertRadius << ",\n"
      << "  \"deadlineMs\": " << p.deadlineMs << ",\n"
      << "  \"hotspotX\": " << p.hotspot.x << ",\n"
      << "  \"hotspotY\": " << p.hotspot.y << "\n"
      << "}\n";
    return o.str();
}

/* ------------------------------ V2vHeader ------------------------------- */

static uint32_t
FloatBits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

static float
BitsFloat(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

V2vHeader::V2vHeader()
{
}

TypeId
V2vHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::v2v::V2vHeader")
                            .SetParent<Header>()
                            .SetGroupName("V2vJam")
                            .AddConstructor<V2vHeader>();
    return tid;
}

TypeId
V2vHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
V2vHeader::Print(std::ostream& os) const
{
    os << "type=" << (type == MSG_ALERT ? "ALERT" : "BEACON") << " seq=" << seq
       << " src=" << srcId << " sender=" << senderId << " path=" << PathName(pathType)
       << " hop=" << unsigned(hopCount);
}

uint32_t
V2vHeader::GetSerializedSize() const
{
    // 5 x uint8 + 4 x uint32 + uint64 + uint32 + 4 x float + uint16
    return 5 + 16 + 8 + 4 + 16 + 2;
}

void
V2vHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(type);
    i.WriteU8(priority);
    i.WriteU8(pathType);
    i.WriteU8(hopCount);
    i.WriteU8(flags);
    i.WriteHtonU32(seq);
    i.WriteHtonU32(srcId);
    i.WriteHtonU32(senderId);
    i.WriteHtonU32(dstId);
    i.WriteHtonU64(genTimeNs);
    i.WriteHtonU32(deadlineUs);
    i.WriteHtonU32(FloatBits(posX));
    i.WriteHtonU32(FloatBits(posY));
    i.WriteHtonU32(FloatBits(velX));
    i.WriteHtonU32(FloatBits(velY));
    i.WriteHtonU16(rateDeciHz);
}

uint32_t
V2vHeader::Deserialize(Buffer::Iterator start)
{
    Buffer::Iterator i = start;
    type = i.ReadU8();
    priority = i.ReadU8();
    pathType = i.ReadU8();
    hopCount = i.ReadU8();
    flags = i.ReadU8();
    seq = i.ReadNtohU32();
    srcId = i.ReadNtohU32();
    senderId = i.ReadNtohU32();
    dstId = i.ReadNtohU32();
    genTimeNs = i.ReadNtohU64();
    deadlineUs = i.ReadNtohU32();
    posX = BitsFloat(i.ReadNtohU32());
    posY = BitsFloat(i.ReadNtohU32());
    velX = BitsFloat(i.ReadNtohU32());
    velY = BitsFloat(i.ReadNtohU32());
    rateDeciHz = i.ReadNtohU16();
    return GetSerializedSize();
}

/* ------------------------------- EventLog ------------------------------- */

std::ofstream EventLog::s_out;

void
EventLog::Open(const std::string& path)
{
    s_out.open(path, std::ios::out | std::ios::trunc);
    NS_ABORT_MSG_IF(!s_out.is_open(), "Cannot open event log " << path);
    s_out << "time_ms,event,node,seq,extra\n";
}

void
EventLog::Close()
{
    if (s_out.is_open())
        s_out.close();
}

void
EventLog::Log(const std::string& event, uint32_t node, int64_t seq, const std::string& extra)
{
    if (!s_out.is_open())
        return;
    char t[32];
    std::snprintf(t, sizeof(t), "%.3f", Simulator::Now().GetSeconds() * 1000.0); // ms, us precision
    s_out << t << ',' << event << ',' << node << ',' << seq << ',' << extra << '\n';
}

/* ---------------------------- Node registry ----------------------------- */

std::vector<NodeInfo>&
Registry()
{
    static std::vector<NodeInfo> reg;
    return reg;
}

NodeInfo&
Info(uint32_t id)
{
    NS_ASSERT_MSG(id < Registry().size(), "unknown node id " << id);
    return Registry()[id];
}

Vector
PosOf(uint32_t id)
{
    return Info(id).mob->GetPosition();
}

double
Dist2D(const Vector& a, const Vector& b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

double
DistBetween(uint32_t a, uint32_t b)
{
    return Dist2D(PosOf(a), PosOf(b));
}

std::vector<uint32_t>
VehicleIds()
{
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < Registry().size(); ++i)
        if (Registry()[i].kind == KIND_VEHICLE)
            out.push_back(i);
    return out;
}

std::vector<uint32_t>
RsuIds()
{
    std::vector<uint32_t> out;
    for (uint32_t i = 0; i < Registry().size(); ++i)
        if (Registry()[i].kind == KIND_RSU)
            out.push_back(i);
    return out;
}

} // namespace v2v
} // namespace ns3
