#include "jammer.h"

#include "ns3/inet-socket-address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/socket.h"
#include "ns3/udp-socket-factory.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("V2vJammer");
NS_OBJECT_ENSURE_REGISTERED(JammerApplication);

TypeId
JammerApplication::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::JammerApplication")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<JammerApplication>()
            .AddAttribute("PacketSize",
                          "Bytes in each interference packet.",
                          UintegerValue(1200),
                          MakeUintegerAccessor(&JammerApplication::m_packetSize),
                          MakeUintegerChecker<uint32_t>(1))
            .AddAttribute("Interval",
                          "Spacing for a constant jammer.",
                          TimeValue(MicroSeconds(500)),
                          MakeTimeAccessor(&JammerApplication::m_interval),
                          MakeTimeChecker())
            .AddAttribute("ReactiveDelay",
                          "Delay between sensing a frame and starting a reactive burst.",
                          TimeValue(MicroSeconds(100)),
                          MakeTimeAccessor(&JammerApplication::m_reactiveDelay),
                          MakeTimeChecker())
            .AddAttribute("BurstSpacing",
                          "Spacing between packets in a reactive burst.",
                          TimeValue(MicroSeconds(100)),
                          MakeTimeAccessor(&JammerApplication::m_burstSpacing),
                          MakeTimeChecker())
            .AddAttribute("BurstPackets",
                          "Number of packets transmitted for each reactive trigger.",
                          UintegerValue(8),
                          MakeUintegerAccessor(&JammerApplication::m_burstPackets),
                          MakeUintegerChecker<uint32_t>(1));
    return tid;
}

JammerApplication::JammerApplication() = default;
JammerApplication::~JammerApplication() = default;

void
JammerApplication::Setup(Address peer, JammerMode mode)
{
    m_peer = peer;
    m_mode = mode;
}

void
JammerApplication::SetTxTrace(Callback<void, uint32_t> trace)
{
    m_txTrace = trace;
}

void
JammerApplication::StartApplication()
{
    m_running = true;
    m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
    // A jammer broadcasts; it never ARP-resolves a victim, and (being deaf to
    // carrier sense) could not hear an ARP reply anyway.
    m_socket->SetAllowBroadcast(true);
    if (m_socket->Connect(m_peer) == -1)
    {
        NS_FATAL_ERROR("JammerApplication could not connect its UDP socket");
    }
    if (m_mode == JammerMode::CONSTANT)
    {
        m_txEvent = Simulator::ScheduleNow(&JammerApplication::SendOne, this);
    }
}

void
JammerApplication::StopApplication()
{
    m_running = false;
    m_txEvent.Cancel();
    m_reactiveEvent.Cancel();
    if (m_socket)
    {
        m_socket->Close();
    }
}

void
JammerApplication::DoDispose()
{
    m_socket = nullptr;
    Application::DoDispose();
}

void
JammerApplication::SendOne()
{
    if (!m_running)
    {
        return;
    }
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    m_socket->Send(packet);
    if (!m_txTrace.IsNull())
    {
        m_txTrace(m_packetSize);
    }
    m_txEvent = Simulator::Schedule(m_interval, &JammerApplication::SendOne, this);
}

void
JammerApplication::ObserveFrame(Ptr<const Packet> packet, RxPowerWattPerChannelBand rxPowersW)
{
    (void)packet;
    (void)rxPowersW;
    if (m_running && m_mode == JammerMode::REACTIVE && m_reactiveEvent.IsExpired())
    {
        m_reactiveEvent =
            Simulator::Schedule(m_reactiveDelay, &JammerApplication::StartReactiveBurst, this);
    }
}

void
JammerApplication::StartReactiveBurst()
{
    SendReactiveBurstPacket(m_burstPackets);
}

void
JammerApplication::SendReactiveBurstPacket(uint32_t packetsRemaining)
{
    if (!m_running || packetsRemaining == 0)
    {
        return;
    }
    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    m_socket->Send(packet);
    if (!m_txTrace.IsNull())
    {
        m_txTrace(m_packetSize);
    }
    if (packetsRemaining > 1)
    {
        m_reactiveEvent = Simulator::Schedule(m_burstSpacing,
                                               &JammerApplication::SendReactiveBurstPacket,
                                               this,
                                               packetsRemaining - 1);
    }
}

} // namespace ns3
