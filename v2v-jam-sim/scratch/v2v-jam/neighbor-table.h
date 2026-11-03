/*
 * neighbor-table.h - Who is around me? Built only from received beacons.
 *
 * Used by the detector (expected vs received beacons per neighbour = link PDR)
 * and by the router (choose an RSU / relay vehicle).
 */
#ifndef V2V_NEIGHBOR_TABLE_H
#define V2V_NEIGHBOR_TABLE_H

#include "common.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>

namespace ns3
{
namespace v2v
{

struct Neighbor
{
    uint32_t id = 0;
    Vector pos;          // position reported in its last beacon
    Vector vel;          // velocity reported in its last beacon
    Time lastHeard;      // when its last beacon arrived
    Time firstHeard;     // when we first heard it
    double rssi = -100;  // smoothed RSSI of its beacons (dBm)
    double rateHz = 10;  // beacon rate it advertises
    bool isRsu = false;
    bool reportsJam = false; // its last beacon said "I suspect jamming"
    std::deque<Time> rxTimes; // beacon arrival times inside the detection window
};

class NeighborTable
{
  public:
    /** Record a beacon from 'h.senderId' received with 'rssiDbm'. */
    void OnBeacon(const V2vHeader& h, double rssiDbm)
    {
        Time now = Simulator::Now();
        auto it = m_n.find(h.senderId);
        if (it == m_n.end())
        {
            Neighbor n;
            n.id = h.senderId;
            n.firstHeard = now;
            n.rssi = rssiDbm;
            it = m_n.emplace(h.senderId, n).first;
        }
        Neighbor& n = it->second;
        n.pos = Vector(h.posX, h.posY, 0);
        n.vel = Vector(h.velX, h.velY, 0);
        n.lastHeard = now;
        n.rssi = 0.7 * n.rssi + 0.3 * rssiDbm; // EWMA
        n.rateHz = h.rateDeciHz / 10.0;
        n.isRsu = (h.flags & V2vHeader::FLAG_RSU) != 0;
        n.reportsJam = (h.flags & V2vHeader::FLAG_JAM) != 0;
        n.rxTimes.push_back(now);
    }

    /** Remove neighbours not heard for 'timeout' and old rx timestamps. */
    void Purge(Time timeout, Time window)
    {
        Time now = Simulator::Now();
        for (auto it = m_n.begin(); it != m_n.end();)
        {
            if (now - it->second.lastHeard > timeout)
            {
                it = m_n.erase(it);
                continue;
            }
            auto& q = it->second.rxTimes;
            while (!q.empty() && now - q.front() > window)
                q.pop_front();
            ++it;
        }
    }

    /** Dead-reckoning: where should this neighbour be now? */
    static Vector PredictPos(const Neighbor& n, Time now)
    {
        double dt = (now - n.lastHeard).GetSeconds();
        return Vector(n.pos.x + n.vel.x * dt, n.pos.y + n.vel.y * dt, 0);
    }

    /** How long (s) will a link between two moving nodes last before their distance
     *  exceeds 'range'? Returns 'cap' if they never separate (or within cap). */
    static double LinkLifetime(Vector pa, Vector va, Vector pb, Vector vb, double range, double cap)
    {
        double dx = pb.x - pa.x;
        double dy = pb.y - pa.y;
        double dvx = vb.x - va.x;
        double dvy = vb.y - va.y;
        double a = dvx * dvx + dvy * dvy;
        double b = 2 * (dx * dvx + dy * dvy);
        double c = dx * dx + dy * dy - range * range;
        if (c > 0)
            return 0; // already out of range
        if (a < 1e-9)
            return cap; // same velocity: link lasts
        double t = (-b + std::sqrt(b * b - 4 * a * c)) / (2 * a);
        return std::min(std::max(t, 0.0), cap);
    }

    std::map<uint32_t, Neighbor>& All() { return m_n; }
    const std::map<uint32_t, Neighbor>& All() const { return m_n; }

  private:
    std::map<uint32_t, Neighbor> m_n;
};

} // namespace v2v
} // namespace ns3

#endif
