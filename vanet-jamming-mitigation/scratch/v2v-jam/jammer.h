/*
 * Role B: same-channel jammer application for ns-3.47.
 *
 * This class deliberately models a jammer as high-rate UDP traffic sent through
 * the same WifiNetDevice as the vehicles.  It therefore competes for the same
 * 802.11p PHY/MAC channel without requiring a custom PHY implementation.
 */
#ifndef V2V_JAM_JAMMER_H
#define V2V_JAM_JAMMER_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/callback.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/wifi-phy.h"

namespace ns3
{

class Socket;

enum class JammerMode
{
    CONSTANT,
    REACTIVE
};

class JammerApplication : public Application
{
  public:
    static TypeId GetTypeId();

    JammerApplication();
    ~JammerApplication() override;

    /** Configure the receiver address and UDP port used for interference traffic. */
    void Setup(Address peer, JammerMode mode);

    /**
     * Connect this to a non-jammer WifiPhy PhyRxBegin trace for reactive mode.
     * A trigger schedules a finite burst after ReactiveDelay; repeated frames
     * during a pending burst are coalesced.
     */
    void ObserveFrame(Ptr<const Packet> packet, RxPowerWattPerChannelBand rxPowersW);

    void SetTxTrace(Callback<void, uint32_t> trace);

  private:
    void StartApplication() override;
    void StopApplication() override;
    void DoDispose() override;
    void SendOne();
    void StartReactiveBurst();
    void SendReactiveBurstPacket(uint32_t packetsRemaining);

    Address m_peer;
    Ptr<Socket> m_socket;
    EventId m_txEvent;
    EventId m_reactiveEvent;
    JammerMode m_mode{JammerMode::CONSTANT};
    bool m_running{false};
    uint32_t m_packetSize{1200};
    Time m_interval{MicroSeconds(500)};
    Time m_reactiveDelay{MicroSeconds(100)};
    Time m_burstSpacing{MicroSeconds(100)};
    uint32_t m_burstPackets{8};
    Callback<void, uint32_t> m_txTrace;
};

} // namespace ns3

#endif // V2V_JAM_JAMMER_H
