// BEGIN optional NetAnim
#include "../common/routesense-animation.h"
// END optional NetAnim
#include "../common/routesense-measurement.h"

#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <filesystem>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>

using namespace ns3;

static void MoveNode(Ptr<Node> node, Vector position)
{
    Ptr<MobilityModel> mobility =
        node->GetObject<MobilityModel>();

    mobility->SetPosition(position);

    std::cout << "Topology event at "
              << Simulator::Now().GetSeconds()
              << " s: Node " << node->GetId()
              << " moved to (" << position.x << ", "
              << position.y << ")\n";
}

int main(int argc, char *argv[])
{
    // BEGIN optional NetAnim
    bool enableAnimation = false;
    std::string animationFile = "results/netanim/routesense-aodv.xml";
    // END optional NetAnim
    double drainTime = 15.0;
    bool diagnostics = false;
    double simulationTime = 30.0;
    double packetInterval = 0.5;
    std::string outputDir = "results/generated/aodv-new";
    uint32_t seed = 1;
    uint64_t run = 1;
    bool crossTraffic = false;
    bool dynamicScenario = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simulationTime", "Simulation duration",
                 simulationTime);
    cmd.AddValue("packetInterval", "UDP packet interval",
                 packetInterval);
    cmd.AddValue("crossTraffic", "Enable extra UDP flows",
                 crossTraffic);
    cmd.AddValue("dynamicScenario",
                 "Enable relay movement and link disruption",
                 dynamicScenario);
    cmd.AddValue("outputDir", "Directory for run outputs", outputDir);
    cmd.AddValue("seed", "Random seed", seed);
    cmd.AddValue("run", "Random run number", run);
    cmd.AddValue("drainTime", "Receive-only cleanup after traffic stops (seconds)", drainTime);
    cmd.AddValue("diagnostics", "Write PHY/MAC/ARP/IP event traces", diagnostics);
    // BEGIN optional NetAnim
    cmd.AddValue("enableAnimation", "Write actual node/mobility/packet events for NetAnim", enableAnimation);
    cmd.AddValue("animationFile", "Fresh NetAnim .xml output path (independent of outputDir)", animationFile);
    // END optional NetAnim
    cmd.Parse(argc, argv);
    RngSeedManager::SetSeed(seed);
    RngSeedManager::SetRun(run);

    if (!std::isfinite(drainTime) || drainTime <= 0 || !std::isfinite(simulationTime) || !std::isfinite(packetInterval) || simulationTime <= 4.0 || packetInterval <= 0.0 || Seconds(packetInterval).IsZero())
    {
        std::cerr << "Invalid simulation parameters.\n";
        return 1;
    }

    if (dynamicScenario && simulationTime <= 23.0)
    {
        std::cerr << "Dynamic scenario requires simulationTime > 23.\n";
        return 1;
    }

    const double appStop = simulationTime - 1.0;
    const double observationEnd = appStop + drainTime;
    const uint32_t nodeCount = 12;
    const double maxRange = 100.0;

    NodeContainer nodes;
    nodes.Create(nodeCount);

    // Configure the wireless ad hoc network
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay(
        "ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss(
        "ns3::RangePropagationLossModel",
        "MaxRange", DoubleValue(maxRange));

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode", StringValue("DsssRate11Mbps"),
        "ControlMode", StringValue("DsssRate1Mbps"));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    // Stationary positions for the initial topology
    MobilityHelper mobility;
    mobility.SetMobilityModel(
        "ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    Vector positions[nodeCount] = {
        Vector(0, 0, 0),
        Vector(80, 0, 0),
        Vector(160, 0, 0),
        Vector(240, 0, 0),
        Vector(320, 0, 0),
        Vector(400, 0, 0),
        Vector(80, 50, 0),
        Vector(160, 50, 0),
        Vector(240, 50, 0),
        Vector(320, 50, 0),
        Vector(160, -50, 0),
        Vector(240, -50, 0)
    };

    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        nodes.Get(i)->GetObject<MobilityModel>()
            ->SetPosition(positions[i]);
    }

    // Install AODV as the conventional routing protocol
    AodvHelper aodv;
    InternetStackHelper internet;
    internet.SetRoutingHelper(aodv);
    internet.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    const uint16_t mainPort = 9;
    const uint16_t secondPort = 10;
    const uint16_t thirdPort = 11;

    // Main application receiver: Node 5
    PacketSinkHelper mainSinkHelper(
        "ns3::UdpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), mainPort));

    ApplicationContainer mainSink =
        mainSinkHelper.Install(nodes.Get(5));

    mainSink.Start(Seconds(0.0));
    mainSink.Stop(Seconds(observationEnd));

    // Optional receivers for additional application flows
    if (crossTraffic)
    {
        PacketSinkHelper sink2(
            "ns3::UdpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), secondPort));

        PacketSinkHelper sink3(
            "ns3::UdpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), thirdPort));

        ApplicationContainer sinkApps2 =
            sink2.Install(nodes.Get(4));
        ApplicationContainer sinkApps3 =
            sink3.Install(nodes.Get(1));

        sinkApps2.Start(Seconds(0.0));
        sinkApps2.Stop(Seconds(observationEnd));
        sinkApps3.Start(Seconds(0.0));
        sinkApps3.Stop(Seconds(observationEnd));
    }

    // Main UDP flow: Node 0 -> Node 5
    UdpClientHelper mainClient(
        interfaces.GetAddress(5), mainPort);

    mainClient.SetAttribute(
        "MaxPackets", UintegerValue(1000000));
    mainClient.SetAttribute(
        "PacketSize", UintegerValue(1024));
    mainClient.SetAttribute(
        "Interval", TimeValue(Seconds(packetInterval)));

    ApplicationContainer mainApp =
        mainClient.Install(nodes.Get(0));

    mainApp.Start(Seconds(2.0));
    mainApp.Stop(Seconds(simulationTime - 1.0));

    // Optional competing UDP flows
    if (crossTraffic)
    {
        UdpClientHelper client2(
            interfaces.GetAddress(4), secondPort);

        client2.SetAttribute(
            "MaxPackets", UintegerValue(1000000));
        client2.SetAttribute(
            "PacketSize", UintegerValue(1024));
        client2.SetAttribute(
            "Interval", TimeValue(Seconds(packetInterval)));

        ApplicationContainer app2 =
            client2.Install(nodes.Get(10));

        app2.Start(Seconds(2.0));
        app2.Stop(Seconds(simulationTime - 1.0));

        UdpClientHelper client3(
            interfaces.GetAddress(1), thirdPort);

        client3.SetAttribute(
            "MaxPackets", UintegerValue(1000000));
        client3.SetAttribute(
            "PacketSize", UintegerValue(1024));
        client3.SetAttribute(
            "Interval", TimeValue(Seconds(packetInterval)));

        ApplicationContainer app3 =
            client3.Install(nodes.Get(11));

        app3.Start(Seconds(2.0));
        app3.Stop(Seconds(simulationTime - 1.0));
    }

    // Create controlled topology disruption and recovery
    if (dynamicScenario)
    {
        Simulator::Schedule(
            Seconds(12.0), &MoveNode,
            nodes.Get(1), Vector(80, 160, 0));

        Simulator::Schedule(
            Seconds(22.0), &MoveNode,
            nodes.Get(1), Vector(80, 0, 0));
    }

    // Flow statistics and packet captures
    std::error_code directoryError;
    if (std::filesystem::exists(outputDir, directoryError) &&
        !std::filesystem::is_empty(outputDir, directoryError))
    {
        std::cerr << "Output directory must be empty; choose a fresh --outputDir." << std::endl;
        return 1;
    }
    std::filesystem::create_directories(outputDir, directoryError);
    if (directoryError)
    {
        std::cerr << "Could not create output directory: " << directoryError.message() << std::endl;
        return 1;
    }

    routesense::Measurement measurement;
    measurement.Add(0, 5, mainPort, nodes, interfaces);
    if (crossTraffic)
    {
        measurement.Add(10, 4, secondPort, nodes, interfaces);
        measurement.Add(11, 1, thirdPort, nodes, interfaces);
    }
    measurement.Diagnose(nodes, devices, outputDir, diagnostics);

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    phy.EnablePcap(
        outputDir + "/aodv", devices, true);

    // Capture routing-table snapshots at selected times
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> routeStream =
        ascii.CreateFileStream(
            outputDir + "/routing-tables.txt");

    for (double t : {5.0, 13.0, 18.0, 23.0, 28.0})
    {
        if (t < simulationTime)
        {
            Ipv4RoutingHelper::PrintRoutingTableAllAt(
                Seconds(t), routeStream);
        }
    }

    std::cout << "\n========== AODV Baseline ==========\n";
    std::cout << "Seed: " << seed << " Run: " << run << std::endl;
    std::cout << "Nodes: " << nodeCount << '\n';
    std::cout << "Routing protocol: AODV\n";
    std::cout << "Main flow: Node 0 -> Node 5\n";
    std::cout << "Dynamic scenario: "
              << (dynamicScenario ? "Enabled" : "Disabled")
              << '\n';
    std::cout << "Cross traffic: "
              << (crossTraffic ? "Enabled" : "Disabled")
              << '\n';

    // BEGIN optional NetAnim
    std::unique_ptr<AnimationInterface> animation;
    if (!routesense::ConfigureAnimation(enableAnimation, animationFile, outputDir,
                                        "aodv-flowmon.xml", nodes, interfaces,
                                        crossTraffic, dynamicScenario, observationEnd, animation))
    {
        Simulator::Destroy();
        return 1;
    }
    // END optional NetAnim
    Simulator::Stop(Seconds(observationEnd));
    Simulator::Run();

    monitor->CheckForLostPackets();

    measurement.Write(outputDir, "routesense-aodv", RngSeedManager::GetSeed(), RngSeedManager::GetRun(),
                      appStop, drainTime, packetInterval, dynamicScenario, crossTraffic,
                      monitor, DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier()));

    monitor->SerializeToXmlFile(
        outputDir + "/aodv-flowmon.xml",
        true, true);

    Simulator::Destroy();
    return 0;
}
