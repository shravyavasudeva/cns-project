/*
 * Baseline for the VANET jamming-mitigation study.
 * ns-3.47; copy to <ns-3>/scratch and run with ./ns3 run scratch/vanet-jamming
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("VanetJammingBaseline");

int
main(int argc, char* argv[])
{
    uint32_t vehicles = 15;
    bool jammer = true;
    double duration = 10.0;
    CommandLine cmd(__FILE__);
    cmd.AddValue("vehicles", "Number of vehicle nodes", vehicles);
    cmd.AddValue("jammer", "Enable interference source", jammer);
    cmd.AddValue("duration", "Simulation duration in seconds", duration);
    cmd.Parse(argc, argv);

    NodeContainer vehicleNodes;
    vehicleNodes.Create(vehicles);
    NodeContainer rsuNode;
    rsuNode.Create(1);
    NodeContainer jammerNode;
    if (jammer)
    {
        jammerNode.Create(1);
    }

    NodeContainer allNodes;
    allNodes.Add(vehicleNodes);
    allNodes.Add(rsuNode);
    if (jammer)
    {
        allNodes.Add(jammerNode);
    }

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < vehicles; ++i)
    {
        positions->Add(Vector(20.0 * i, 0.0, 1.5));
    }
    positions->Add(Vector(100.0, 8.0, 4.0)); // RSU
    if (jammer)
    {
        positions->Add(Vector(130.0, 3.0, 1.5));
    }
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allNodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211p);
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, allNodes);

    InternetStackHelper internet;
    internet.Install(allNodes);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.1.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

    // Periodic safety beacons from vehicle 0 to all nodes. This becomes the detection input.
    const uint16_t safetyPort = 5000;
    UdpServerHelper safetySink(safetyPort);
    ApplicationContainer sinks = safetySink.Install(vehicleNodes);
    sinks.Start(Seconds(0.0));
    sinks.Stop(Seconds(duration));

    UdpClientHelper beacon(interfaces.GetAddress(1), safetyPort);
    beacon.SetAttribute("MaxPackets", UintegerValue(100));
    beacon.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
    beacon.SetAttribute("PacketSize", UintegerValue(200));
    ApplicationContainer beacons = beacon.Install(vehicleNodes.Get(0));
    beacons.Start(Seconds(1.0));
    beacons.Stop(Seconds(duration));

    // One emergency message. Later replace with a timestamped/deadline-aware application.
    UdpClientHelper emergency(interfaces.GetAddress(1), safetyPort);
    emergency.SetAttribute("MaxPackets", UintegerValue(1));
    emergency.SetAttribute("Interval", TimeValue(MilliSeconds(100)));
    emergency.SetAttribute("PacketSize", UintegerValue(300));
    ApplicationContainer emergencyAlert = emergency.Install(vehicleNodes.Get(0));
    emergencyAlert.Start(Seconds(5.0));
    emergencyAlert.Stop(Seconds(5.2));

    // Controlled same-channel traffic approximates an always-on jammer for the baseline.
    if (jammer)
    {
        UdpClientHelper interference(interfaces.GetAddress(1), safetyPort);
        interference.SetAttribute("MaxPackets", UintegerValue(100000));
        interference.SetAttribute("Interval", TimeValue(MicroSeconds(500)));
        interference.SetAttribute("PacketSize", UintegerValue(1200));
        ApplicationContainer jammingTraffic = interference.Install(jammerNode.Get(0));
        jammingTraffic.Start(Seconds(2.0));
        jammingTraffic.Stop(Seconds(duration));
    }

    Simulator::Stop(Seconds(duration));
    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
