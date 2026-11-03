/*
 * jammer.cc - RF jammers and the non-malicious interferer (see jammer.h).
 */
#include "jammer.h"

#include <cmath>
#include <sstream>

namespace ns3
{
namespace v2v
{

Ptr<WaveformGenerator>
CreateNoiseSource(Ptr<Node> node,
                  Ptr<SpectrumChannel> channel,
                  Ptr<WifiPhy> refPhy,
                  double powerDbm,
                  Time period,
                  double dutyCycle)
{
    // Spectrum: one band covering the WiFi channel (e.g. 5890 MHz +- 5 MHz).
    double fc = refPhy->GetFrequency() * 1e6;    // Hz
    double bw = refPhy->GetChannelWidth() * 1e6; // Hz
    BandInfo band;
    band.fl = fc - bw / 2;
    band.fc = fc;
    band.fh = fc + bw / 2;
    Bands bands;
    bands.push_back(band);
    Ptr<SpectrumModel> model = Create<SpectrumModel>(bands);

    // Power spectral density in W/Hz so that the total power equals powerDbm.
    double powerW = std::pow(10.0, (powerDbm - 30.0) / 10.0);
    Ptr<SpectrumValue> psd = Create<SpectrumValue>(model);
    (*psd) = powerW / bw;

    WaveformGeneratorHelper helper;
    helper.SetChannel(channel);
    helper.SetTxPowerSpectralDensity(psd);
    helper.SetPhyAttribute("Period", TimeValue(period));
    helper.SetPhyAttribute("DutyCycle", DoubleValue(dutyCycle));
    NetDeviceContainer devs = helper.Install(node);

    Ptr<NonCommunicatingNetDevice> dev = DynamicCast<NonCommunicatingNetDevice>(devs.Get(0));
    return DynamicCast<WaveformGenerator>(dev->GetPhy());
}

/* --------------------------- ConstantJammer ----------------------------- */

void
ConstantJammer::Install(Ptr<Node> node, Ptr<SpectrumChannel> ch, Ptr<WifiPhy> refPhy)
{
    const Params& p = P();
    // Back-to-back 5 ms noise segments (duty cycle 1) = continuous noise.
    m_wg = CreateNoiseSource(node, ch, refPhy, p.jamPower, MilliSeconds(5), 1.0);
    Simulator::Schedule(Seconds(p.jamStart), &ConstantJammer::On, this);
    Simulator::Schedule(Seconds(p.jamStop), &ConstantJammer::Off, this);
}

void
ConstantJammer::On()
{
    m_active = true;
    m_wg->Start();
    std::ostringstream x;
    x << "type=constant;power_dbm=" << P().jamPower << ";x=" << P().jamX << ";y=" << P().jamY;
    EventLog::Log("JAM_ON", 0, -1, x.str());
}

void
ConstantJammer::Off()
{
    m_active = false;
    m_wg->Stop();
    EventLog::Log("JAM_OFF", 0, -1, "type=constant");
}

bool
ConstantJammer::IsActive() const
{
    return m_active;
}

/* --------------------------- ReactiveJammer ----------------------------- */

void
ReactiveJammer::Install(Ptr<Node> node, Ptr<SpectrumChannel> ch, Ptr<WifiPhy> sensor)
{
    const Params& p = P();
    Time burst = MicroSeconds(p.burstUs);
    // Period = burst length, duty 1: each Start() emits one burst; Stop() prevents a repeat.
    m_wg = CreateNoiseSource(node, ch, sensor, p.jamPower, burst, 1.0);
    sensor->TraceConnectWithoutContext("PhyRxPayloadBegin",
                                       MakeCallback(&ReactiveJammer::OnPayloadBegin, this));
    std::ostringstream x;
    x << "type=reactive;power_dbm=" << p.jamPower << ";x=" << p.jamX << ";y=" << p.jamY;
    Simulator::Schedule(Seconds(p.jamStart), &EventLog::Log, std::string("JAM_ON"), 0u, int64_t(-1),
                        x.str());
    Simulator::Schedule(Seconds(p.jamStop), &EventLog::Log, std::string("JAM_OFF"), 0u,
                        int64_t(-1), std::string("type=reactive"));
}

bool
ReactiveJammer::IsActive() const
{
    double t = Simulator::Now().GetSeconds();
    return t >= P().jamStart && t < P().jamStop;
}

void
ReactiveJammer::OnPayloadBegin(WifiTxVector /*txVector*/, Time /*psduDuration*/)
{
    if (!IsActive() || Simulator::Now() < m_busyUntil)
        return;
    const Params& p = P();
    Time delay = MicroSeconds(p.reactDelayUs);
    Time burst = MicroSeconds(p.burstUs);
    m_busyUntil = Simulator::Now() + delay + burst;
    Simulator::Schedule(delay, &ReactiveJammer::BurstOn, this);
    // Stop half-way: the burst already on air continues for its full length,
    // stopping early just cancels the generator's automatic repeat.
    Simulator::Schedule(delay + burst / 2, &ReactiveJammer::BurstOff, this);
}

void
ReactiveJammer::BurstOn()
{
    m_bursts++;
    m_wg->Start();
}

void
ReactiveJammer::BurstOff()
{
    m_wg->Stop();
}

/* ----------------------------- Interferer ------------------------------- */

void
Interferer::Install(Ptr<Node> node, Ptr<SpectrumChannel> ch, Ptr<WifiPhy> refPhy)
{
    const Params& p = P();
    m_wg = CreateNoiseSource(node,
                             ch,
                             refPhy,
                             p.interfPower,
                             MilliSeconds(p.interfPeriodMs),
                             p.interfDuty);
    Simulator::Schedule(Seconds(p.jamStart), &WaveformGenerator::Start, m_wg);
    std::ostringstream x;
    x << "type=interferer;power_dbm=" << p.interfPower << ";duty=" << p.interfDuty;
    Simulator::Schedule(Seconds(p.jamStart), &EventLog::Log, std::string("INTERF_ON"), 0u,
                        int64_t(-1), x.str());
}

} // namespace v2v
} // namespace ns3
