// BEGIN optional NetAnim
#include "../common/routesense-animation.h"
// END optional NetAnim
#include "../common/routesense-measurement.h"

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

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <vector>

using namespace ns3;

// Shared simulation topology used by the experimental routing algorithm.
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

    void SetNodeIndex(uint32_t index)
    {
        m_nodeIndex = index;
    }

    void SetIpv4(Ptr<Ipv4> ipv4) override
    {
        m_ipv4 = ipv4;
    }

    Ptr<Ipv4Route> RouteOutput(
        Ptr<Packet> packet,
        const Ipv4Header& header,
        Ptr<NetDevice> outputDevice,
        Socket::SocketErrno& sockerr) override
    {
        (void)packet;


        Ptr<Ipv4Route> route = BuildRoute(header.GetDestination());

        if (route && outputDevice && route->GetOutputDevice() != outputDevice)
        {
            route = nullptr;
        }
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
        int32_t inputInterface =
            m_ipv4->GetInterfaceForDevice(inputDevice);

        uint32_t iif =
            inputInterface >= 0
                ? static_cast<uint32_t>(inputInterface)
                : 0;

        // Deliver packets addressed to this node.
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

        if (inputInterface < 0 || !m_ipv4->IsForwarding(iif))
        {
            error(packet, header, Socket::ERROR_NOROUTETOHOST);
            return true;
        }

        // Multicast forwarding is outside this prototype's scope.
        (void)multicastForward;
        if (header.GetDestination().IsMulticast())
        {
            return false;
        }

        Ptr<Ipv4Route> route =
            BuildRoute(header.GetDestination());

        if (route)
        {
            unicastForward(route, packet, header);
            return true;
        }

        error(packet, header, Socket::ERROR_NOROUTETOHOST);
        return true;
    }

    void NotifyInterfaceUp(uint32_t interface) override
    {
        (void)interface;
    }

    void NotifyInterfaceDown(uint32_t interface) override
    {
        (void)interface;
    }

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

        *out << "\nRouteSense table for node "
             << m_nodeIndex << " at "
             << Simulator::Now().As(unit) << '\n';

        *out << "Destination\tNextHop\tScore\n";

        for (uint32_t destination = 0;
             destination < g_nodes.GetN();
             ++destination)
        {
            if (destination == m_nodeIndex)
            {
                continue;
            }

            std::vector<uint32_t> path;
            double cost = 0.0;

            if (FindBestPath(
                    m_nodeIndex, destination, path, cost) &&
                path.size() >= 2)
            {
                *out << g_interfaces.GetAddress(destination)
                     << '\t'
                     << g_interfaces.GetAddress(path[1])
                     << '\t'
                     << 100.0 / (1.0 + cost)
                     << '\n';
            }
        }
    }

  protected:
    void DoDispose() override
    {
        m_ipv4 = nullptr;
        Ipv4RoutingProtocol::DoDispose();
    }

  private:
    uint32_t m_nodeIndex{0};
    Ptr<Ipv4> m_ipv4;

    // Remember the selected next hop for each destination.
    std::map<uint32_t, std::vector<uint32_t>> m_lastNextHop;

    double LinkCost(uint32_t a, uint32_t b) const
    {
        for (uint32_t index : {a, b})
        {
            auto ipv4 = g_nodes.Get(index)->GetObject<Ipv4>();
            int32_t interface = ipv4->GetInterfaceForDevice(g_devices.Get(index));
            if (interface < 0 || !ipv4->IsUp(interface))
            {
                return std::numeric_limits<double>::infinity();
            }
        }
        Vector pa =
            g_nodes.Get(a)->GetObject<MobilityModel>()->GetPosition();

        Vector pb =
            g_nodes.Get(b)->GetObject<MobilityModel>()->GetPosition();

        double dx = pa.x - pb.x;
        double dy = pa.y - pb.y;
        double dz = pa.z - pb.z;
        double distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (distance > g_maxRange)
        {
            return std::numeric_limits<double>::infinity();
        }

        double ratio = std::min(1.0, distance / g_maxRange);

        // Distance-based estimates, not measured packet statistics.
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

        if (!FindBestPath(
                m_nodeIndex, destinationIndex, path, totalCost))
        {
            if (m_lastNextHop.erase(destinationIndex))
            {
                std::cout << "[RouteSense] t=" << Simulator::Now().GetSeconds()
                          << "s node=" << m_nodeIndex << " destination=" << destination
                          << " route unavailable" << std::endl;
            }
            return nullptr;
        }

        uint32_t nextHop = path[1];

        auto previous = m_lastNextHop.find(destinationIndex);

        if (previous == m_lastNextHop.end() ||
            previous->second != path)
        {
            std::cout << std::fixed << std::setprecision(2)
                      << "[RouteSense] t="
                      << Simulator::Now().GetSeconds()
                      << "s node=" << m_nodeIndex
                      << " destination=" << destination
                      << " nextHop=" << nextHop
                      << " score=" << 100.0 / (1.0 + totalCost)
                      << '\n';

            std::cout << "  path=";
            for (auto node : path) { std::cout << node << ' '; }
            std::cout << std::endl;
            m_lastNextHop[destinationIndex] = path;
        }

        int32_t outputInterface =
            m_ipv4->GetInterfaceForDevice(g_devices.Get(m_nodeIndex));

        if (outputInterface < 0 || !m_ipv4->IsUp(outputInterface))
        {
            return nullptr;
        }

        Ptr<Ipv4Route> route = Create<Ipv4Route>();

        route->SetDestination(destination);
        route->SetGateway(g_interfaces.GetAddress(nextHop));
        route->SetOutputDevice(g_devices.Get(m_nodeIndex));
        route->SetSource(
            m_ipv4->GetAddress(outputInterface, 0).GetLocal());

        return route;
    }
};

