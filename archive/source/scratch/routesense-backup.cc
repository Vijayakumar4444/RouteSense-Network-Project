#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <vector>

using namespace ns3;

static NodeContainer g_nodes;
static NetDeviceContainer g_devices;
static Ipv4InterfaceContainer g_interfaces;
static double g_maxRange = 100.0;

class RouteSenseRoutingProtocol : public Ipv4RoutingProtocol
{
  public:
    static TypeId GetTypeId()
    {
        static TypeId tid =
            TypeId("ns3::RouteSenseRoutingProtocol")
                .SetParent<Ipv4RoutingProtocol>()
                .SetGroupName("Internet")
                .AddConstructor<RouteSenseRoutingProtocol>();
        return tid;
    }

    RouteSenseRoutingProtocol() = default;

    void SetNodeIndex(uint32_t index) { m_nodeIndex = index; }

    void SetIpv4(Ptr<Ipv4> ipv4) override { m_ipv4 = ipv4; }

    Ptr<Ipv4Route> RouteOutput(
        Ptr<Packet> packet,
        const Ipv4Header& header,
        Ptr<NetDevice> outputDevice,
        Socket::SocketErrno& sockerr) override
    {
        (void)packet;
        (void)outputDevice;

        Ptr<Ipv4Route> route = BuildRoute(header.GetDestination());
        if (route)
        {
            sockerr = Socket::ERROR_NOTERROR;
            return route;
        }

        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }

