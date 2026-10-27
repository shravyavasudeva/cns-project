/* Role B standalone jammer/PDR sanity harness (ns-3.47). */
#include "jammer.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iostream>

using namespace ns3;

namespace
{
uint32_t g_tx = 0;
uint32_t g_rx = 0;

void
CountTx(Ptr<const Packet> packet, const Address&, const Address&)
{
    (void)packet;
    ++g_tx;
}

void
CountRx(Ptr<const Packet> packet, const Address&, const Address&)
{
    (void)packet;
    ++g_rx;
}
} // namespace

int
main(int argc, char* argv[])
{
    std::string jammerMode = "constant";
    double jammerPowerDbm = 18.0;
    double jammerDistanceM = 20.0;
    uint32_t jammerIntervalUs = 100;
    double duration = 10.0;
    CommandLine cmd(__FILE__);
    cmd.AddValue("jammer", "none, constant, or reactive", jammerMode);
    cmd.AddValue("jammerPowerDbm", "Jammer PHY transmit power in dBm", jammerPowerDbm);
    cmd.AddValue("jammerDistanceM", "Jammer distance beyond the receiver in metres", jammerDistanceM);
    cmd.AddValue("jammerIntervalUs", "Constant-jammer packet spacing in microseconds", jammerIntervalUs);
    cmd.AddValue("duration", "Simulation duration in seconds", duration);
    cmd.Parse(argc, argv);
    if (jammerMode != "none" && jammerMode != "constant" && jammerMode != "reactive")
    {
        NS_FATAL_ERROR("--jammer must be none, constant, or reactive");
    }

    NodeContainer vehicles;
    vehicles.Create(2); // vehicle 0 sends safety traffic to vehicle 1
    NodeContainer jammerNode;
    if (jammerMode != "none")
    {
        jammerNode.Create(1);
    }
    NodeContainer allNodes;
    allNodes.Add(vehicles);
    allNodes.Add(jammerNode);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    positions->Add(Vector(0.0, 0.0, 1.5));
    // Keep the baseline V2V link comfortably decodable; the sweep changes only
    // the jammer-to-receiver separation so its effect is interpretable.
    positions->Add(Vector(20.0, 0.0, 1.5));
    if (jammerMode != "none")
    {
        positions->Add(Vector(20.0 + jammerDistanceM, 0.0, 1.5));
    }
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allNodes);

    Ptr<YansWifiChannel> channel = YansWifiChannelHelper::Default().Create();
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211p);
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    YansWifiPhyHelper vehiclePhy;
    vehiclePhy.SetChannel(channel);
    vehiclePhy.Set("TxPowerStart", DoubleValue(16.0));
    vehiclePhy.Set("TxPowerEnd", DoubleValue(16.0));
    NetDeviceContainer vehicleDevices = wifi.Install(vehiclePhy, mac, vehicles);

    NetDeviceContainer jammerDevices;
    if (jammerMode != "none")
    {
        YansWifiPhyHelper jammerPhy;
        jammerPhy.SetChannel(channel);
        jammerPhy.Set("TxPowerStart", DoubleValue(jammerPowerDbm));
        jammerPhy.Set("TxPowerEnd", DoubleValue(jammerPowerDbm));
        // A jammer does not cooperate with 802.11 carrier sensing.  Raising
        // its CCA threshold makes it transmit through legitimate activity.
        // Deaf jammer: ignore energy- and preamble-based carrier sense so it
        // transmits through legitimate 802.11p activity instead of deferring.
        jammerPhy.Set("CcaEdThreshold", DoubleValue(100.0));
        jammerPhy.Set("CcaSensitivity", DoubleValue(100.0));
        jammerPhy.Set("RxSensitivity", DoubleValue(100.0));
        jammerDevices = wifi.Install(jammerPhy, mac, jammerNode);
    }

    InternetStackHelper internet;
    internet.Install(allNodes);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.1.0.0", "255.255.255.0");
    Ipv4InterfaceContainer vehicleIfaces = addresses.Assign(vehicleDevices);
    Ipv4InterfaceContainer jammerIfaces;
    if (jammerMode != "none")
    {
        jammerIfaces = addresses.Assign(jammerDevices);
    }

    constexpr uint16_t safetyPort = 5000;
    constexpr uint16_t jammerPort = 5001;
    UdpServerHelper safetyServer(safetyPort);
    ApplicationContainer safetySink = safetyServer.Install(vehicles.Get(1));
    safetySink.Start(Seconds(0));
    safetySink.Stop(Seconds(duration));
    Ptr<UdpServer> server = DynamicCast<UdpServer>(safetySink.Get(0));
    server->TraceConnectWithoutContext("RxWithAddresses", MakeCallback(&CountRx));

    UdpClientHelper beacon(vehicleIfaces.GetAddress(1), safetyPort);
    beacon.SetAttribute("MaxPackets", UintegerValue(static_cast<uint32_t>((duration - 1.0) * 10)));
    beacon.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
    beacon.SetAttribute("PacketSize", UintegerValue(200));
    ApplicationContainer beaconApp = beacon.Install(vehicles.Get(0));
    beaconApp.Start(Seconds(1));
    beaconApp.Stop(Seconds(duration));
    Ptr<UdpClient> source = DynamicCast<UdpClient>(beaconApp.Get(0));
    source->TraceConnectWithoutContext("TxWithAddresses", MakeCallback(&CountTx));

    if (jammerMode != "none")
    {
        // A separate sink prevents jammer packets from contaminating safety RX counts.
        UdpServerHelper jammerServer(jammerPort);
        ApplicationContainer jammerSink = jammerServer.Install(vehicles.Get(1));
        jammerSink.Start(Seconds(0));
        jammerSink.Stop(Seconds(duration));

        Ptr<JammerApplication> jammer = CreateObject<JammerApplication>();
        const JammerMode mode = jammerMode == "constant" ? JammerMode::CONSTANT : JammerMode::REACTIVE;
        jammer->Setup(InetSocketAddress(Ipv4Address("10.1.0.255"), jammerPort), mode);
        jammer->SetAttribute("Interval", TimeValue(MicroSeconds(jammerIntervalUs)));
        jammerNode.Get(0)->AddApplication(jammer);
        jammer->SetStartTime(Seconds(2));
        jammer->SetStopTime(Seconds(duration));
        if (mode == JammerMode::REACTIVE)
        {
            Ptr<WifiNetDevice> sourceDevice = DynamicCast<WifiNetDevice>(vehicleDevices.Get(0));
            sourceDevice->GetPhy()->TraceConnectWithoutContext(
                "PhyRxBegin",
                MakeCallback(&JammerApplication::ObserveFrame, jammer));
        }
    }

    Simulator::Stop(Seconds(duration));
    Simulator::Run();
    std::cout << "PDR_RESULT," << jammerMode << ',' << jammerPowerDbm << ',' << jammerDistanceM << ','
              << g_tx << ',' << g_rx << std::endl;
    Simulator::Destroy();
    return 0;
}
