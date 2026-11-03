/*
 * scenario.h - Road layouts and vehicle mobility.
 *
 *   highway : 2 km straight road, 3 lanes per direction, fast + dense
 *   local   : 1 km two-lane road, slow + sparse, usually no RSU
 *   urban   : two 1 km streets crossing at an intersection, RSU at the junction
 *
 * Vehicles move with ConstantVelocity and "wrap around" at the end of the road
 * (like a ring road), so the vehicle density stays constant for the whole run.
 */
#ifndef V2V_SCENARIO_H
#define V2V_SCENARIO_H

#include "common.h"

namespace ns3
{
namespace v2v
{

/** Place vehicles and give them speeds/directions. Uses its own random stream
 *  so the traffic is identical for every mitigation setting with the same seed. */
void BuildVehicleMobility(NodeContainer vehicles);

/** Compute RSU positions for the current scenario (empty if --rsu=0). */
std::vector<Vector> RsuPositions();

/** Put nodes at fixed positions (RSUs, jammer, interferer). */
void PlaceStatic(NodeContainer nodes, const std::vector<Vector>& positions);

/** Start the periodic wrap-around check for vehicles. */
void StartWrapAround(NodeContainer vehicles);

/** Optional: write positions.csv every 0.5 s (used by analysis/animate.py). */
void StartPositionLog(const std::string& path);

} // namespace v2v
} // namespace ns3

#endif
