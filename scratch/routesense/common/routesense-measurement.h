#ifndef ROUTESENSE_MEASUREMENT_H
#define ROUTESENSE_MEASUREMENT_H
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include <fstream>
#include <iomanip>
#include <set>
#include <map>
#include <cmath>
#include <sstream>
#include <limits>

namespace routesense
{
using namespace ns3;
inline double Missing() { return std::numeric_limits<double>::quiet_NaN(); }
inline std::string Number(double x)
{
    if (!std::isfinite(x)) { return "NA"; }
    std::ostringstream s; s << std::setprecision(12) << x; return s.str();
}
struct Flow
{
    Ipv4Address source, destination;
    uint16_t port, sourcePort{0};
    Ptr<UdpClient> client;
    uint64_t attempts{0}, accepted{0}, duplicates{0}, outOfOrder{0}, bytes{0};
    uint32_t high{0};
    std::set<uint32_t> sequences;
    std::set<uint64_t> acceptedUids, receivedUids;
    double delaySum{0}, jitterSum{0}, previousDelay{0};
    std::map<int, uint64_t> sendErrors;
    void Attempt(Ptr<const Packet> p)
    {
        ++attempts;
        Address local;
        client->GetSocket()->GetSockName(local);
        sourcePort = InetSocketAddress::ConvertFrom(local).GetPort();
        auto before = client->GetTotalTx();
        Simulator::ScheduleNow(&Flow::AfterSend, this, before, p->GetUid());
    }
    void AfterSend(uint64_t before, uint64_t uid)
    {
        if (client->GetTotalTx() > before) { ++accepted; acceptedUids.insert(uid); }
        else { ++sendErrors[static_cast<int>(client->GetSocket()->GetErrno())]; }
    }
    void Receive(Ptr<const Packet> packet, const Address& from)
    {
        if (!InetSocketAddress::IsMatchingType(from) ||
            InetSocketAddress::ConvertFrom(from).GetIpv4() != source) { return; }
        auto copy = packet->Copy();
        SeqTsHeader header;
        NS_ABORT_MSG_IF(copy->RemoveHeader(header) != header.GetSerializedSize(), "Invalid sequence header");
        uint32_t seq = header.GetSeq();
        if (!sequences.insert(seq).second) { ++duplicates; return; }
        if (sequences.size() > 1 && seq < high) { ++outOfOrder; }
        high = std::max(high, seq);
        double delay = (Simulator::Now() - header.GetTs()).GetSeconds();
        if (sequences.size() > 1) { jitterSum += std::abs(delay - previousDelay); }
        previousDelay = delay;
        delaySum += delay;
        receivedUids.insert(packet->GetUid());
        bytes += packet->GetSize(); // UDP payload, including 12-byte SeqTsHeader.
    }
};
class Measurement
{
  public:
    std::map<uint16_t, Flow> flows;
    std::ofstream events;
    std::set<uint64_t> terminalDrops;
    void Add(uint32_t source, uint32_t destination, uint16_t port,
             const NodeContainer& nodes, const Ipv4InterfaceContainer& addresses)
    {
        auto& f = flows[port];
        f.source = addresses.GetAddress(source);
        f.destination = addresses.GetAddress(destination);
        f.port = port;
        for (uint32_t i=0; i<nodes.Get(source)->GetNApplications(); ++i)
        {
            auto c = DynamicCast<UdpClient>(nodes.Get(source)->GetApplication(i));
            if (c) { AddressValue remote; c->GetAttribute("Remote", remote); if (InetSocketAddress::ConvertFrom(remote.Get()).GetPort() == port) { f.client = c; } }
        }
        NS_ABORT_MSG_IF(!f.client, "Missing UDP client");
        NS_ABORT_MSG_IF(!f.client->TraceConnectWithoutContext("Tx", MakeCallback(&Flow::Attempt, &f)), "Missing Tx trace");
        for (uint32_t i=0; i<nodes.Get(destination)->GetNApplications(); ++i)
        {
            auto sink = DynamicCast<PacketSink>(nodes.Get(destination)->GetApplication(i));
            AddressValue local; if (sink) { sink->GetAttribute("Local", local); }
            if (sink && InetSocketAddress::ConvertFrom(local.Get()).GetPort() == port)
            {
                NS_ABORT_MSG_IF(!sink->TraceConnectWithoutContext("Rx", MakeCallback(&Flow::Receive, &f)), "Missing Rx trace");
            }
        }
    }
    void Event(uint32_t node, const std::string& kind, Ptr<const Packet> p, const std::string& reason)
    {
        if (!events.is_open()) { return; }
        events << std::setprecision(12) << Simulator::Now().GetSeconds() << ','
               << node << ',' << kind << ',' << p->GetUid() << ',' << p->GetSize() << ',' << reason << '\n';
    }
    static void RxDrop(Measurement* m, uint32_t node, Ptr<const Packet> p, WifiPhyRxfailureReason r)
    {
        std::ostringstream s; s << r; m->Event(node,"phy_rx_drop",p,s.str());
    }
    static void Tx(Measurement* m, uint32_t node, Ptr<const Packet> p, double)
    { m->Event(node,"phy_tx",p,""); }
    static void TxDrop(Measurement* m, uint32_t node, Ptr<const Packet> p)
    { m->Event(node,"phy_tx_drop",p,""); }
    static void MacDrop(Measurement* m, uint32_t node, WifiMacDropReason r, Ptr<const WifiMpdu> p)
    { m->terminalDrops.insert(p->GetPacket()->GetUid()); m->Event(node,"mac_mpdu_drop",p->GetPacket(),std::to_string(static_cast<int>(r))); }
    static void ArpDrop(Measurement* m, uint32_t node, Ptr<const Packet> p)
    { m->terminalDrops.insert(p->GetUid()); m->Event(node,"arp_drop",p,""); }
    static void IpDrop(Measurement* m, uint32_t node, const Ipv4Header&, Ptr<const Packet> p,
                       Ipv4L3Protocol::DropReason r, Ptr<Ipv4>, uint32_t)
    { m->terminalDrops.insert(p->GetUid()); m->Event(node,"ip_drop",p,std::to_string(static_cast<int>(r))); }
    void Diagnose(const NodeContainer& nodes, const NetDeviceContainer& devices, const std::string& dir, bool verbose)
    {
        if (verbose) { events.open(dir + "/events.csv");
        events << "time,node,event,uid,bytes,reason\n"; }
        for (uint32_t i=0; i<nodes.GetN(); ++i)
        {
            auto wifi = DynamicCast<WifiNetDevice>(devices.Get(i));
            auto phy = wifi->GetPhy();
            if (verbose) {
            NS_ABORT_MSG_IF(!phy->TraceConnectWithoutContext("PhyRxDrop", MakeBoundCallback(&RxDrop,this,i)), "Missing PHY trace");
            NS_ABORT_MSG_IF(!phy->TraceConnectWithoutContext("PhyTxBegin", MakeBoundCallback(&Tx,this,i)), "Missing PHY Tx trace");
            NS_ABORT_MSG_IF(!phy->TraceConnectWithoutContext("PhyTxDrop", MakeBoundCallback(&TxDrop,this,i)), "Missing PHY drop trace");
            }
            NS_ABORT_MSG_IF(!wifi->GetMac()->TraceConnectWithoutContext("DroppedMpdu", MakeBoundCallback(&MacDrop,this,i)), "Missing MAC trace");
            auto arp = nodes.Get(i)->GetObject<ArpL3Protocol>();
            NS_ABORT_MSG_IF(!arp->TraceConnectWithoutContext("Drop", MakeBoundCallback(&ArpDrop,this,i)), "Missing ARP trace");
            auto ip = nodes.Get(i)->GetObject<Ipv4L3Protocol>();
            NS_ABORT_MSG_IF(!ip->TraceConnectWithoutContext("Drop", MakeBoundCallback(&IpDrop,this,i)), "Missing IP trace");
            int32_t interface = ip->GetInterfaceForDevice(devices.Get(i));
            auto cache = ip->GetInterface(interface)->GetArpCache();
            NS_ABORT_MSG_IF(!cache->TraceConnectWithoutContext("Drop", MakeBoundCallback(&ArpDrop,this,i)), "Missing ARP cache trace");
        }
    }
    void Write(const std::string& dir, const std::string& protocol, uint32_t seed, uint64_t run,
               double stop, double drain, double interval, bool dynamic, bool cross,
               Ptr<FlowMonitor> monitor, Ptr<Ipv4FlowClassifier> classifier)
    {
        std::ofstream meta(dir + "/provenance.csv");
        meta << "schema,protocol,seed,run,app_start,app_stop,simulation_stop,drain,interval,dynamic,cross,packet_bytes\n"
             << "2," << protocol << ',' << seed << ',' << run << ",2," << stop << ','
             << stop+drain << ',' << drain << ',' << interval << ',' << dynamic << ',' << cross << ",1024\n";
        std::ofstream out(dir + "/metrics.csv");
        out << "source,source_port,destination,port,attempts,accepted,send_failures,unique_rx,duplicates,out_of_order,"
            << "app_unreceived,app_drop_observed_unreceived,app_unresolved,app_pdr_pct,app_udp_goodput_kbps,app_delay_ms,app_jitter_ms,"
            << "ip_tx,ip_rx,ip_unreceived,ip_pdr_pct,ip_throughput_kbps,ip_delay_ms,ip_jitter_ms,"
            << "ip_reported_drop,ip_timeout_inferred,ip_unresolved\n";
        auto stats = monitor->GetFlowStats();
        for (auto& [port,f] : flows)
        {
            uint64_t tx=0,rx=0,bytes=0,lost=0,drops=0;
            double delay=0,jitter=0;
            for (const auto& [id,s] : stats)
            {
                auto t=classifier->FindFlow(id);
                if (t.protocol != 17 || t.sourceAddress != f.source || t.destinationAddress != f.destination || t.destinationPort != port) { continue; }
                tx+=s.txPackets; rx+=s.rxPackets; bytes+=s.rxBytes; lost+=s.lostPackets;
                for (auto n : s.packetsDropped) { drops+=n; }
                delay+=s.delaySum.GetSeconds(); jitter+=s.jitterSum.GetSeconds();
            }
            uint64_t n=f.sequences.size();
            NS_ABORT_MSG_IF(n>f.accepted || f.accepted>f.attempts || rx>tx || lost>tx-rx || drops>lost,
                            "Inconsistent accounting");
            // Observation window [2, appStop+drain); includes all counted receptions.
            double duration=stop+drain-2;
            uint64_t confirmed=0;
            for (auto uid : f.acceptedUids)
            {
                if (terminalDrops.count(uid) && !f.receivedUids.count(uid)) { ++confirmed; }
            }
            NS_ABORT_MSG_IF(confirmed > f.accepted-n, "Invalid terminal drop count");
            out << f.source << ',' << f.sourcePort << ',' << f.destination << ',' << port << ',' << f.attempts << ',' << f.accepted
                << ',' << f.attempts-f.accepted << ',' << n << ',' << f.duplicates << ',' << f.outOfOrder
                << ',' << f.accepted-n << ',' << confirmed << ',' << f.accepted-n-confirmed << ',' << Number(f.accepted?100.0*n/f.accepted:Missing())
                << ',' << Number(f.bytes*8.0/duration/1000)
                << ',' << Number(n?f.delaySum*1000/n:Missing())
                << ',' << Number(n>1?f.jitterSum*1000/(n-1):Missing())
                << ',' << tx << ',' << rx << ',' << tx-rx
                << ',' << Number(tx?100.0*rx/tx:Missing())
                << ',' << Number(bytes*8.0/duration/1000)
                << ',' << Number(rx?delay*1000/rx:Missing())
                << ',' << Number(rx>1?jitter*1000/(rx-1):Missing())
                << ',' << drops << ',' << lost-drops << ',' << tx-rx-lost << '\n';
            std::cout << "Measured flow " << f.source << " -> " << f.destination << ':' << port
                      << " attempts=" << f.attempts << " accepted=" << f.accepted << " uniqueRx=" << n;
            for (auto [error,count] : f.sendErrors) { std::cout << " errno[" << error << "]=" << count; }
            std::cout << '\n';
        }
    }
};
}
#endif
