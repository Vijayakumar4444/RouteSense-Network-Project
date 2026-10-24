
#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <cstdlib>
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
    double simulationTime = 30.0;
    double packetInterval = 0.5;
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
    cmd.Parse(argc, argv);

    if (simulationTime <= 4.0 || packetInterval <= 0.0)
    {
        std::cerr << "Invalid simulation parameters.\n";
        return 1;
    }

    if (dynamicScenario && simulationTime <= 23.0)
    {
        std::cerr << "Dynamic scenario requires simulationTime > 23.\n";
        return 1;
    }

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
    mainSink.Stop(Seconds(simulationTime));

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
        sinkApps2.Stop(Seconds(simulationTime));
        sinkApps3.Start(Seconds(0.0));
        sinkApps3.Stop(Seconds(simulationTime));
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
    if (std::system("mkdir -p results/baseline") != 0)
    {
        std::cerr << "Could not create results/baseline.\n";
        return 1;
    }

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    phy.EnablePcap(
        "results/baseline/aodv", devices, true);

    // Capture routing-table snapshots at selected times
    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> routeStream =
        ascii.CreateFileStream(
            "results/baseline/routing-tables.txt");

    for (double t : {5.0, 13.0, 18.0, 23.0, 28.0})
    {
        if (t < simulationTime)
        {
            Ipv4RoutingHelper::PrintRoutingTableAllAt(
                Seconds(t), routeStream);
        }
    }

    std::cout << "\n========== AODV Baseline ==========\n";
    std::cout << "Nodes: " << nodeCount << '\n';
    std::cout << "Routing protocol: AODV\n";
    std::cout << "Main flow: Node 0 -> Node 5\n";
    std::cout << "Dynamic scenario: "
              << (dynamicScenario ? "Enabled" : "Disabled")
              << '\n';
    std::cout << "Cross traffic: "
              << (crossTraffic ? "Enabled" : "Disabled")
              << '\n';

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(
            flowHelper.GetClassifier());

    std::map<FlowId, FlowMonitor::FlowStats> stats =
        monitor->GetFlowStats();

    std::cout << std::fixed << std::setprecision(2);

    std::cout << "\nApplication flow metrics\n";

    for (const auto &entry : stats)
    {
        const FlowMonitor::FlowStats &flow = entry.second;
        Ipv4FlowClassifier::FiveTuple tuple =
            classifier->FindFlow(entry.first);

        // Exclude AODV control traffic from application metrics
        if (tuple.destinationPort != mainPort &&
            tuple.destinationPort != secondPort &&
            tuple.destinationPort != thirdPort)
        {
            continue;
        }

        if (flow.txPackets == 0)
        {
            continue;
        }

        double pdr =
            100.0 * flow.rxPackets / flow.txPackets;

        double lostPackets =
            flow.txPackets > flow.rxPackets
                ? flow.txPackets - flow.rxPackets
                : 0;

        double loss =
            100.0 * lostPackets / flow.txPackets;

        double delayMs = 0.0;
        if (flow.rxPackets > 0)
        {
            delayMs = 1000.0 *
                flow.delaySum.GetSeconds() / flow.rxPackets;
        }

        double jitterMs = 0.0;
        if (flow.rxPackets > 1)
        {
            jitterMs = 1000.0 * flow.jitterSum.GetSeconds()
                       / (flow.rxPackets - 1);
        }

        // Traffic runs from 2 seconds until simulationTime - 1 second.
        const double duration = simulationTime - 3.0;

        double throughputKbps = 0.0;
        if (flow.rxPackets > 0 && duration > 0.0)
        {
            throughputKbps =
                flow.rxBytes * 8.0 / duration / 1000.0;
        }

        std::cout << "\nFlow " << entry.first << ": "
                  << tuple.sourceAddress << " -> "
                  << tuple.destinationAddress << '\n';
        std::cout << "  TX packets: " << flow.txPackets << '\n';
        std::cout << "  RX packets: " << flow.rxPackets << '\n';
        std::cout << "  PDR: " << pdr << "%\n";
        std::cout << "  Packet loss ratio: " << loss << "%\n";
        std::cout << "  Throughput: "
                  << throughputKbps << " Kbps\n";
        std::cout << "  Average delay: "
                  << delayMs << " ms\n";
        std::cout << "  Average jitter: "
                  << jitterMs << " ms\n";
    }

    monitor->SerializeToXmlFile(
        "results/baseline/aodv-flowmon.xml",
        true, true);

    Simulator::Destroy();
    return 0;
}