    bool RouteInput(
        Ptr<const Packet> packet,
        const Ipv4Header& header,
        Ptr<const NetDevice> inputDevice,
        const UnicastForwardCallback& unicastForward,
        const MulticastForwardCallback& multicastForward,
        const LocalDeliverCallback& localDeliver,
        const ErrorCallback& error) override
    {
        (void)multicastForward;

        int32_t inputInterface = m_ipv4->GetInterfaceForDevice(inputDevice);
        uint32_t iif = inputInterface >= 0
                           ? static_cast<uint32_t>(inputInterface)
                           : 0;

        for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); ++i)
        {
            for (uint32_t j = 0; j < m_ipv4->GetNAddresses(i); ++j)
            {
                if (m_ipv4->GetAddress(i, j).GetLocal() ==
                    header.GetDestination())
                {
                    localDeliver(packet, header, iif);
                    return true;
                }
            }
        }

        // This prototype handles unicast only; let other protocols handle multicast.
        if (header.GetDestination().IsMulticast())
        {
            return false;
        }

        Ptr<Ipv4Route> route = BuildRoute(header.GetDestination());
        if (route)
        {
            unicastForward(route, packet, header);
            return true;
        }

        error(packet, header, Socket::ERROR_NOROUTETOHOST);
        return true;
    }

    void NotifyInterfaceUp(uint32_t interface) override { (void)interface; }
    void NotifyInterfaceDown(uint32_t interface) override { (void)interface; }

    void NotifyAddAddress(
        uint32_t interface,
        Ipv4InterfaceAddress address) override
    {
        (void)interface;
        (void)address;
    }

    void NotifyRemoveAddress(
        uint32_t interface,
        Ipv4InterfaceAddress address) override
    {
        (void)interface;
        (void)address;
    }

    void PrintRoutingTable(
        Ptr<OutputStreamWrapper> stream,
        Time::Unit unit = Time::S) const override
    {
        std::ostream* out = stream->GetStream();
        *out << "\nRouteSense table for node " << m_nodeIndex
             << " at " << Simulator::Now().As(unit) << '\n';
        *out << "Destination\tNextHop\tScore\n";

        for (uint32_t destination = 0; destination < g_nodes.GetN(); ++destination)
        {
            if (destination == m_nodeIndex)
            {
                continue;
            }

            std::vector<uint32_t> path;
            double cost = 0.0;
            if (FindBestPath(m_nodeIndex, destination, path, cost) &&
                path.size() >= 2)
            {
                *out << g_interfaces.GetAddress(destination) << '\t'
                     << g_interfaces.GetAddress(path[1]) << '\t'
                     << 100.0 / (1.0 + cost) << '\n';
            }
        }
    }

  private:
    uint32_t m_nodeIndex{0};
    Ptr<Ipv4> m_ipv4;
    std::map<uint32_t, uint32_t> m_lastNextHop;

    double LinkCost(uint32_t a, uint32_t b) const
    {
        Vector pa = g_nodes.Get(a)->GetObject<MobilityModel>()->GetPosition();
        Vector pb = g_nodes.Get(b)->GetObject<MobilityModel>()->GetPosition();

        double dx = pa.x - pb.x;
        double dy = pa.y - pb.y;
        double dz = pa.z - pb.z;
        double distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (distance > g_maxRange)
        {
            return std::numeric_limits<double>::infinity();
        }

        double ratio = std::min(1.0, distance / g_maxRange);
        double stability = 1.0 - ratio;
        double reliability = 1.0 - 0.5 * ratio * ratio;
        double delayPenalty = ratio;

        const double wStability = 0.35;
        const double wReliability = 0.30;
        const double wDelay = 0.20;
        const double wHop = 0.15;

        return wStability * (1.0 - stability)
             + wReliability * (1.0 - reliability)
             + wDelay * delayPenalty
             + wHop;
    }

    bool FindBestPath(
        uint32_t source,
        uint32_t destination,
        std::vector<uint32_t>& path,
        double& totalCost) const
    {
        const uint32_t n = g_nodes.GetN();
        if (source >= n || destination >= n)
        {
            return false;
        }

        std::vector<double> distance(
            n, std::numeric_limits<double>::infinity());
        std::vector<int32_t> previous(n, -1);
        std::vector<bool> visited(n, false);
        distance[source] = 0.0;

        for (uint32_t iteration = 0; iteration < n; ++iteration)
        {
            int32_t current = -1;
            double best = std::numeric_limits<double>::infinity();

            for (uint32_t i = 0; i < n; ++i)
            {
                if (!visited[i] && distance[i] < best)
                {
                    best = distance[i];
                    current = static_cast<int32_t>(i);
                }
            }

            if (current < 0)
            {
                break;
            }
            if (static_cast<uint32_t>(current) == destination)
            {
                break;
            }

            visited[current] = true;
            for (uint32_t neighbor = 0; neighbor < n; ++neighbor)
            {
                if (neighbor == static_cast<uint32_t>(current) ||
                    visited[neighbor])
                {
                    continue;
                }

                double edgeCost =
                    LinkCost(static_cast<uint32_t>(current), neighbor);
                if (!std::isfinite(edgeCost))
                {
                    continue;
                }

                double candidate = distance[current] + edgeCost;
                if (candidate < distance[neighbor])
                {
                    distance[neighbor] = candidate;
                    previous[neighbor] = current;
                }
            }
        }

        if (!std::isfinite(distance[destination]))
        {
            return false;
        }

        path.clear();
        int32_t current = static_cast<int32_t>(destination);
        while (current >= 0)
        {
            path.push_back(static_cast<uint32_t>(current));
            if (static_cast<uint32_t>(current) == source)
            {
                break;
            }
            current = previous[current];
        }

        if (path.empty() || path.back() != source)
        {
            path.clear();
            return false;
        }

        std::reverse(path.begin(), path.end());
        totalCost = distance[destination];
        return path.size() >= 2;
    }

    Ptr<Ipv4Route> BuildRoute(Ipv4Address destination)
    {
        uint32_t destinationIndex = g_nodes.GetN();
        for (uint32_t i = 0; i < g_interfaces.GetN(); ++i)
        {
            if (g_interfaces.GetAddress(i) == destination)
            {
                destinationIndex = i;
                break;
            }
        }

        if (destinationIndex >= g_nodes.GetN() ||
            destinationIndex == m_nodeIndex)
        {
            return nullptr;
        }

        std::vector<uint32_t> path;
        double totalCost = 0.0;
        if (!FindBestPath(m_nodeIndex, destinationIndex, path, totalCost))
        {
            m_lastNextHop.erase(destinationIndex);
            return nullptr;
        }

        uint32_t nextHop = path[1];
        auto previous = m_lastNextHop.find(destinationIndex);
        if (previous == m_lastNextHop.end() || previous->second != nextHop)
        {
            std::cout << std::fixed << std::setprecision(2)
                      << "[RouteSense] t=" << Simulator::Now().GetSeconds()
                      << "s node=" << m_nodeIndex
                      << " destination=" << destination
                      << " nextHop=" << nextHop
                      << " score=" << 100.0 / (1.0 + totalCost) << '\n';
            m_lastNextHop[destinationIndex] = nextHop;
        }

        int32_t outputInterface =
            m_ipv4->GetInterfaceForDevice(g_devices.Get(m_nodeIndex));
        if (outputInterface < 0)
        {
            return nullptr;
        }

        Ptr<Ipv4Route> route = Create<Ipv4Route>();
        route->SetDestination(destination);
        route->SetGateway(g_interfaces.GetAddress(nextHop));
        route->SetOutputDevice(g_devices.Get(m_nodeIndex));
        route->SetSource(m_ipv4->GetAddress(outputInterface, 0).GetLocal());
        return route;
    }
};

