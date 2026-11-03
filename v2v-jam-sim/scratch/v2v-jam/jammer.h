/*
 * jammer.h - RF jammers and the non-malicious interferer.
 *
 * All noise sources are ns-3 WaveformGenerators on the same SpectrumChannel as the
 * 802.11p radios. WiFi PHYs treat them as "foreign" (non-WiFi) energy:
 *   - it is added as interference -> lowers SINR -> frames fail to decode
 *   - above the CCA energy-detection threshold (-62 dBm) it also makes the
 *     channel look busy, so nearby radios defer (cannot transmit)
 *
 *   ConstantJammer  : transmits noise continuously between jamStart and jamStop.
 *   ReactiveJammer  : listens with a WiFi radio; when a frame payload starts it waits
 *                     reactDelayUs and then sends a burstUs noise burst over the frame.
 *   Interferer      : legitimate periodic interference (confounder), low duty cycle.
 *
 * Threat model: the jammer cannot forge messages and does not control RSUs/relays.
 */
#ifndef V2V_JAMMER_H
#define V2V_JAMMER_H

#include "common.h"

#include "ns3/spectrum-module.h"
#include "ns3/wifi-module.h"

namespace ns3
{
namespace v2v
{

/** Create a WaveformGenerator on 'node' that emits 'powerDbm' spread over the
 *  WiFi channel (centre/width taken from 'refPhy'). Not started yet. */
Ptr<WaveformGenerator> CreateNoiseSource(Ptr<Node> node,
                                         Ptr<SpectrumChannel> channel,
                                         Ptr<WifiPhy> refPhy,
                                         double powerDbm,
                                         Time period,
                                         double dutyCycle);

class ConstantJammer
{
  public:
    void Install(Ptr<Node> node, Ptr<SpectrumChannel> ch, Ptr<WifiPhy> refPhy);
    bool IsActive() const;

  private:
    void On();
    void Off();
    Ptr<WaveformGenerator> m_wg;
    bool m_active = false;
};

class ReactiveJammer
{
  public:
    /** 'sensor' is a WiFi PHY on the jammer node used only for listening. */
    void Install(Ptr<Node> node, Ptr<SpectrumChannel> ch, Ptr<WifiPhy> sensor);
    bool IsActive() const; // inside its attack window [jamStart, jamStop)
    uint64_t Bursts() const { return m_bursts; }

  private:
    void OnPayloadBegin(WifiTxVector txVector, Time psduDuration);
    void BurstOn();
    void BurstOff();
    Ptr<WaveformGenerator> m_wg;
    Time m_busyUntil;
    uint64_t m_bursts = 0;
};

class Interferer
{
  public:
    void Install(Ptr<Node> node, Ptr<SpectrumChannel> ch, Ptr<WifiPhy> refPhy);

  private:
    Ptr<WaveformGenerator> m_wg;
};

} // namespace v2v
} // namespace ns3

#endif