NS_OBJECT_ENSURE_REGISTERED(RouteSenseRoutingProtocol);

static void
MoveNode(Ptr<Node> node, Vector position)
{
    node->GetObject<MobilityModel>()->SetPosition(position);

    std::cout << "Topology event at "
              << Simulator::Now().GetSeconds()
              << " s: Node " << node->GetId()
              << " moved to (" << position.x
              << ", " << position.y << ")\n";
}

int
main(int argc, char* argv[])
{
    // BEGIN optional NetAnim
    bool enableAnimation = false;
    std::string animationFile = "results/netanim/routesense.xml";
    // END optional NetAnim
    double drainTime = 15.0;
    bool diagnostics = false;
    double simulationTime = 30.0;
    double packetInterval = 0.5;
    std::string outputDir = "results/generated/routesense-new";
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
    cmd.AddValue("dynamicScenario", "Enable node movement",
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

    if (!std::isfinite(drainTime) || drainTime <= 0 || !std::isfinite(simulationTime) || !std::isfinite(packetInterval) || simulationTime <= 4.0 || packetInterval <= 0.0 || Seconds(packetInterval).IsZero() ||
        (dynamicScenario && simulationTime <= 23.0))
    {
        std::cerr << "Invalid simulation parameters.\n";
        return 1;
    }

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

    const double appStop = simulationTime - 1.0;
    const double observationEnd = appStop + drainTime;
    const uint32_t nodeCount = 12;
    g_maxRange = 100.0;

    NodeContainer nodes;
    nodes.Create(nodeCount);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay(
        "ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss(
        "ns3::RangePropagationLossModel",
        "MaxRange", DoubleValue(g_maxRange));

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

    // Install the IPv4 stack, then replace its routing protocol.
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

    ApplicationContainer mainSinkApp =
        mainSink.Install(nodes.Get(5));

    mainSinkApp.Start(Seconds(0.0));
    mainSinkApp.Stop(Seconds(observationEnd));

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
        sinkApp2.Stop(Seconds(observationEnd));
        sinkApp3.Start(Seconds(0.0));
        sinkApp3.Stop(Seconds(observationEnd));
    }

    UdpClientHelper client1(interfaces.GetAddress(5), mainPort);
    client1.SetAttribute("MaxPackets", UintegerValue(1000000));
    client1.SetAttribute("PacketSize", UintegerValue(1024));
    client1.SetAttribute(
        "Interval", TimeValue(Seconds(packetInterval)));

    ApplicationContainer app1 = client1.Install(nodes.Get(0));
    app1.Start(Seconds(2.0));
    app1.Stop(Seconds(simulationTime - 1.0));

    if (crossTraffic)
    {
        UdpClientHelper client2(interfaces.GetAddress(4), secondPort);
        client2.SetAttribute("MaxPackets", UintegerValue(1000000));
        client2.SetAttribute("PacketSize", UintegerValue(1024));
        client2.SetAttribute(
            "Interval", TimeValue(Seconds(packetInterval)));

        ApplicationContainer app2 = client2.Install(nodes.Get(10));
        app2.Start(Seconds(2.0));
        app2.Stop(Seconds(simulationTime - 1.0));

        UdpClientHelper client3(interfaces.GetAddress(1), thirdPort);
        client3.SetAttribute("MaxPackets", UintegerValue(1000000));
        client3.SetAttribute("PacketSize", UintegerValue(1024));
        client3.SetAttribute(
            "Interval", TimeValue(Seconds(packetInterval)));

        ApplicationContainer app3 = client3.Install(nodes.Get(11));
        app3.Start(Seconds(2.0));
        app3.Stop(Seconds(simulationTime - 1.0));
    }

    if (dynamicScenario)
    {
        Simulator::Schedule(
            Seconds(12.0), &MoveNode,
            nodes.Get(1), Vector(80, 160, 0));

        Simulator::Schedule(
            Seconds(22.0), &MoveNode,
            nodes.Get(1), Vector(80, 0, 0));
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
        outputDir + "/routesense", devices, true);

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

    std::cout << "\n========== RouteSense ==========\n";
    std::cout << "Seed: " << seed << " Run: " << run << std::endl;
    std::cout << "Nodes: " << nodeCount << '\n';
    std::cout << "Routing: RouteSense weighted path selection\n";
    std::cout << "Main flow: Node 0 -> Node 5\n";
    std::cout << "Dynamic scenario: "
              << (dynamicScenario ? "Enabled" : "Disabled") << '\n';
    std::cout << "Cross traffic: "
              << (crossTraffic ? "Enabled" : "Disabled") << '\n';

    // BEGIN optional NetAnim
    std::unique_ptr<AnimationInterface> animation;
    if (!routesense::ConfigureAnimation(enableAnimation, animationFile, outputDir,
                                        "routesense-flowmon.xml", nodes, interfaces,
                                        crossTraffic, dynamicScenario, observationEnd, animation))
    {
        Simulator::Destroy();
        return 1;
    }
    // END optional NetAnim
    Simulator::Stop(Seconds(observationEnd));
    Simulator::Run();

    monitor->CheckForLostPackets();

    measurement.Write(outputDir, "routesense", RngSeedManager::GetSeed(), RngSeedManager::GetRun(),
                      appStop, drainTime, packetInterval, dynamicScenario, crossTraffic,
                      monitor, DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier()));

    monitor->SerializeToXmlFile(
        outputDir + "/routesense-flowmon.xml",
        true, true);

    Simulator::Destroy();
    return 0;
}