NS_OBJECT_ENSURE_REGISTERED(RouteSenseRoutingProtocol);

static void MoveNode(Ptr<Node> node, Vector position)
{
    node->GetObject<MobilityModel>()->SetPosition(position);
    std::cout << "Topology event at " << Simulator::Now().GetSeconds()
              << " s: Node " << node->GetId() << " moved to ("
              << position.x << ", " << position.y << ")\n";
}

int main(int argc, char* argv[])
{
    double simulationTime = 30.0;
    double packetInterval = 0.5;
    bool crossTraffic = false;
    bool dynamicScenario = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("simulationTime", "Simulation duration", simulationTime);
    cmd.AddValue("packetInterval", "UDP packet interval", packetInterval);
    cmd.AddValue("crossTraffic", "Enable extra UDP flows", crossTraffic);
    cmd.AddValue("dynamicScenario", "Enable node movement", dynamicScenario);
    cmd.Parse(argc, argv);

    if (simulationTime <= 4.0 || packetInterval <= 0.0 ||
        (dynamicScenario && simulationTime <= 23.0))
    {
        std::cerr << "Invalid simulation parameters.\n";
        return 1;
    }

    if (std::system("mkdir -p results/routesense") != 0)
    {
        std::cerr << "Could not create results/routesense.\n";
        return 1;
    }

    const uint32_t nodeCount = 12;
    g_maxRange = 100.0;

    NodeContainer nodes;
    nodes.Create(nodeCount);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss(
        "ns3::RangePropagationLossModel", "MaxRange", DoubleValue(g_maxRange));

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

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    Vector positions[nodeCount] = {
        Vector(0, 0, 0), Vector(80, 0, 0), Vector(160, 0, 0),
        Vector(240, 0, 0), Vector(320, 0, 0), Vector(400, 0, 0),
        Vector(80, 50, 0), Vector(160, 50, 0), Vector(240, 50, 0),
        Vector(320, 50, 0), Vector(160, -50, 0), Vector(240, -50, 0)
    };

    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        nodes.Get(i)->GetObject<MobilityModel>()->SetPosition(positions[i]);
    }

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    g_nodes = nodes;
    g_devices = devices;
    g_interfaces = interfaces;

    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        Ptr<RouteSenseRoutingProtocol> routing =
            CreateObject<RouteSenseRoutingProtocol>();
        routing->SetNodeIndex(i);
        Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
        ipv4->SetRoutingProtocol(routing);
    }

    const uint16_t mainPort = 9;
    const uint16_t secondPort = 10;
    const uint16_t thirdPort = 11;

    PacketSinkHelper mainSink(
        "ns3::UdpSocketFactory",
        InetSocketAddress(Ipv4Address::GetAny(), mainPort));
    ApplicationContainer mainSinkApp = mainSink.Install(nodes.Get(5));
    mainSinkApp.Start(Seconds(0.0));
    mainSinkApp.Stop(Seconds(simulationTime));

    if (crossTraffic)
    {
        PacketSinkHelper sink2(
            "ns3::UdpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), secondPort));
        PacketSinkHelper sink3(
            "ns3::UdpSocketFactory",
            InetSocketAddress(Ipv4Address::GetAny(), thirdPort));

        ApplicationContainer sinkApp2 = sink2.Install(nodes.Get(4));
        ApplicationContainer sinkApp3 = sink3.Install(nodes.Get(1));
        sinkApp2.Start(Seconds(0.0));
        sinkApp2.Stop(Seconds(simulationTime));
        sinkApp3.Start(Seconds(0.0));
        sinkApp3.Stop(Seconds(simulationTime));
    }

    UdpClientHelper client1(interfaces.GetAddress(5), mainPort);
    client1.SetAttribute("MaxPackets", UintegerValue(1000000));
    client1.SetAttribute("PacketSize", UintegerValue(1024));
    client1.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
    ApplicationContainer app1 = client1.Install(nodes.Get(0));
    app1.Start(Seconds(2.0));
    app1.Stop(Seconds(simulationTime - 1.0));

    if (crossTraffic)
    {
        UdpClientHelper client2(interfaces.GetAddress(4), secondPort);
        client2.SetAttribute("MaxPackets", UintegerValue(1000000));
        client2.SetAttribute("PacketSize", UintegerValue(1024));
        client2.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
        ApplicationContainer app2 = client2.Install(nodes.Get(10));
        app2.Start(Seconds(2.0));
        app2.Stop(Seconds(simulationTime - 1.0));

        UdpClientHelper client3(interfaces.GetAddress(1), thirdPort);
        client3.SetAttribute("MaxPackets", UintegerValue(1000000));
        client3.SetAttribute("PacketSize", UintegerValue(1024));
        client3.SetAttribute("Interval", TimeValue(Seconds(packetInterval)));
        ApplicationContainer app3 = client3.Install(nodes.Get(11));
        app3.Start(Seconds(2.0));
        app3.Stop(Seconds(simulationTime - 1.0));
    }

    if (dynamicScenario)
    {
        Simulator::Schedule(
            Seconds(12.0), &MoveNode, nodes.Get(1), Vector(80, 160, 0));
        Simulator::Schedule(
            Seconds(22.0), &MoveNode, nodes.Get(1), Vector(80, 0, 0));
    }

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();

    phy.EnablePcap("results/routesense/routesense", devices, true);

    AsciiTraceHelper ascii;
    Ptr<OutputStreamWrapper> routeStream =
        ascii.CreateFileStream("results/routesense/routing-tables.txt");

    for (double t : {5.0, 13.0, 18.0, 23.0, 28.0})
    {
        if (t < simulationTime)
        {
            // NS-3.47 API: time, stream, time unit (no NodeContainer argument).
            Ipv4RoutingHelper::PrintRoutingTableAllAt(Seconds(t), routeStream);
        }
    }

    std::cout << "\n========== RouteSense ==========\n";
    std::cout << "Nodes: " << nodeCount << '\n';
    std::cout << "Routing: RouteSense weighted path selection\n";
    std::cout << "Main flow: Node 0 -> Node 5\n";
    std::cout << "Dynamic scenario: "
              << (dynamicScenario ? "Enabled" : "Disabled") << '\n';
    std::cout << "Cross traffic: "
              << (crossTraffic ? "Enabled" : "Disabled") << '\n';

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();
    monitor->CheckForLostPackets();

    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    auto stats = monitor->GetFlowStats();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n========== Application Metrics ==========\n";

    for (const auto& entry : stats)
    {
        const auto& flow = entry.second;
        auto tuple = classifier->FindFlow(entry.first);

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

        double pdr = 100.0 * flow.rxPackets / flow.txPackets;
        double lostPackets = flow.txPackets > flow.rxPackets
                                 ? flow.txPackets - flow.rxPackets
                                 : 0;
        double lossRatio = 100.0 * lostPackets / flow.txPackets;

        double delayMs = 0.0;
        if (flow.rxPackets > 0)
        {
            delayMs = 1000.0 * flow.delaySum.GetSeconds() / flow.rxPackets;
        }

        // Use the same application measurement window for both protocols:
        // traffic starts at 2 s and stops at simulationTime - 1 s.
        const double duration = simulationTime - 3.0;
        double throughputKbps = 0.0;
        if (flow.rxPackets > 0 && duration > 0.0)
        {
            throughputKbps = flow.rxBytes * 8.0 / duration / 1000.0;
        }

        std::cout << "\nFlow " << entry.first << ": "
                  << tuple.sourceAddress << " -> " << tuple.destinationAddress << '\n';
        std::cout << "  TX packets: " << flow.txPackets << '\n';
        std::cout << "  RX packets: " << flow.rxPackets << '\n';
        std::cout << "  PDR: " << pdr << "%\n";
        std::cout << "  Packet loss ratio: " << lossRatio << "%\n";
        std::cout << "  Throughput: " << throughputKbps << " Kbps\n";
        std::cout << "  Average delay: " << delayMs << " ms\n";
    }

    monitor->SerializeToXmlFile(
        "results/routesense/routesense-flowmon.xml", true, true);

    Simulator::Destroy();
    return 0;
}
