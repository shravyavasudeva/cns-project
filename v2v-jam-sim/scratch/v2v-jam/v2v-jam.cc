/*
 * v2v-jam.cc - Entry point. Wires the whole simulation together.
 *
 *   Cross-Layer Resilience for Jammed V2V Safety Communication (ECE 6612)
 *
 * Build & run (from the ns-3.47 root folder, with this folder at scratch/v2v-jam):
 *   ./ns3 build
 *   ./ns3 run "v2v-jam --scenario=highway --jammer=constant --mitigation=3 --seed=1"
 *   ./ns3 run "v2v-jam --PrintHelp"          (list every flag)
 *
 * Node ids: vehicles first (0 .. V-1), then RSUs, then the jammer, then the interferer.
 *
 * Output files in --outDir (default results/run):
 *   params.json, nodes.csv, events.csv, beacons.csv, pdr_by_jamdist.csv, node_summary.csv
 *   (+ positions.csv with --posLog=1, anim.xml with --anim=1)
 */
#include "common.h"
#include "detector.h"
#include "jammer.h"
#include "safety-app.h"
#include "scenario.h"
#include "stats.h"

#include "ns3/core-module.h"
#include "ns3/mobility-module.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/propagation-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/wifi-module.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace ns3;
using namespace ns3::v2v;

NS_LOG_COMPONENT_DEFINE("V2vJam");

namespace
{
ConstantJammer g_constJammer;
ReactiveJammer g_reactJammer;
Ptr<PropagationLossModel> g_gtLoss; // deterministic copy of the channel loss (no fading)
Ptr<MobilityModel> g_jamMob;
std::vector<bool> g_gtState;
uint32_t g_alertId = 0;
Ptr<UniformRandomVariable> g_alertRng;

bool
JammerActive()
{
    const Params& p = P();
    if (p.jammer == "constant")
        return g_constJammer.IsActive();
    if (p.jammer == "reactive")
        return g_reactJammer.IsActive();
    return false;
}

/** Every 0.1 s: is each node "under jamming" (ground truth)? Score the detectors. */
void
GroundTruthTick()
{
    const Params& p = P();
    bool active = JammerActive();
    for (uint32_t id = 0; id < Registry().size(); ++id)
    {
        NodeInfo& ni = Info(id);
        if (ni.kind != KIND_VEHICLE && ni.kind != KIND_RSU)
            continue;
        bool gt = false;
        double jamDbm = -200;
        if (active && g_jamMob)
        {
            jamDbm = g_gtLoss->CalcRxPower(p.jamPower, g_jamMob, ni.mob);
            gt = jamDbm >= p.gtJamDbm;
        }
        if (gt != g_gtState[id])
        {
            std::ostringstream x;
            x.precision(4);
            x << "jam_dbm=" << jamDbm;
            EventLog::Log(gt ? "GT_ON" : "GT_OFF", id, -1, x.str());
            g_gtState[id] = gt;
        }
        ni.app->GetDetector()->ScoreSample(gt);
    }
    Simulator::Schedule(MilliSeconds(100), &GroundTruthTick);
}

/** One emergency alert: a random vehicle near the hotspot raises it. */
void
AlertTick()
{
    const Params& p = P();
    std::vector<uint32_t> vehicles = VehicleIds();
    std::vector<uint32_t> cand;
    for (uint32_t v : vehicles)
        if (Dist2D(PosOf(v), p.hotspot) <= p.alertSrcRadius)
            cand.push_back(v);
    if (cand.empty())
        cand = vehicles;

    uint32_t src = cand[g_alertRng->GetInteger(0, cand.size() - 1)];
    uint32_t alertId = ++g_alertId;

    // Targets = every other vehicle within alertRadius of the source right now.
    for (uint32_t v : vehicles)
    {
        if (v == src)
            continue;
        double d = DistBetween(src, v);
        if (d <= p.alertRadius)
        {
            std::ostringstream x;
            x.precision(4);
            x << "src=" << src << ";dist=" << d;
            EventLog::Log("ALERT_TARGET", v, alertId, x.str());
        }
    }
    Info(src).app->TriggerAlert(alertId);
}

/** Schedule all alerts: every alertInterval, plus a random 0-200 ms offset so alerts do not
 *  line up with periodic sources (interferer, beacon phases). Same times for every config. */
void
ScheduleAlerts()
{
    const Params& p = P();
    for (double t = p.alertStart; t < p.simTime - 0.5; t += p.alertInterval)
        Simulator::Schedule(Seconds(t + g_alertRng->GetValue(0, 0.2)), &AlertTick);
}

void
WriteNodesCsv(const std::string& path)
{
    std::ofstream o(path);
    o << "node,kind,x,y\n";
    for (uint32_t id = 0; id < Registry().size(); ++id)
    {
        Vector v = PosOf(id);
        o << id << ',' << KindName(Info(id).kind) << ',' << v.x << ',' << v.y << '\n';
    }
}

void
WriteNodeSummary(const std::string& path)
{
    std::ofstream o(path);
    o << "node,kind,beacons_tx,beacons_rx,max_gap_ms,interrupt_ms,"
         "tp,fn,fp,tn,first_gt_ms,first_detect_ms,n_normal,n_congested,n_weak,n_jammed\n";
    for (uint32_t id = 0; id < Registry().size(); ++id)
    {
        NodeInfo& ni = Info(id);
        if (!ni.app)
            continue;
        o << ni.app->SummaryCsv() << ',' << ni.app->GetDetector()->SummaryCsv() << '\n';
    }
}
} // namespace

