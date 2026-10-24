# RouteSense audit — 24 October 2026

## Project
scratch/routesense.cc implements custom IPv4 routing, topology, traffic and outputs.
scratch/routesense-aodv.cc is the independent conventional AODV baseline.
scripts/analyze_results.py extracts trial logs into CSV and averages.
scripts/generate_graphs.py plots static single-flow mean/std; inspected, unchanged, not rerun.
Scratch backup .cc files are separately discoverable build targets; .save is not.
results/baseline/routesense-aodv.cc is archived source, not built.
Results contain historical trial logs, routing snapshots, XML, PCAP, CSV and five PNG graphs. These are generated artifacts, not implementation dependencies. README.md describes upstream ns-3.
No AGENTS.md was found under the project parent. Old outputs were preserved; new evidence is in results/audit/.

## Changes
- Both simulations: --outputDir prevents forced reuse of old output paths; defaults remain compatible. Replaced shell mkdir with filesystem directory creation.
- Both: explicit --seed/--run, logged for reproducibility. Existing global ns-3 RNG options were already available.
- Both: reject non-finite timing values and intervals rounded to zero.
- RouteSense: respect requested output device, incoming forwarding state and down candidate interfaces; release IPv4 reference during disposal.
- RouteSense: compare/log complete paths, report transition to unreachable instead of silently clearing state.
- Analyzer: identify the main flow by endpoints instead of assuming Flow 1; require complete metrics and valid packet counts; classify scenario from log flags instead of filenames.
Files modified: scratch/routesense.cc, scratch/routesense-aodv.cc, scripts/analyze_results.py.
Scratch sources are ignored by this checkout's Git rules; scripts/results are untracked, so ordinary git diff does not capture these deliverables.

## Actual algorithm
For distance d <= 100 m, r=d/100:
stability=1-r, reliability=1-0.5*r*r, delayPenalty=r.
Edge cost = 0.35*r + 0.30*0.5*r*r + 0.20*r + 0.15.
Dijkstra minimizes summed edge cost; displayed score = 100/(1+cost).
Links beyond range and down interfaces are excluded. Positive costs give decreasing remaining distance along next hops in a fixed topology.
Routes are recomputed on each lookup/forwarding using global instantaneous positions. Mobility is observed at the next packet lookup; there is no periodic distributed update. Table snapshots recompute paths without updating change logs.
Stability/reliability/delay are distance estimates, not measured loss, jitter, delay, velocity or link lifetime.

## AODV and fairness
Verified actual ns-3 AodvHelper installed through InternetStackHelper::SetRoutingHelper.
Both programs match 12-node positions, range 100 m, RangePropagationLossModel, constant-speed delay, 802.11b ad hoc radios, data/control rates 11/1 Mbps, addressing, 1024-byte UDP, interval, start 2 s/stop T-1 s, optional 10->4 and 11->1 traffic and node-1 moves at 12/22 s.
AODV discovery, queueing, timers and control traffic are necessary protocol differences. RouteSense has free global knowledge, a major comparison limitation. Same seed/run ensures repeatability, not identical random draws across protocols.

## Verification
Environment: ns-3 3.47; existing cmake-cache; /usr/bin/c++; default profile.
Build command: ./ns3 build routesense routesense-aodv. Both compiled successfully.
Reproducible tests are in results/audit/run_smoke.py, verify.py and check_options.py.
- Eight smoke cases completed: both programs with static single/static cross at 6 s and dynamic single/dynamic cross at 25 s; seed 7/run 2. Mobility and cross flags checked independently.
- Initial static main TX/RX: RouteSense 6/6, AODV 6/4.
- Dynamic cross main TX/RX: RouteSense 44/44, AODV 44/43.
- RouteSense path changed from 0 1 2 3 4 5 to 0 6 7 8 9 5 at 12 s, restored at 22 s.
- Disconnected cross destination logged unavailable at 12 s and recovered at 22 s.
- Initial four cases generated XML, routing tables and 12 PCAP files each.
- Both static repeats yielded byte-identical XML.
- Both rejected zero interval, 1e-12 s interval, duration 4 s and dynamic duration 6 s.
- Analyzer historical-log run wrote an isolated CSV and skipped incomplete records. Synthetic regression passed for Flow 17, filename/flag disagreement and missing jitter.
- No large experiment batch or graph regeneration.

## Remaining issues and next steps
1. RouteSense dynamic cross flows TX/RX were 44/0 and 24/0. ARP debug tracing shows simultaneous reciprocal requests/retries by nodes 10/11 without replies. Half-duplex collision is consistent with this evidence, but full PHY drop diagnosis remains unverified. Do not infer protocol superiority from main-flow results.
2. FlowMonitor TX is IP-observed traffic, not all application send attempts. Unreachable RouteSense sends fail before monitoring (24 observed versus 44 scheduled). Validate offered-load accounting before comparing PDR.
3. Throughput uses received IP bytes over T-3 seconds, not payload goodput. Zero delay/jitter for no/one received packet is undefined-data presentation, not evidence of low latency.
4. TX-RX means undelivered by the end; XML lostPackets uses timeout/drop semantics. A one-second drain is not proven adequate for every workload.
5. Routing overhead, aggregate failures and initial-selection-versus-change counts are not implemented reliably. FlowMonitor does not comprehensively capture broadcast control overhead.
6. Static positive costs prevent shortest-path loops; mobility during transit can still allow transient revisits. No dedicated adversarial loop, interface-down or output-device runtime harness was run.
7. Historical artifacts were inventoried and representative logs/XML inspected, not every PCAP decoded or graph visually validated.
Next: validate application/IP accounting, undefined metrics, drain/loss semantics, ARP/PHY failures, seed/run provenance and paired confidence intervals before comparative graphs.
