/*
 * scenario.cc - Road layouts and vehicle mobility (see scenario.h).
 */
#include "scenario.h"

#include <cstdio>
#include <fstream>

namespace ns3
{
namespace v2v
{

namespace
{
// Per-vehicle road description used by the wrap-around.
struct Lane
{
    int axis;   // 0 = moves along x, 1 = moves along y
    double lo;  // road start
    double hi;  // road end
};

std::vector<std::pair<Ptr<ConstantVelocityMobilityModel>, Lane>> g_lanes;
std::ofstream g_posOut;

void
WrapTick()
{
    for (auto& [mob, lane] : g_lanes)
    {
        Vector p = mob->GetPosition();
        double& c = (lane.axis == 0) ? p.x : p.y;
        double len = lane.hi - lane.lo;
        bool moved = false;
        if (c < lane.lo)
        {
            c += len;
            moved = true;
        }
        else if (c > lane.hi)
        {
            c -= len;
            moved = true;
        }
        if (moved)
            mob->SetPosition(p); // velocity is kept
    }
    Simulator::Schedule(MilliSeconds(100), &WrapTick);
}

void
PosTick()
{
    char t[32];
    std::snprintf(t, sizeof(t), "%.0f", Simulator::Now().GetSeconds() * 1000.0);
    for (uint32_t id = 0; id < Registry().size(); ++id)
    {
        Vector p = PosOf(id);
        g_posOut << t << ',' << id << ',' << KindName(Info(id).kind) << ',' << p.x << ',' << p.y
                 << '\n';
    }
    Simulator::Schedule(MilliSeconds(500), &PosTick);
}
} // namespace

void
BuildVehicleMobility(NodeContainer vehicles)
{
    const Params& p = P();
    MobilityHelper mh;
    mh.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mh.Install(vehicles);

    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    rng->SetStream(2000); // fixed stream -> same traffic for every mitigation config

    const double L = p.roadLength;
    for (uint32_t i = 0; i < vehicles.GetN(); ++i)
    {
        Ptr<ConstantVelocityMobilityModel> mob =
            vehicles.Get(i)->GetObject<ConstantVelocityMobilityModel>();
        double v = p.speed * rng->GetValue(0.85, 1.15); // +-15% speed spread
        Lane lane;
        Vector pos;
        Vector vel;

        if (p.scenario == "highway")
        {
            // 6 lanes: 3 eastbound (y = 2, 6, 10), 3 westbound (y = -2, -6, -10)
            int laneIdx = i % 6;
            bool east = laneIdx < 3;
            double y = (east ? 1 : -1) * (2 + 4 * (laneIdx % 3));
            pos = Vector(rng->GetValue(0, L), y, 0);
            vel = Vector(east ? v : -v, 0, 0);
            lane = {0, 0, L};
        }
        else if (p.scenario == "local")
        {
            // 2 lanes: eastbound y = 2, westbound y = -2
            bool east = (i % 2 == 0);
            pos = Vector(rng->GetValue(0, L), east ? 2 : -2, 0);
            vel = Vector(east ? v : -v, 0, 0);
            lane = {0, 0, L};
        }
        else // urban: horizontal street y = c +- 2, vertical street x = c +- 2
        {
            double c = L / 2;
            int k = i % 4;
            if (k < 2)
            {
                bool east = (k == 0);
                pos = Vector(rng->GetValue(0, L), c + (east ? -2 : 2), 0);
                vel = Vector(east ? v : -v, 0, 0);
                lane = {0, 0, L};
            }
            else
            {
                bool north = (k == 2);
                pos = Vector(c + (north ? 2 : -2), rng->GetValue(0, L), 0);
                vel = Vector(0, north ? v : -v, 0);
                lane = {1, 0, L};
            }
        }
        mob->SetPosition(pos);
        mob->SetVelocity(vel);
        g_lanes.push_back({mob, lane});
    }
}

std::vector<Vector>
RsuPositions()
{
    const Params& p = P();
    std::vector<Vector> out;
    if (p.rsu <= 0)
        return out;
    if (p.scenario == "urban")
    {
        // One RSU at the intersection corner.
        out.push_back(Vector(p.hotspot.x + 8, p.hotspot.y + 8, 0));
        return out;
    }
    // Along the road, 15 m off the edge, every rsuSpacing metres.
    for (double x = p.rsuSpacing / 2; x < p.roadLength; x += p.rsuSpacing)
        out.push_back(Vector(x, -15, 0));
    return out;
}

void
PlaceStatic(NodeContainer nodes, const std::vector<Vector>& positions)
{
    NS_ASSERT(nodes.GetN() == positions.size());
    MobilityHelper mh;
    Ptr<ListPositionAllocator> alloc = CreateObject<ListPositionAllocator>();
    for (const auto& v : positions)
        alloc->Add(v);
    mh.SetPositionAllocator(alloc);
    mh.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mh.Install(nodes);
}

void
StartWrapAround(NodeContainer /*vehicles*/)
{
    Simulator::Schedule(MilliSeconds(100), &WrapTick);
}

void
StartPositionLog(const std::string& path)
{
    g_posOut.open(path);
    g_posOut << "time_ms,node,kind,x,y\n";
    Simulator::Schedule(Seconds(0), &PosTick);
}

} // namespace v2v
} // namespace ns3