int
main(int argc, char* argv[])
{
    Params& p = P();

    CommandLine cmd(__FILE__);
    // experiment
    cmd.AddValue("scenario", "highway | local | urban", p.scenario);
    cmd.AddValue("mitigation", "0 none, 1 detect+repeat, 2 +L3 reroute, 3 +L4 deadline/priority", p.mitigation);
    cmd.AddValue("jammer", "none | constant | reactive", p.jammer);
    cmd.AddValue("confounder", "none | congestion | weak | interference", p.confounder);
    cmd.AddValue("seed", "random run number", p.seed);
    cmd.AddValue("simTime", "simulation length (s)", p.simTime);
    cmd.AddValue("outDir", "output folder", p.outDir);
    // traffic
    cmd.AddValue("vehicles", "number of vehicles (0 = scenario default)", p.vehicles);
    cmd.AddValue("speed", "mean speed m/s (-1 = scenario default)", p.speed);
    cmd.AddValue("rsu", "1 = place RSUs, 0 = none (-1 = scenario default)", p.rsu);
    cmd.AddValue("rsuSpacing", "RSU spacing along the road (m)", p.rsuSpacing);
    // radio
    cmd.AddValue("txPower", "vehicle TX power (dBm)", p.txPower);
    cmd.AddValue("rsuTxPower", "RSU TX power (dBm)", p.rsuTxPower);
    cmd.AddValue("pathLossExp", "log-distance exponent (-1 = scenario default)", p.pathLossExp);
    cmd.AddValue("fading", "add Nakagami fading", p.fading);
    cmd.AddValue("rxThreshold", "preamble detection / CCA sensitivity (dBm)", p.rxThreshold);
    cmd.AddValue("channel", "802.11p channel number (172..184)", p.channel);
    // jammer
    cmd.AddValue("jamPower", "jammer power (dBm)", p.jamPower);
    cmd.AddValue("jamX", "jammer x (m, -1 = hotspot)", p.jamX);
    cmd.AddValue("jamY", "jammer y (m, -1 = hotspot + 20 m)", p.jamY);
    cmd.AddValue("jamStart", "jamming start (s)", p.jamStart);
    cmd.AddValue("jamStop", "jamming stop (s, -1 = end)", p.jamStop);
    cmd.AddValue("reactDelayUs", "reactive jammer reaction delay (us)", p.reactDelayUs);
    cmd.AddValue("burstUs", "reactive jammer burst length (us)", p.burstUs);
    cmd.AddValue("gtJamDbm", "ground truth: jammer power at a node that counts as jammed (dBm)", p.gtJamDbm);
    // confounders
    cmd.AddValue("interfPower", "interferer power (dBm)", p.interfPower);
    cmd.AddValue("interfPeriodMs", "interferer period (ms)", p.interfPeriodMs);
    cmd.AddValue("interfDuty", "interferer duty cycle (0..1)", p.interfDuty);
    // application
    cmd.AddValue("beaconHz", "beacon rate (Hz)", p.beaconHz);
    cmd.AddValue("beaconBytes", "beacon size (bytes)", p.beaconBytes);
    cmd.AddValue("alertBytes", "alert size (bytes)", p.alertBytes);
    cmd.AddValue("alertStart", "first alert time (s)", p.alertStart);
    cmd.AddValue("alertInterval", "time between alerts (s)", p.alertInterval);
    cmd.AddValue("alertRadius", "alert target radius (m)", p.alertRadius);
    cmd.AddValue("alertSrcRadius", "alert sources are chosen within this radius of the hotspot (m)", p.alertSrcRadius);
    cmd.AddValue("deadlineMs", "safety deadline (ms)", p.deadlineMs);
    cmd.AddValue("repeatMs", "alert repeat interval when jamming is suspected (ms)", p.repeatMs);
    cmd.AddValue("pdrRadius", "beacon PDR receiver radius (m)", p.pdrRadius);
    // detector
    cmd.AddValue("detWindow", "detector window (s)", p.detWindow);
    cmd.AddValue("detPeriod", "detector evaluation period (s)", p.detPeriod);
    cmd.AddValue("busySampleMs", "channel sampling period (ms)", p.busySampleMs);
    cmd.AddValue("edBusyThr", "busy ratio = energy flood", p.edBusyThr);
    cmd.AddValue("congBusyThr", "busy ratio = congestion", p.congBusyThr);
    cmd.AddValue("ccaJamThr", "CCA_BUSY-only ratio (non-frame energy) = jammer", p.ccaJamThr);
    cmd.AddValue("pdrThr", "neighbour PDR threshold", p.pdrThr);
    cmd.AddValue("errThr", "decode-error ratio threshold", p.errThr);
    cmd.AddValue("noiseThr", "elevated noise+interference threshold (dBm)", p.noiseThr);
    cmd.AddValue("linkRange", "expected neighbour range (m)", p.linkRange);
    cmd.AddValue("minLinkJam", "jammed links needed to suspect jamming", p.minLinkJam);
    cmd.AddValue("enterCount", "evaluations to raise JAMMED", p.enterCount);
    cmd.AddValue("exitCount", "evaluations to clear JAMMED", p.exitCount);
    // routing / L4
    cmd.AddValue("ackTimeoutMs", "implicit ACK timeout (ms)", p.ackTimeoutMs);
    cmd.AddValue("maxSwitches", "max path switches per alert", p.maxSwitches);
    cmd.AddValue("backhaulMs", "RSU backhaul latency (ms)", p.backhaulMs);
    cmd.AddValue("relayMinRssi", "minimum RSSI for a relay (dBm)", p.relayMinRssi);
    cmd.AddValue("beaconHzJam", "beacon rate while jammed, mitigation 3 (Hz)", p.beaconHzJam);
    cmd.AddValue("serviceMs", "L4 queue service tick (ms)", p.serviceMs);
    // outputs
    cmd.AddValue("posLog", "write positions.csv", p.posLog);
    cmd.AddValue("anim", "write NetAnim anim.xml", p.anim);
    cmd.Parse(argc, argv);

    ApplyScenarioDefaults(p);
    RngSeedManager::SetSeed(6612);
    RngSeedManager::SetRun(p.seed);

    std::filesystem::create_directories(p.outDir);
    {
        std::ofstream j(p.outDir + "/params.json");
        j << ParamsToJson(p);
    }
    EventLog::Open(p.outDir + "/events.csv");

    std::cout << "v2v-jam: scenario=" << p.scenario << " jammer=" << p.jammer
              << " confounder=" << p.confounder << " mitigation=" << p.mitigation
              << " vehicles=" << p.vehicles << " seed=" << p.seed << " -> " << p.outDir
              << std::endl;

    /* ------------------------------ nodes ------------------------------- */
    std::vector<Vector> rsuPos = RsuPositions();
    NodeContainer vehicles;
    vehicles.Create(p.vehicles);
    NodeContainer rsus;
    rsus.Create(rsuPos.size());
    NodeContainer jammerNode;
    if (p.jammer != "none")
        jammerNode.Create(1);
    NodeContainer interfNode;
    if (p.confounder == "interference")
        interfNode.Create(1);

    BuildVehicleMobility(vehicles);
    StartWrapAround(vehicles);
    PlaceStatic(rsus, rsuPos);
    if (jammerNode.GetN())
        PlaceStatic(jammerNode, {Vector(p.jamX, p.jamY, 0)});
    if (interfNode.GetN())
        PlaceStatic(interfNode, {Vector(p.jamX, p.jamY, 0)}); // same spot as a jammer would be

    /* ------------------------- channel and radios ----------------------- */
    Ptr<MultiModelSpectrumChannel> channel = CreateObject<MultiModelSpectrumChannel>();
    Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel>();
    loss->SetAttribute("Exponent", DoubleValue(p.pathLossExp));
    loss->SetAttribute("ReferenceDistance", DoubleValue(1.0));
    loss->SetAttribute("ReferenceLoss", DoubleValue(p.refLoss));
    if (p.fading)
        loss->SetNext(CreateObject<NakagamiPropagationLossModel>());
    channel->AddPropagationLossModel(loss);
    channel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());

    // Ground-truth copy of the deterministic path loss (no fading).
    Ptr<LogDistancePropagationLossModel> gtLoss = CreateObject<LogDistancePropagationLossModel>();
    gtLoss->SetAttribute("Exponent", DoubleValue(p.pathLossExp));
    gtLoss->SetAttribute("ReferenceDistance", DoubleValue(1.0));
    gtLoss->SetAttribute("ReferenceLoss", DoubleValue(p.refLoss));
    g_gtLoss = gtLoss;

    SpectrumWifiPhyHelper phy;
    phy.SetChannel(channel);
    std::ostringstream chs;
    chs << "{" << p.channel << ", 10, BAND_5GHZ, 0}";
    phy.Set("ChannelSettings", StringValue(chs.str()));
    phy.Set("TxPowerStart", DoubleValue(p.txPower));
    phy.Set("TxPowerEnd", DoubleValue(p.txPower));
    phy.Set("CcaSensitivity", DoubleValue(p.rxThreshold));
    phy.SetPreambleDetectionModel("ns3::ThresholdPreambleDetectionModel",
                                  "MinimumRssi", DoubleValue(p.rxThreshold),
                                  "Threshold", DoubleValue(4.0));
    phy.SetErrorRateModel("ns3::NistErrorRateModel");

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211p);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate6MbpsBW10MHz"),
                                 "ControlMode", StringValue("OfdmRate6MbpsBW10MHz"),
                                 "NonUnicastMode", StringValue("OfdmRate6MbpsBW10MHz"));
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac"); // no association needed, like 802.11p OCB mode

    NodeContainer radioNodes(vehicles, rsus);
    if (p.jammer == "reactive")
        radioNodes.Add(jammerNode); // reactive jammer needs a receiver to sense frames
    NetDeviceContainer devs = wifi.Install(phy, mac, radioNodes);

    /* ---------------------------- registry ------------------------------ */
    Registry().resize(NodeList::GetNNodes());
    g_gtState.assign(NodeList::GetNNodes(), false);
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i)
    {
        Ptr<Node> n = NodeList::GetNode(i);
        Registry()[i].node = n;
        Registry()[i].mob = n->GetObject<MobilityModel>();
    }
    for (uint32_t i = 0; i < vehicles.GetN(); ++i)
        Info(vehicles.Get(i)->GetId()).kind = KIND_VEHICLE;
    for (uint32_t i = 0; i < rsus.GetN(); ++i)
        Info(rsus.Get(i)->GetId()).kind = KIND_RSU;
    for (uint32_t i = 0; i < jammerNode.GetN(); ++i)
        Info(jammerNode.Get(i)->GetId()).kind = KIND_JAMMER;
    for (uint32_t i = 0; i < interfNode.GetN(); ++i)
        Info(interfNode.Get(i)->GetId()).kind = KIND_INTERFERER;
    for (uint32_t i = 0; i < devs.GetN(); ++i)
        Info(devs.Get(i)->GetNode()->GetId()).dev = devs.Get(i);

    // RSUs transmit with more power (taller antenna, mains powered).
    for (uint32_t i = 0; i < rsus.GetN(); ++i)
    {
        Ptr<WifiPhy> rp = DynamicCast<WifiNetDevice>(Info(rsus.Get(i)->GetId()).dev)->GetPhy();
        rp->SetTxPowerStart(p.rsuTxPower);
        rp->SetTxPowerEnd(p.rsuTxPower);
    }

    /* --------------------------- applications --------------------------- */
    NodeContainer appNodes(vehicles, rsus);
    for (uint32_t i = 0; i < appNodes.GetN(); ++i)
    {
        Ptr<Node> n = appNodes.Get(i);
        NodeInfo& ni = Info(n->GetId());
        Ptr<SafetyApp> app = CreateObject<SafetyApp>();
        app->Setup(n->GetId(), ni.kind, DynamicCast<WifiNetDevice>(ni.dev));
        n->AddApplication(app);
        app->SetStartTime(Seconds(0.1));
        app->SetStopTime(Seconds(p.simTime));
        ni.app = PeekPointer(app);
    }

    /* ------------------------ jammer / confounders ---------------------- */
    Ptr<WifiPhy> refPhy = DynamicCast<WifiNetDevice>(devs.Get(0))->GetPhy();
    std::cout << "  802.11p channel " << p.channel << ": " << refPhy->GetFrequency() << " MHz, "
              << refPhy->GetChannelWidth() << " MHz wide" << std::endl;
    if (p.jammer == "constant")
    {
        g_constJammer.Install(jammerNode.Get(0), channel, refPhy);
        g_jamMob = jammerNode.Get(0)->GetObject<MobilityModel>();
    }
    else if (p.jammer == "reactive")
    {
        Ptr<WifiPhy> sensor =
            DynamicCast<WifiNetDevice>(Info(jammerNode.Get(0)->GetId()).dev)->GetPhy();
        g_reactJammer.Install(jammerNode.Get(0), channel, sensor);
        g_jamMob = jammerNode.Get(0)->GetObject<MobilityModel>();
    }
    Interferer interferer;
    if (interfNode.GetN())
        interferer.Install(interfNode.Get(0), channel, refPhy);

    /* ------------------------- monitors & outputs ----------------------- */
    g_alertRng = CreateObject<UniformRandomVariable>();
    g_alertRng->SetStream(1000); // same alert sources for every mitigation config
    ScheduleAlerts();
    Simulator::Schedule(MilliSeconds(100), &GroundTruthTick);
    Stats::Init(p.outDir);
    WriteNodesCsv(p.outDir + "/nodes.csv");
    if (p.posLog)
        StartPositionLog(p.outDir + "/positions.csv");

    AnimationInterface* anim = nullptr;
    if (p.anim)
    {
        anim = new AnimationInterface(p.outDir + "/anim.xml");
        anim->SetMobilityPollInterval(Seconds(0.25));
        anim->SkipPacketTracing();
        for (uint32_t id = 0; id < Registry().size(); ++id)
        {
            switch (Info(id).kind)
            {
            case KIND_VEHICLE:
                anim->UpdateNodeColor(id, 0, 102, 204);
                break;
            case KIND_RSU:
                anim->UpdateNodeColor(id, 0, 153, 0);
                anim->UpdateNodeSize(id, 8, 8);
                break;
            default:
                anim->UpdateNodeColor(id, 204, 0, 0);
                anim->UpdateNodeSize(id, 10, 10);
                break;
            }
        }
    }

    /* ------------------------------- run -------------------------------- */
    Simulator::Stop(Seconds(p.simTime));
    Simulator::Run();

    WriteNodeSummary(p.outDir + "/node_summary.csv");
    Stats::Finish(p.outDir);
    if (p.jammer == "reactive")
    {
        std::ostringstream x;
        x << "bursts=" << g_reactJammer.Bursts();
        EventLog::Log("JAM_STATS", 0, -1, x.str());
    }
    EventLog::Close();
    std::cout << "  done: " << g_alertId << " alerts generated" << std::endl;

    delete anim;
    Simulator::Destroy();
    return 0;
}
