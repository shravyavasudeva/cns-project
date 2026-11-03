/*
 * relay.cc - Layer 3 alternate-path recovery (see relay.h).
 */
#include "relay.h"

#include <algorithm>
#include <sstream>

namespace ns3
{
namespace v2v
{

Router::Router(uint32_t self, NeighborTable* nt, Detector* det, SendFn send)
    : m_self(self),
      m_nt(nt),
      m_det(det),
      m_send(std::move(send))
{
}

void
Router::StartRecovery(const V2vHeader& alert)
{
    Pending pd;
    pd.alert = alert;
    pd.start = Simulator::Now();
    m_pending[alert.seq] = pd;
    TryPath(alert.seq);
}

bool
Router::Choose(Pending& pd, uint32_t& via, bool& isRsu, double& score)
{
    const Params& p = P();
    Time now = Simulator::Now();
    Vector me = PosOf(m_self);
    Vector myVel = Info(m_self).mob->GetVelocity();

    Vector jc;
    bool haveCenter = m_det->JamCenter(jc);

    // 1) RSU first: strongest RSU heard recently.
    double bestRssi = -1e9;
    uint32_t bestRsu = kBroadcastId;
    for (const auto& [id, n] : m_nt->All())
    {
        if (!n.isRsu || pd.tried.count(id))
            continue;
        if (now - n.lastHeard > Seconds(1) || n.rssi < p.relayMinRssi)
            continue;
        if (n.rssi > bestRssi)
        {
            bestRssi = n.rssi;
            bestRsu = id;
        }
    }
    if (bestRsu != kBroadcastId)
    {
        via = bestRsu;
        isRsu = true;
        score = 1.0;
        return true;
    }

    // 2) Best relay vehicle.
    double best = -1;
    uint32_t bestId = kBroadcastId;
    for (const auto& [id, n] : m_nt->All())
    {
        if (n.isRsu || pd.tried.count(id))
            continue;
        if (now - n.lastHeard > MilliSeconds(500) || n.rssi < p.relayMinRssi)
            continue;
        if (n.reportsJam || m_det->IsLinkJammed(id))
            continue; // it is itself inside the jammed area
        Vector q = NeighborTable::PredictPos(n, now);

        double quality = std::clamp((n.rssi - p.relayMinRssi) / 25.0, 0.0, 1.0);
        double away = haveCenter ? std::clamp(Dist2D(q, jc) / 300.0, 0.0, 1.0) : 0.5;
        double life = NeighborTable::LinkLifetime(me, myVel, q, n.vel, p.linkRange, 5.0) / 5.0;
        double s = 0.4 * quality + 0.3 * away + 0.3 * life;
        if (s > best)
        {
            best = s;
            bestId = id;
        }
    }
    if (bestId != kBroadcastId)
    {
        via = bestId;
        isRsu = false;
        score = best;
        return true;
    }
    return false;
}

void
Router::TryPath(uint32_t alertId)
{
    auto it = m_pending.find(alertId);
    if (it == m_pending.end() || it->second.done)
        return;
    Pending& pd = it->second;
    const Params& p = P();
    Time now = Simulator::Now();

    if (pd.alert.Expired(now))
    {
        pd.done = true;
        EventLog::Log("RECOVERY_TIMEOUT", m_self, alertId, "reason=deadline");
        return;
    }

    uint32_t via;
    bool isRsu;
    double score;
    if (!Choose(pd, via, isRsu, score))
    {
        pd.done = true;
        std::ostringstream x;
        x << "neighbors=" << m_nt->All().size() << ";tried=" << pd.tried.size();
        EventLog::Log("NO_RELAY", m_self, alertId, x.str());
        return;
    }

    std::ostringstream x;
    x.precision(3);
    if (pd.via != kBroadcastId)
    {
        pd.switches++;
        std::ostringstream s;
        s << "from=" << pd.via << ";to=" << via;
        EventLog::Log("PATH_SWITCH", m_self, alertId, s.str());
    }
    pd.via = via;
    pd.tried.insert(via);
    x << "path=" << (isRsu ? "RSU" : "RELAY") << ";via=" << via << ";score=" << score
      << ";dist=" << DistBetween(m_self, via);
    EventLog::Log("REROUTE", m_self, alertId, x.str());

    V2vHeader h = pd.alert;
    h.pathType = isRsu ? PATH_RSU : PATH_RELAY;
    h.dstId = via;
    m_send(h, via);

    pd.timer = Simulator::Schedule(MilliSeconds(p.ackTimeoutMs), &Router::OnAckTimeout, this,
                                   alertId);
}

void
Router::OnAckTimeout(uint32_t alertId)
{
    auto it = m_pending.find(alertId);
    if (it == m_pending.end() || it->second.done)
        return;
    Pending& pd = it->second;
    std::ostringstream x;
    x << "via=" << pd.via;
    EventLog::Log("ROUTE_BREAK", m_self, alertId, x.str());
    if (pd.switches >= P().maxSwitches)
    {
        pd.done = true;
        EventLog::Log("RECOVERY_TIMEOUT", m_self, alertId, "reason=max_switches");
        return;
    }
    TryPath(alertId);
}

void
Router::OnOverheard(const V2vHeader& h)
{
    if (h.type != MSG_ALERT || h.srcId != m_self || h.hopCount == 0)
        return;
    auto it = m_pending.find(h.seq);
    if (it == m_pending.end() || it->second.done)
        return;
    Pending& pd = it->second;
    pd.done = true;
    pd.timer.Cancel();
    std::ostringstream x;
    x.precision(4);
    x << "via=" << h.senderId << ";path=" << PathName(h.pathType)
      << ";rec_ms=" << (Simulator::Now() - pd.start).GetSeconds() * 1000.0
      << ";switches=" << pd.switches;
    EventLog::Log("RELAY_OK", m_self, h.seq, x.str());
}

} // namespace v2v
} // namespace ns3
