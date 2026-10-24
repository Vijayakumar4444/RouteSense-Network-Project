#ifndef ROUTESENSE_ANIMATION_H
#define ROUTESENSE_ANIMATION_H

#include "ns3/netanim-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>

namespace routesense
{
// Optional observer only: never change mobility, routing, traffic, or RNG streams.
inline bool ConfigureAnimation(
    bool enabled,
    const std::string& filename,
    const std::string& outputDir,
    const std::string& flowMonitorFilename,
    const ns3::NodeContainer& nodes,
    const ns3::Ipv4InterfaceContainer& interfaces,
    bool crossTraffic,
    bool dynamicScenario,
    double observationEnd,
    std::unique_ptr<ns3::AnimationInterface>& animation)
{
    if (!enabled)
    {
        return true; // Do not touch the animation path or enable packet metadata.
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path path(filename);
    if (path.empty() || path.extension() != ".xml")
    {
        std::cerr << "animationFile must name an .xml file.\n";
        return false;
    }
    auto absolute = fs::absolute(path, ec).lexically_normal();
    if (ec)
    {
        std::cerr << "Invalid animation path: " << ec.message() << '\n';
        return false;
    }
    auto monitorPath = fs::absolute(fs::path(outputDir) / flowMonitorFilename, ec).lexically_normal();
    if (ec || absolute == monitorPath)
    {
        std::cerr << "Animation output must differ from FlowMonitor output.\n";
        return false;
    }
    if (fs::exists(path, ec) || ec)
    {
        std::cerr << "Animation output already exists or cannot be inspected; choose a fresh --animationFile.\n";
        return false;
    }
    fs::create_directories(absolute.parent_path(), ec);
    if (ec)
    {
        std::cerr << "Could not create animation directory: " << ec.message() << '\n';
        return false;
    }
    // Exclusive reservation prevents accidental replacement (C++23, used by ns-3.47).
    std::ofstream reservation(absolute, std::ios::out | std::ios::noreplace);
    if (!reservation)
    {
        std::cerr << "Cannot reserve animation output without overwriting: " << absolute << '\n';
        return false;
    }
    reservation.close();

    animation = std::make_unique<ns3::AnimationInterface>(absolute.string());
    animation->EnablePacketMetadata(true);
    animation->SetStartTime(ns3::Seconds(0));
    animation->SetStopTime(ns3::Seconds(observationEnd));
    // One file: avoid NetAnim's implicit rollover filenames overwriting old files.
    animation->SetMaxPktsPerTraceFile(std::numeric_limits<uint64_t>::max());
    animation->EnableWifiPhyCounters(ns3::Seconds(0), ns3::Seconds(observationEnd));
    animation->EnableWifiMacCounters(ns3::Seconds(0), ns3::Seconds(observationEnd));
    animation->EnableIpv4L3ProtocolCounters(ns3::Seconds(0), ns3::Seconds(observationEnd));
    // CourseChange records the real 12/22 s moves immediately; retain default polling.
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
    {
        std::string role = "relay";
        uint8_t r = 120, g = 135, b = 150;
        double size = 6;
        if (i == 0)
        {
            role = "main-TX"; r = 35; g = 110; b = 220; size = 9;
        }
        else if (i == 5)
        {
            role = "main-RX"; r = 25; g = 160; b = 80; size = 9;
        }
        else if (crossTraffic && (i == 10 || i == 11))
        {
            role = i == 10 ? "X1-TX" : "X2-TX";
            r = 230; g = 125; b = 25; size = 8;
        }
        else if (crossTraffic && (i == 4 || i == 1))
        {
            role = i == 4 ? "X1-RX" : "X2-RX";
            r = 145; g = 65; b = 190; size = 8;
        }
        if (dynamicScenario && i == 1)
        {
            role += "*"; // Actual moving node; no synthetic motion.
        }
        // Compact labels avoid long address text; native <ip> entries store full IPs.
        animation->UpdateNodeDescription(nodes.Get(i), "N" + std::to_string(i) + " " + role);
        animation->UpdateNodeColor(nodes.Get(i), r, g, b);
        animation->UpdateNodeSize(nodes.Get(i), size, size);
    }
    std::cout << "NetAnim XML: " << absolute.string() << '\n'
              << "NetAnim IP map: ";
    for (uint32_t i = 0; i < interfaces.GetN(); ++i)
    {
        std::cout << "N" << i << '=' << interfaces.GetAddress(i) << ' ';
    }
    std::cout << "\nNetAnim roles: main 0->5; ";
    if (crossTraffic)
    {
        std::cout << "X1 10->4; X2 11->1; ";
    }
    std::cout << "* marks moving node 1 when enabled.\n";
    return true;
}
} // namespace routesense
#endif

