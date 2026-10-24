# RouteSense NetAnim

NetAnim support is integrated into the maintained simulations:
- scratch/routesense/custom/routesense.cc
- scratch/routesense/baseline/routesense-aodv.cc
- shared setup: scratch/routesense/common/routesense-animation.h

The older scratch/routesense.cc and scratch/routesense-aodv.cc paths were relocated during project organization. Program names remain routesense and routesense-aodv.

## Build and generate

The installed ns-3.47 netanim module and shared library were inspected and are already built. Both targets already link the configured ns-3 modules, so no module reconfiguration was necessary.

Run inside Ubuntu/WSL from /home/vijayakumar/network-project/ns-3:

    ./ns3 build routesense routesense-aodv
    ./ns3 run "routesense --simulationTime=25 --dynamicScenario=1 --crossTraffic=1 --seed=7 --run=2 --enableAnimation=1 --animationFile=results/netanim/routesense-example.xml --outputDir=results/generated/routesense-animation-example"
    ./ns3 run "routesense-aodv --simulationTime=25 --dynamicScenario=1 --crossTraffic=1 --seed=7 --run=2 --enableAnimation=1 --animationFile=results/netanim/aodv-example.xml --outputDir=results/generated/aodv-animation-example"

Choose new paths when rerunning. Animation is disabled by default. Default enabled filenames are results/netanim/routesense.xml and results/netanim/routesense-aodv.xml. --animationFile is independently configurable; --outputDir continues to control all original simulation outputs. Existing XML is rejected, directories are created with error checks, exclusive file reservation protects against replacement, and rollover is disabled to avoid hidden numbered-file overwrites. FlowMonitor output cannot be used as the animation path.

Disabled mode does not create/inspect animation paths or enable packet metadata/counter polling. Original metrics, provenance and FlowMonitor formats remain unchanged.

## Viewing the generated smoke traces

The standalone GUI is not installed in this environment. The simulation-side module is available; these are separate components. GUI playback and label overlap at every zoom level could not be visually verified.

With the standalone GUI on PATH, launch:

    netanim

Then use the top-left File Open button and select one of these existing files:

    /home/vijayakumar/network-project/ns-3/results/netanim/routesense-static.xml
    /home/vijayakumar/network-project/ns-3/results/netanim/routesense-dynamic-cross.xml
    /home/vijayakumar/network-project/ns-3/results/netanim/routesense-aodv-static.xml
    /home/vijayakumar/network-project/ns-3/results/netanim/routesense-aodv-dynamic-cross.xml

Click Play. Use the timeline near 12 s and 22 s to inspect node 1 moving away and returning. Enable packet display, node descriptions/IDs and wireless-circle display in the GUI as needed. Use native IP/address and packet metadata views to inspect full addresses and UDP/control headers. Node counters provide native Wi-Fi PHY/MAC and IPv4 counts; these do not replace application metrics.

If the GUI must be built, the vendored ns-3.47 instructions in src/netanim/doc/animation.rst specify Qt5 and CMake, then:

    git clone https://gitlab.com/nsnam/netanim.git /tmp/routesense-netanim-gui
    cmake -S /tmp/routesense-netanim-gui -B /tmp/routesense-netanim-gui/build
    cmake --build /tmp/routesense-netanim-gui/build
    /tmp/routesense-netanim-gui/build/bin/netanim

Those optional GUI build commands were not executed; Qt5/GUI build tools were not found. Launch requires a graphical desktop/WSLg session. No unverified command-line XML-loading switch is assumed; use File Open.

## Display and event semantics

All twelve initial positions and actual CourseChange updates are recorded. No positions, routes, links, packet motion or traffic times are invented.
- Blue/larger: N0 main-TX; green/larger: N5 main-RX.
- Orange: N10 X1-TX and N11 X2-TX when cross traffic is enabled.
- Purple: N4 X1-RX and N1 X2-RX; a star marks moving N1.
- Gray: other relays.
Labels are compact to reduce overlap; full addresses are stored in native IP entries rather than long per-node labels. GUI zoom/font settings can still affect readability. Node sizes are display-only.

Wireless links are transient, trace-derived packet activity, not fabricated permanent edges. AnimationInterface writes PHY transmit references and PHY receive-start records; a wireless reception marker is not proof of successful decoding or application delivery. Broadcasts, retries, ARP, AODV control and data can all appear. Packet metadata and native drop counters help inspection, but use existing application/IP metrics for delivery conclusions. Existing ARP/PHY failures remain visible and unchanged.

## Verification

Both targets compiled. Four enabled smoke tests covered static single-flow and dynamic cross traffic for both protocols. .NET XML parsing verified:
- all 12 nodes, descriptions/colors/sizes and native IPv4 addresses;
- actual initial source/destination positions;
- exact node-1 updates at 12 and 22 s;
- wireless transmit/reception records and packet metadata;
- main/cross-flow roles.

| Program | Scenario | TX references | RX-start records |
|---|---|---:|---:|
| RouteSense | static | 75 | 390 |
| RouteSense | dynamic cross | 481 | 2180 |
| AODV | static | 373 | 1736 |
| AODV | dynamic cross | 1958 | 8427 |

Every TX reference includes metadata. These counts include control traffic/PHY observations, not application packet totals.

Enabled and disabled runs produced byte-identical metrics.csv, provenance.csv and FlowMonitor XML to the preserved pre-animation references. Both programs passed existing-animation overwrite rejection. Ten analysis regression tests and preservation checks passed: 1,223 evidence files unchanged, 24 authoritative trials / 48 records valid. No historical outputs were replaced.

XML validation used PowerShell/.NET, not Python event analysis. Smoke evidence: results/verification/netanim/. To repeat from Windows PowerShell with fresh destinations:

    pwsh -NoProfile -ExecutionPolicy Bypass -File ./scripts/tests/test_netanim.ps1 -Output results/verification/netanim-repeat -AnimationDirectory results/netanim/repeat

ExecutionPolicy Bypass applies only to that process; no global policy change is made. The test defaults to Ubuntu-24.04 and the current Linux project path; parameters allow changing them. Large experiments, graph regeneration, GUI installation and GUI playback were not performed.

The evaluation still has three run pairs at one seed, exploratory confidence intervals, conditional latency, global topology knowledge for RouteSense and unmeasured routing overhead. Animation does not strengthen statistical claims or fix radio failures.
