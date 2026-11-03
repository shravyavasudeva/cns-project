/*
 * safety-app.h - The V2V safety application running on every vehicle and RSU.
 *
 * What it does:
 *   - Beacons : ROUTINE status messages (position, velocity) at beaconHz (default 10 Hz)
 *   - Alerts  : EMERGENCY messages created by TriggerAlert() with a safety deadline
 *   - Receive : updates the neighbour table, logs alert receptions, acts as relay / RSU
 *
 * Mitigation levels (--mitigation), each one adds to the previous:
 *   0  no mitigation       : alert is broadcast once, messages go straight to the MAC
 *   1  detection           : detector runs; if jamming is suspected the alert is
 *                            re-broadcast every repeatMs until its deadline
 *   2  + Layer 3 rerouting : additionally hand the alert to an RSU / relay vehicle (relay.h)
 *   3  + Layer 4 handling  : priority queue (EMERGENCY first), messages past their deadline
 *                            are dropped at sender, relay and receiver (never shown as current),
 *                            beacon rate is reduced while jamming is suspected
 * (The detector always runs and logs, so detection accuracy can be measured at every level.)
 */
#ifndef V2V_SAFETY_APP_H
#define V2V_SAFETY_APP_H

#include "common.h"
#include "detector.h"
#include "neighbor-table.h"
#include "relay.h"

#include "ns3/wifi-module.h"

#include <memory>
#include <set>

namespace ns3
{
namespace v2v
{

class SafetyApp : public Application
{
  public:
    static TypeId GetTypeId();
    SafetyApp();
    ~SafetyApp() override;

    void Setup(uint32_t id, NodeKind kind, Ptr<WifiNetDevice> dev);
    /** Create an emergency alert at this vehicle (called by the alert scheduler). */
    void TriggerAlert(uint32_t alertId);
    /** RSU only: re-broadcast an alert (direct request or via the backhaul). */
    bool RsuRebroadcast(V2vHeader h);

    Detector* GetDetector() { return m_det.get(); }
    /** node,kind,beacons_tx,beacons_rx,max_gap_ms,interrupt_ms */
    std::string SummaryCsv() const;

  private:
    void StartApplication() override;
    void StopApplication() override;

    void SendBeacon();
    double CurrentBeaconHz() const;
    void FillSender(V2vHeader& h, bool withPosition) const;

    // Layer 4 entry point for every outgoing message.
    void Submit(const V2vHeader& h, uint32_t dstId, uint32_t bytes);
    void ServiceQueue();
    void Transmit(const V2vHeader& h, uint32_t dstId, uint32_t bytes);
    void DropExpired(const V2vHeader& h, const char* where);

    void Receive(Ptr<NetDevice> dev,
                 Ptr<const Packet> pkt,
                 uint16_t proto,
                 const Address& from,
                 const Address& to,
                 NetDevice::PacketType type);
    void HandleAlert(const V2vHeader& h);
    void RelayRebroadcast(const V2vHeader& h);
    void BackhaulForward(const V2vHeader& h);
    void RepeatAlert(V2vHeader h, uint32_t k);

    uint32_t m_id = 0;
    NodeKind m_kind = KIND_VEHICLE;
    Ptr<WifiNetDevice> m_dev;
    Ptr<WifiPhy> m_phy;
    NeighborTable m_nt;
    std::unique_ptr<Detector> m_det;
    std::unique_ptr<Router> m_router;
    Ptr<UniformRandomVariable> m_rng;

    uint32_t m_beaconSeq = 0;
    EventId m_beaconEvent;

    // Layer 4 queue
    struct QItem
    {
        V2vHeader h;
        uint32_t dst;
        uint32_t bytes;
        uint64_t order;
    };
    std::vector<QItem> m_queue;
    uint64_t m_order = 0;
    EventId m_serviceEvent;

    std::set<uint32_t> m_seenAlerts; // alerts already received (log first copy only)
    std::set<uint32_t> m_relayed;    // alerts already re-broadcast by this node

    // connection statistics
    uint64_t m_beaconsTx = 0;
    uint64_t m_beaconsRx = 0;
    Time m_lastBeaconRx;
    double m_maxGap = 0;
    double m_interrupt = 0;
};

} // namespace v2v
} // namespace ns3

#endif
