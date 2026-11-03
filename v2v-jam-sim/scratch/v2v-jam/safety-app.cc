/*
 * safety-app.cc - V2V safety application, Layer 4 queue, relay / RSU forwarding.
 * See safety-app.h for the behaviour of each mitigation level.
 */
#include "safety-app.h"

#include "stats.h"

#include <algorithm>
#include <sstream>

namespace ns3
{
namespace v2v
{

NS_OBJECT_ENSURE_REGISTERED(SafetyApp);

static const double kInterruptGapS = 1.0; // a beacon gap longer than this = interruption
static const double kWarmupS = 2.0;       // ignore gaps before this time

TypeId
SafetyApp::GetTypeId()
{
    static TypeId tid = TypeId("ns3::v2v::SafetyApp")
                            .SetParent<Application>()
                            .SetGroupName("V2vJam")
                            .AddConstructor<SafetyApp>();
    return tid;
}

SafetyApp::SafetyApp()
{
}

SafetyApp::~SafetyApp()
{
}

void
SafetyApp::Setup(uint32_t id, NodeKind kind, Ptr<WifiNetDevice> dev)
{
    m_id = id;
    m_kind = kind;
    m_dev = dev;
    m_phy = dev->GetPhy();
    m_rng = CreateObject<UniformRandomVariable>();
    m_det = std::make_unique<Detector>(id, m_phy, &m_nt);
    if (kind == KIND_VEHICLE)
    {
        m_router = std::make_unique<Router>(id, &m_nt, m_det.get(), [this](V2vHeader h, uint32_t dst) {
            h.senderId = m_id;
            Submit(h, dst, P().alertBytes);
        });
    }
    dev->GetNode()->RegisterProtocolHandler(MakeCallback(&SafetyApp::Receive, this),
                                            kV2vProtocol,
                                            dev,
                                            false);
}

void
SafetyApp::StartApplication()
{
    m_det->Start();
    double period = 1.0 / CurrentBeaconHz();
    m_beaconEvent =
        Simulator::Schedule(Seconds(m_rng->GetValue(0, period)), &SafetyApp::SendBeacon, this);
}

void
SafetyApp::StopApplication()
{
    m_beaconEvent.Cancel();
    m_serviceEvent.Cancel();
}

/* ------------------------------- helpers -------------------------------- */

double
SafetyApp::CurrentBeaconHz() const
{
    const Params& p = P();
    if (p.mitigation >= 3 && m_det && m_det->JamSuspected())
        return p.beaconHzJam; // L4: leave the channel to emergency traffic
    return p.beaconHz;
}

void
SafetyApp::FillSender(V2vHeader& h, bool withPosition) const
{
    h.senderId = m_id;
    h.flags = 0;
    if (m_kind == KIND_RSU)
        h.flags |= V2vHeader::FLAG_RSU;
    // Advertise only LOCAL evidence; advertising JamSuspected() would let one node's
    // suspicion spread through the whole network via beacons.
    if (m_det && m_det->LocalJammed())
        h.flags |= V2vHeader::FLAG_JAM;
    if (withPosition)
    {
        Vector pos = GetNode()->GetObject<MobilityModel>()->GetPosition();
        Vector vel = GetNode()->GetObject<MobilityModel>()->GetVelocity();
        h.posX = pos.x;
        h.posY = pos.y;
        h.velX = vel.x;
        h.velY = vel.y;
        h.rateDeciHz = uint16_t(CurrentBeaconHz() * 10);
    }
}

/* ------------------------------- beacons -------------------------------- */

void
SafetyApp::SendBeacon()
{
    const Params& p = P();
    V2vHeader h;
    h.type = MSG_BEACON;
    h.priority = PRIO_ROUTINE;
    h.pathType = PATH_DIRECT;
    h.seq = m_beaconSeq++;
    h.srcId = m_id;
    h.dstId = kBroadcastId;
    h.genTimeNs = Simulator::Now().GetNanoSeconds();
    h.deadlineUs = uint32_t(p.deadlineMs * 1000);
    FillSender(h, true);

    if (m_kind == KIND_VEHICLE)
        Stats::BeaconGenerated(m_id);
    Submit(h, kBroadcastId, p.beaconBytes);

    double period = 1.0 / CurrentBeaconHz();
    double jitter = m_rng->GetValue(-0.05, 0.05) * period; // +-5% to avoid synchronisation
    m_beaconEvent = Simulator::Schedule(Seconds(period + jitter), &SafetyApp::SendBeacon, this);
}

/* -------------------------------- alerts -------------------------------- */

void
SafetyApp::TriggerAlert(uint32_t alertId)
{
    const Params& p = P();
    V2vHeader h;
    h.type = MSG_ALERT;
    h.priority = PRIO_EMERGENCY;
    h.pathType = PATH_DIRECT;
    h.hopCount = 0;
    h.seq = alertId;
    h.srcId = m_id;
    h.dstId = kBroadcastId;
    h.genTimeNs = Simulator::Now().GetNanoSeconds();
    h.deadlineUs = uint32_t(p.deadlineMs * 1000);
    FillSender(h, true); // alert carries the SOURCE position (kept by relays)

    m_seenAlerts.insert(alertId);
    bool jam = m_det->JamSuspected();
    std::ostringstream x;
    x << "x=" << h.posX << ";y=" << h.posY << ";jam_suspected=" << (jam ? 1 : 0)
      << ";det_state=" << DetStateName(m_det->GetState());
    EventLog::Log("ALERT_GEN", m_id, alertId, x.str());

    Submit(h, kBroadcastId, p.alertBytes);

    if (p.mitigation >= 1 && jam)
        Simulator::Schedule(MilliSeconds(p.repeatMs), &SafetyApp::RepeatAlert, this, h, 1u);
    if (p.mitigation >= 2 && jam && m_router)
        m_router->StartRecovery(h);
}

void
SafetyApp::RepeatAlert(V2vHeader h, uint32_t k)
{
    const Params& p = P();
    if (h.Expired(Simulator::Now()))
        return;
    std::ostringstream x;
    x << "rep=" << k;
    EventLog::Log("REPEAT", m_id, h.seq, x.str());
    Submit(h, kBroadcastId, p.alertBytes);
    Time next = Simulator::Now() + MilliSeconds(p.repeatMs);
    if (next - h.GenTime() < h.Deadline())
        Simulator::Schedule(MilliSeconds(p.repeatMs), &SafetyApp::RepeatAlert, this, h, k + 1);
}

/* ------------------------------- Layer 4 -------------------------------- */

void
SafetyApp::Submit(const V2vHeader& h, uint32_t dstId, uint32_t bytes)
{
    if (P().mitigation < 3)
    {
        Transmit(h, dstId, bytes); // straight into the MAC FIFO
        return;
    }
    if (h.Expired(Simulator::Now()))
    {
        DropExpired(h, "tx");
        return;
    }
    m_queue.push_back({h, dstId, bytes, m_order++});
    if (m_serviceEvent.IsExpired()) // IsExpired() exists in both ns-3.40 and ns-3.47
        ServiceQueue();
}

void
SafetyApp::ServiceQueue()
{
    Time now = Simulator::Now();
    // 1) never send stale data
    for (auto it = m_queue.begin(); it != m_queue.end();)
    {
        if (it->h.Expired(now))
        {
            DropExpired(it->h, "queue");
            it = m_queue.erase(it);
        }
        else
            ++it;
    }
    if (m_queue.empty())
        return;
    // 2) hand one message to the MAC only when the radio is idle, EMERGENCY first
    if (m_phy->IsStateIdle())
    {
        auto best = std::min_element(m_queue.begin(), m_queue.end(), [](const QItem& a, const QItem& b) {
            if (a.h.priority != b.h.priority)
                return a.h.priority > b.h.priority; // higher priority first
            return a.order < b.order;               // then oldest first
        });
        QItem item = *best;
        m_queue.erase(best);
        Transmit(item.h, item.dst, item.bytes);
    }
    if (!m_queue.empty())
        m_serviceEvent =
            Simulator::Schedule(MicroSeconds(P().serviceMs * 1000), &SafetyApp::ServiceQueue, this);
}

void
SafetyApp::DropExpired(const V2vHeader& h, const char* where)
{
    if (h.type == MSG_ALERT)
    {
        std::ostringstream x;
        x << "where=" << where << ";age_ms=" << (Simulator::Now() - h.GenTime()).GetSeconds() * 1000;
        EventLog::Log("DROP_DEADLINE", m_id, h.seq, x.str());
    }
    else
    {
        Stats::BeaconDroppedDeadline();
    }
}

void
SafetyApp::Transmit(const V2vHeader& h, uint32_t dstId, uint32_t bytes)
{
    uint32_t hdr = h.GetSerializedSize();
    Ptr<Packet> pkt = Create<Packet>(bytes > hdr ? bytes - hdr : 0);
    pkt->AddHeader(h);
    Address to = (dstId == kBroadcastId) ? m_dev->GetBroadcast() : Info(dstId).dev->GetAddress();
    m_dev->Send(pkt, to, kV2vProtocol);

    if (h.type == MSG_ALERT)
    {
        std::ostringstream x;
        x << "path=" << PathName(h.pathType) << ";hop=" << unsigned(h.hopCount)
          << ";dst=" << (dstId == kBroadcastId ? std::string("bcast") : std::to_string(dstId))
          << ";bytes=" << pkt->GetSize();
        EventLog::Log("TX", m_id, h.seq, x.str());
    }
    else
    {
        m_beaconsTx++;
    }
}

/* ------------------------------- receive -------------------------------- */

void
SafetyApp::Receive(Ptr<NetDevice>,
                   Ptr<const Packet> pkt,
                   uint16_t,
                   const Address&,
                   const Address&,
                   NetDevice::PacketType)
{
    Ptr<Packet> copy = pkt->Copy();
    V2vHeader h;
    copy->RemoveHeader(h);
    Time now = Simulator::Now();

    if (h.type == MSG_BEACON)
    {
        if (P().mitigation >= 3 && h.Expired(now))
        {
            Stats::BeaconDroppedDeadline(); // L4: stale status is not trusted
            return;
        }
        m_nt.OnBeacon(h, m_det->LastRssi());
        m_beaconsRx++;
        if (m_lastBeaconRx > Seconds(0) && now.GetSeconds() > kWarmupS)
        {
            double gap = (now - m_lastBeaconRx).GetSeconds();
            m_maxGap = std::max(m_maxGap, gap);
            if (gap > kInterruptGapS)
                m_interrupt += gap;
        }
        m_lastBeaconRx = now;
        Stats::BeaconReceived(h.srcId, m_id, (now - h.GenTime()).GetSeconds(), pkt->GetSize());
        return;
    }
    HandleAlert(h);
}

void
SafetyApp::HandleAlert(const V2vHeader& h)
{
    const Params& p = P();
    Time now = Simulator::Now();

    // Our own alert coming back from a relay/RSU = implicit ACK for the router.
    if (h.srcId == m_id)
    {
        if (m_router)
            m_router->OnOverheard(h);
        return;
    }

    // Record the first copy we get (only vehicles are alert targets).
    if (m_kind == KIND_VEHICLE && !m_seenAlerts.count(h.seq))
    {
        m_seenAlerts.insert(h.seq);
        double latMs = (now - h.GenTime()).GetSeconds() * 1000;
        bool stale = h.Expired(now);
        if (p.mitigation >= 3 && stale)
        {
            DropExpired(h, "rx");
        }
        else
        {
            std::ostringstream x;
            x.precision(5);
            x << "lat_ms=" << latMs << ";path=" << PathName(h.pathType)
              << ";hop=" << unsigned(h.hopCount) << ";from=" << h.senderId
              << ";stale=" << (stale ? 1 : 0);
            EventLog::Log("RX", m_id, h.seq, x.str());
        }
    }

    // Were we asked to forward it?
    if (h.dstId == m_id)
    {
        if (m_kind == KIND_RSU)
        {
            if (RsuRebroadcast(h))
                BackhaulForward(h);
        }
        else
        {
            RelayRebroadcast(h);
        }
    }
}

void
SafetyApp::RelayRebroadcast(const V2vHeader& h)
{
    if (m_relayed.count(h.seq))
        return;
    m_relayed.insert(h.seq);
    if (P().mitigation >= 3 && h.Expired(Simulator::Now()))
    {
        DropExpired(h, "relay");
        return;
    }
    V2vHeader f = h;
    f.hopCount = h.hopCount + 1;
    f.pathType = PATH_RELAY;
    f.dstId = kBroadcastId;
    FillSender(f, false);
    std::ostringstream x;
    x << "src=" << h.srcId;
    EventLog::Log("RELAY_FWD", m_id, h.seq, x.str());
    Submit(f, kBroadcastId, P().alertBytes);
}

bool
SafetyApp::RsuRebroadcast(V2vHeader h)
{
    if (m_relayed.count(h.seq))
        return false;
    m_relayed.insert(h.seq);
    if (P().mitigation >= 3 && h.Expired(Simulator::Now()))
    {
        DropExpired(h, "rsu");
        return false;
    }
    V2vHeader f = h;
    f.hopCount = h.hopCount + 1;
    f.pathType = PATH_RSU;
    f.dstId = kBroadcastId;
    FillSender(f, false);
    std::ostringstream x;
    x << "src=" << h.srcId;
    EventLog::Log("RSU_FWD", m_id, h.seq, x.str());
    Submit(f, kBroadcastId, P().alertBytes);
    return true;
}

void
SafetyApp::BackhaulForward(const V2vHeader& h)
{
    // Wired RSU-to-RSU backhaul (modelled as a fixed latency): every other RSU that
    // covers the alert area re-broadcasts the alert too.
    const Params& p = P();
    Vector origin(h.posX, h.posY, 0);
    for (uint32_t r : RsuIds())
    {
        if (r == m_id || Dist2D(PosOf(r), origin) > p.alertRadius + 300)
            continue;
        std::ostringstream x;
        x << "to=" << r;
        EventLog::Log("BACKHAUL", m_id, h.seq, x.str());
        Simulator::Schedule(MilliSeconds(p.backhaulMs), &SafetyApp::RsuRebroadcast, Info(r).app, h);
    }
}

/* ------------------------------- summary -------------------------------- */

std::string
SafetyApp::SummaryCsv() const
{
    double interrupt = m_interrupt;
    double maxGap = m_maxGap;
    // account for a gap still open at the end of the run
    double tail = (Simulator::Now() - m_lastBeaconRx).GetSeconds();
    if (m_lastBeaconRx > Seconds(0))
    {
        maxGap = std::max(maxGap, tail);
        if (tail > kInterruptGapS)
            interrupt += tail;
    }
    std::ostringstream o;
    o << m_id << ',' << KindName(m_kind) << ',' << m_beaconsTx << ',' << m_beaconsRx << ','
      << maxGap * 1000 << ',' << interrupt * 1000; // ms
    return o.str();
}

} // namespace v2v
} // namespace ns3
