/*
 * detector.cc - Layer 1/2 jamming detector (see detector.h for the rules).
 */
#include "detector.h"

#include <cmath>
#include <sstream>

namespace ns3
{
namespace v2v
{

static const Time kNeighborTimeout = Seconds(5);

const char*
DetStateName(uint8_t s)
{
    switch (s)
    {
    case DET_NORMAL:
        return "NORMAL";
    case DET_CONGESTED:
        return "CONGESTED";
    case DET_WEAK:
        return "WEAK";
    case DET_JAMMED:
        return "JAMMED";
    }
    return "?";
}

Detector::Detector(uint32_t self, Ptr<WifiPhy> phy, NeighborTable* nt)
    : m_self(self),
      m_phy(phy),
      m_nt(nt)
{
    const Params& p = P();
    size_t nBins = std::max<size_t>(1, size_t(std::lround(p.detWindow / p.detPeriod)));
    m_bins.resize(nBins);
}

void
Detector::Start()
{
    m_phy->GetState()->TraceConnectWithoutContext("RxOk", MakeCallback(&Detector::OnRxOk, this));
    m_phy->GetState()->TraceConnectWithoutContext("RxError",
                                                  MakeCallback(&Detector::OnRxError, this));
    m_phy->TraceConnectWithoutContext("MonitorSnifferRx", MakeCallback(&Detector::OnSniff, this));

    const Params& p = P();
    // Random start offset so all nodes do not sample in lock-step.
    Ptr<UniformRandomVariable> u = CreateObject<UniformRandomVariable>();
    Simulator::Schedule(MilliSeconds(u->GetValue(0, p.busySampleMs)), &Detector::SampleChannel,
                        this);
    Simulator::Schedule(Seconds(p.detPeriod + u->GetValue(0, p.detPeriod)), &Detector::Evaluate,
                        this);
}

/* ------------------------------ inputs ---------------------------------- */

void
Detector::SampleChannel()
{
    Bin& b = m_bins[m_cur];
    if (!m_phy->IsStateTx()) // our own transmissions do not count
    {
        b.total++;
        if (m_phy->IsStateCcaBusy() || m_phy->IsStateRx())
            b.busy++;
        if (m_phy->IsStateCcaBusy())
            b.cca++;
    }
    Simulator::Schedule(MicroSeconds(P().busySampleMs * 1000), &Detector::SampleChannel, this);
}

void
Detector::OnRxOk(Ptr<const Packet>, double, WifiMode, WifiPreamble)
{
    m_bins[m_cur].ok++;
}

void
Detector::OnRxError(Ptr<const Packet>, double)
{
    m_bins[m_cur].err++;
}

void
Detector::OnSniff(Ptr<const Packet>, uint16_t, WifiTxVector, MpduInfo, SignalNoiseDbm sn, uint16_t)
{
    Bin& b = m_bins[m_cur];
    b.sig++;
    b.rssiSum += sn.signal;
    b.noiseSum += sn.noise; // thermal noise + interference (incl. jamming)
    m_lastRssi = sn.signal;
}

/* ---------------------------- measurement ------------------------------- */

DetMeasures
Detector::Measure()
{
    const Params& p = P();
    Time now = Simulator::Now();
    DetMeasures m;

    Bin sum;
    for (const Bin& b : m_bins)
    {
        sum.busy += b.busy;
        sum.cca += b.cca;
        sum.total += b.total;
        sum.ok += b.ok;
        sum.err += b.err;
        sum.sig += b.sig;
        sum.rssiSum += b.rssiSum;
        sum.noiseSum += b.noiseSum;
    }
    double window = p.detPeriod * m_bins.size();
    m.busy = sum.total ? double(sum.busy) / sum.total : 0;
    m.ccaBusy = sum.total ? double(sum.cca) / sum.total : 0;
    m.okRate = sum.ok / window;
    m.errRatio = (sum.ok + sum.err) ? double(sum.err) / (sum.ok + sum.err) : 0;
    if (sum.sig > 0)
    {
        m.hasSignal = true;
        m.rssiDbm = sum.rssiSum / sum.sig;
        m.noiseDbm = sum.noiseSum / sum.sig;
    }

    // Neighbour-based PDR and per-link jamming.
    m_nt->Purge(kNeighborTimeout, Seconds(window));
    Vector me = PosOf(m_self);
    double expSum = 0;
    double rxSum = 0;
    m_linkJam.clear();
    for (auto& [id, n] : m_nt->All())
    {
        Vector pred = NeighborTable::PredictPos(n, now);
        double d = Dist2D(me, pred);
        if (d > p.linkRange)
            continue; // we would not expect to hear it anyway
        bool offRoad = pred.x < 0 || pred.x > p.roadLength ||
                       (p.scenario == "urban" && (pred.y < 0 || pred.y > p.roadLength));
        if (offRoad)
            continue; // it wrapped around to the other end of the road, not silenced
        double heardFor = std::min(window, (now - n.firstHeard).GetSeconds());
        double expected = n.rateHz * heardFor;
        if (expected < 1)
            continue;
        double rx = std::min<double>(n.rxTimes.size(), expected);
        expSum += expected;
        rxSum += rx;
        if (expected >= 2 && d < 0.75 * p.linkRange && rx / expected < 0.3)
            m_linkJam.insert(id);
    }
    m.pdr = (expSum >= 3) ? rxSum / expSum : -1;
    m.linkJam = m_linkJam.size();
    return m;
}

DetState
Detector::Classify(const DetMeasures& m) const
{
    const Params& p = P();
    bool energyFlood = m.busy >= p.edBusyThr && m.okRate < 5;
    bool pdrLow = m.pdr >= 0 && m.pdr < p.pdrThr;
    bool noiseHigh = m.hasSignal && m.noiseDbm > p.noiseThr;
    bool quiet = m.busy < p.congBusyThr;
    bool errHigh = m.errRatio > p.errThr;

    // Energy present but nothing decodes: a jammer below the CCA energy threshold still
    // drowns every frame (a merely weak/isolated channel would be idle instead).
    bool energyNoDecode = m.busy >= 0.1 && m.okRate < 2 && (m.pdr < 0 || pdrLow);
    // Congestion keeps the radio busy RECEIVING frames (RX state); a jammer keeps it busy
    // with energy that is not a decodable frame (CCA_BUSY) and raises the noise floor.
    bool ccaJam = m.ccaBusy >= p.ccaJamThr && (noiseHigh || !m.hasSignal);
    bool jammed = energyFlood || ccaJam || energyNoDecode || (pdrLow && noiseHigh) ||
                  (pdrLow && errHigh && quiet) || (m.pdr < 0 && quiet && m.errRatio > 2 * p.errThr);
    if (jammed)
        return DET_JAMMED;
    if (m.busy >= p.congBusyThr)
        return DET_CONGESTED;
    if (pdrLow)
        return DET_WEAK;
    return DET_NORMAL;
}

void
Detector::Evaluate()
{
    const Params& p = P();
    DetMeasures m = Measure();
    m_last = m;
    DetState raw = Classify(m);

    DetState next = m_state;
    if (raw == DET_JAMMED)
    {
        m_badStreak++;
        m_goodStreak = 0;
        if (m_badStreak >= p.enterCount)
            next = DET_JAMMED;
    }
    else
    {
        m_goodStreak++;
        m_badStreak = 0;
        if (m_state != DET_JAMMED || m_goodStreak >= p.exitCount)
            next = raw;
    }

    if (next != m_state)
    {
        std::ostringstream x;
        x.precision(3);
        x << "from=" << DetStateName(m_state) << ";to=" << DetStateName(next) << ";busy=" << m.busy
          << ";cca=" << m.ccaBusy << ";pdr=" << m.pdr << ";err=" << m.errRatio << ";noise=" << m.noiseDbm
          << ";rssi=" << m.rssiDbm << ";linkjam=" << m.linkJam;
        EventLog::Log("DET_STATE", m_self, -1, x.str());
        if (next == DET_JAMMED)
            EventLog::Log("JAM_DETECTED", m_self, -1, x.str());
        m_state = next;
    }

    // Rotate the window: the oldest bin becomes the new current bin.
    m_cur = (m_cur + 1) % m_bins.size();
    m_bins[m_cur] = Bin();
    Simulator::Schedule(Seconds(p.detPeriod), &Detector::Evaluate, this);
}

/* ------------------------------ queries --------------------------------- */

bool
Detector::JamSuspected() const
{
    if (LocalJammed())
        return true;
    if (m_linkJam.size() >= P().minLinkJam)
        return true;
    Time now = Simulator::Now();
    for (const auto& [id, n] : m_nt->All())
        if (n.reportsJam && now - n.lastHeard < Seconds(1))
            return true;
    return false;
}

bool
Detector::JamCenter(Vector& out) const
{
    Time now = Simulator::Now();
    double sx = 0, sy = 0;
    int k = 0;
    if (LocalJammed())
    {
        Vector me = PosOf(m_self);
        sx += me.x;
        sy += me.y;
        k++;
    }
    for (const auto& [id, n] : m_nt->All())
    {
        bool evidence = m_linkJam.count(id) || (n.reportsJam && now - n.lastHeard < Seconds(1));
        if (!evidence)
            continue;
        Vector q = NeighborTable::PredictPos(n, now);
        sx += q.x;
        sy += q.y;
        k++;
    }
    if (k == 0)
        return false;
    out = Vector(sx / k, sy / k, 0);
    return true;
}

/* ------------------------------ accuracy -------------------------------- */

void
Detector::ScoreSample(bool gtJammed)
{
    double now = Simulator::Now().GetSeconds();
    bool det = LocalJammed();
    m_stateSamples[m_state]++;
    if (gtJammed)
    {
        if (m_firstGt < 0)
            m_firstGt = now;
        if (det)
        {
            m_tp++;
            if (m_firstDetect < 0)
                m_firstDetect = now;
        }
        else
            m_fn++;
    }
    else
    {
        if (det)
            m_fp++;
        else
            m_tn++;
    }
}

std::string
Detector::SummaryCsv() const
{
    std::ostringstream o;
    auto ms = [](double s) { return s < 0 ? -1.0 : s * 1000.0; }; // -1 = never
    o << m_tp << ',' << m_fn << ',' << m_fp << ',' << m_tn << ',' << ms(m_firstGt) << ','
      << ms(m_firstDetect) << ',' << m_stateSamples[0] << ',' << m_stateSamples[1] << ','
      << m_stateSamples[2] << ',' << m_stateSamples[3];
    return o.str();
}

} // namespace v2v
} // namespace ns3
