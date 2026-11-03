/*
 * relay.h - Layer 3 alternate-path recovery for emergency alerts.
 *
 * When the source of an alert suspects jamming (Detector::JamSuspected), it does not
 * rely only on its own direct broadcast. It also hands the alert to:
 *   1. an RSU in range (best RSSI)           -> RSU re-broadcasts at high power and
 *                                               forwards over the wired backhaul to the
 *                                               other RSUs near the alert area
 *   2. otherwise the best relay vehicle       -> relay re-broadcasts once
 *   3. otherwise                              -> NO_RELAY (expected on sparse roads)
 *
 * Relay score (all terms normalised to 0..1):
 *   0.4 * link quality (RSSI)  +  0.3 * distance from the estimated jammed area
 *   + 0.3 * predicted link lifetime (relative speed; matters on the highway)
 *
 * Implicit acknowledgement: the source listens for the relay's re-broadcast. If it is not
 * overheard within ackTimeoutMs, the route is considered broken (ROUTE_BREAK) and the next
 * best candidate is tried (PATH_SWITCH), up to maxSwitches times or until the deadline.
 */
#ifndef V2V_RELAY_H
#define V2V_RELAY_H

#include "common.h"
#include "detector.h"
#include "neighbor-table.h"

#include <functional>
#include <map>
#include <set>

namespace ns3
{
namespace v2v
{

class Router
{
  public:
    using SendFn = std::function<void(V2vHeader h, uint32_t dstId)>;

    Router(uint32_t self, NeighborTable* nt, Detector* det, SendFn send);

    /** Source side: start alternate-path delivery for this alert. */
    void StartRecovery(const V2vHeader& alert);
    /** Source side: we overheard a copy of an alert (implicit ACK check). */
    void OnOverheard(const V2vHeader& h);

  private:
    struct Pending
    {
        V2vHeader alert;
        uint32_t via = kBroadcastId;
        Time start;
        std::set<uint32_t> tried;
        uint32_t switches = 0;
        bool done = false;
        EventId timer;
    };

    bool Choose(Pending& pd, uint32_t& via, bool& isRsu, double& score);
    void TryPath(uint32_t alertId);
    void OnAckTimeout(uint32_t alertId);

    uint32_t m_self;
    NeighborTable* m_nt;
    Detector* m_det;
    SendFn m_send;
    std::map<uint32_t, Pending> m_pending; // by alert id
};

} // namespace v2v
} // namespace ns3

#endif
