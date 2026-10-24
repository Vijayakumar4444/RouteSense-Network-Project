# RouteSense — Stability-Aware Adaptive Routing

RouteSense is an ns-3.47 experiment comparing a custom IPv4 routing prototype with ns-3's conventional AODV. Run commands below from this repository root inside Ubuntu/WSL. The existing ns-3 build is used.

## Layout

    scratch/routesense/
      custom/routesense.cc             Custom routing and simulation
      baseline/routesense-aodv.cc      Independent AODV simulation
      common/routesense-measurement.h  Shared measurement instrumentation
      CMakeLists.txt                   Preserves both program names
    scripts/
      analysis/       Validation, metric extraction, evidence path resolution
      comparison/     Trial runner and authoritative-only reanalysis
      tracing/        ARP/PHY diagnosis and diagnostic repeats
      tests/          Regression, integrity and simulation checks
      visualization/  Legacy graph generator; not authoritative
    results/
      authoritative/comparison/       Frozen report, records, summaries, intervals
      authoritative/audit-evidence/   Original audit evidence snapshots
      generated/                      Raw XML, PCAP, metrics, provenance, run logs
      graphs/                         Existing historical graphs, unchanged
      verification/                   New lightweight verification outputs
    archive/
      source/         Old C++ backups and saved source
      logs/trials/    Historical trial text logs
      analysis/       Superseded CSVs and analysis directories
      maintenance/    Organization scripts retained as evidence
    docs/
      audits/         Earlier audit/measurement reports (historical paths)
      organization/   Path map, hashes and final verification report
      upstream/       Preserved original ns-3 README

Upstream ns-3 source, examples, build machinery, licensing and technical documentation remain in place. The original README is preserved at [docs/upstream/README-ns3.md](docs/upstream/README-ns3.md); its original relative links refer to the repository root. The empty src/routesense placeholder is not an installed ns-3 module.

## Architecture and actual algorithm

Both programs create 12 wireless ad hoc nodes. Main traffic is node 0→5 (UDP port 9); optional cross flows are 10→4 (port 10) and 11→1 (port 11). Both use the same positions, 100 m RangePropagationLossModel, constant-speed propagation delay, 802.11b, 11 Mbps data / 1 Mbps control, and 1024-byte UDP payloads at a default interval of 0.5 s.

For a candidate link with distance d≤100 m, let r=d/100. RouteSense estimates stability=1-r, reliability=1-0.5r² and delay penalty=r. Its edge cost is:

    0.35r + 0.30(0.5r²) + 0.20r + 0.15

Dijkstra minimizes summed cost; the displayed score is 100/(1+cost). Out-of-range/down-interface links are excluded. Routes are recomputed on packet lookup/forwarding using global instantaneous positions. Full-path transitions and unavailable routes are logged. These are distance estimates, not measured loss, delay, jitter, velocity or link lifetime. There is no distributed RouteSense discovery/control exchange.

AODV uses ns-3 AodvHelper through InternetStackHelper; discovery, timers, buffering and control traffic are genuine protocol behavior.

## Build and run

    ./ns3 build routesense routesense-aodv
    ./ns3 run "routesense --simulationTime=25 --dynamicScenario=1 --crossTraffic=1 --seed=7 --run=2 --drainTime=15 --outputDir=results/generated/custom-example"
    ./ns3 run "routesense-aodv --simulationTime=25 --dynamicScenario=1 --crossTraffic=1 --seed=7 --run=2 --drainTime=15 --outputDir=results/generated/aodv-example"

Output directories must be empty/new. Add --diagnostics=1 to emit PHY/MAC/ARP/IP events.csv; PCAP, routing tables, metrics.csv, provenance.csv and FlowMonitor XML are produced. Seed and run are explicit; equal values do not mean identical random draws across protocols.

Sources start at 2 s and stop at simulationTime−1 s. Sinks/monitor stop drainTime seconds later (default 15 s). Thus simulationTime=25 means app stop 24 s and actual simulation stop 39 s. Dynamic movement takes node 1 away at 12 s and restores it at 22 s; dynamic duration must span both events. Static examples can use simulationTime=6. The 15-second drain converged with 30 seconds for tested workloads; it is not guaranteed for arbitrary load.

## Measurements

- Attempts count application send calls; accepted counts successful socket acceptance. Failed sends are separate.
- Application PDR = unique received / accepted sends. Offered delivery = unique received / attempts. Zero denominator is NA.
- Application unreceived = accepted−unique received by observation end. UID-correlated terminal ARP/MAC/IP drops are separated from unresolved packets; unresolved does not prove a packet is still in flight.
- IP PDR = FlowMonitor RX/TX. IP unreceived is split into reported drops, timeout-inferred losses and unresolved residuals. FlowMonitor TX is not offered application traffic.
- UDP goodput counts unique received UDP payload bytes, including the 12-byte SeqTsHeader. IP throughput uses received IP-layer bytes. Both use [2, appStop+drainTime), including drain, in kbps. Do not compare rates from different observation windows.
- Delay is mean timestamp-to-delivery time; jitter is mean absolute successive delay difference. No receptions means delay NA; fewer than two means jitter NA. Application samples exclude duplicate sequences. Latency is conditional on delivery and must show its defined sample count.

## Authoritative results and limitations

Use only [results/authoritative/comparison/REPORT.md](results/authoritative/comparison/REPORT.md), records.csv, summary.csv and paired.csv for the current evaluation: 24 distinct trials, 48 flow records, three run numbers at seed 7 per protocol/scenario. Legacy logs and obsolete outputs are excluded with reasons; identical reruns are not independent samples. Historical graphs are not validated illustrations of the corrected evaluation.

Confirmed failures remain visible: reciprocal RouteSense ARP requests receive PHY TXING rejection; AODV run 1 loses last-hop ARP reception amid overlapping transmissions and PREAMBLE_DETECT_FAILURE. RouteSense rejects 20 socket sends while cross-flow destination node 1 is disconnected. No safe custom-protocol defect was established that justifies altering radio settings, jitter, traffic timing or seeds.

Single-flow measured delivery favors RouteSense in this small sample; cross-traffic offered delivery/goodput favors AODV. Accepted-send PDR alone can conceal RouteSense's rejected sends. Conditional latency can favor a protocol that delivers fewer packets. Three trials at one seed do not establish normality or general performance. Confidence intervals are exploratory paired Student-t intervals, unadjusted for multiple comparisons; no statistical significance or universal superiority is claimed. RouteSense has free global topology knowledge, and routing overhead is unmeasured.

## Analysis and lightweight verification

Python analysis needs SciPy (available in the existing WSL environment). No dependency installation is needed for the current setup.

    python3 scripts/tests/test_analysis.py
    python3 scripts/tests/verify_project.py
    python3 scripts/comparison/reanalyze_authoritative.py --output results/verification/reanalysis-new
    python3 scripts/tracing/diagnose_cross_traffic.py results/generated/validation-final

For new datasets, specify only intended input directories; never include archive/analysis or superseded CSVs:

    python3 scripts/analysis/analyze_results.py --input results/generated/new-trials --output results/verification/new-analysis
    python3 scripts/tests/validate_measurements.py --output results/generated/new-validation
    python3 scripts/tests/check_measurement_edges.py --input results/generated/validation-final --output results/verification/new-edge-tests

The full validation command creates a small scenario/repeat/drain suite; it is optional after lightweight checks. The comparison runner can add runs 1 and 3 when explicitly needed; it is not part of final verification. No graphs are regenerated by analysis/tests. The legacy visualization script requires explicit input/output and must not be mistaken for a corrected schema-2 graph workflow.

## Evidence preservation after organization

Frozen CSVs/reports retain original recorded paths and hashes. [docs/organization/path-map.json](docs/organization/path-map.json) maps them to current locations; preserved-files.json records pre-move SHA-256 hashes. Active tools resolve those references without rewriting evidence. Earlier one-off scripts stored alongside evidence are forensic snapshots with historical paths, not supported entry points; use scripts/ instead.

    python3 scripts/analysis/resolve_evidence_paths.py results/authoritative/comparison/records.csv results/verification/records-resolved.csv

The resolver writes a separate CSV with resolved paths, preserving original fields. No historical evidence was deleted; backups are outside scratch so they no longer create accidental simulation targets.

## NetAnim visualization

Optional native animation is available for both programs without changing the simulation configuration:

    ./ns3 run "routesense --simulationTime=25 --dynamicScenario=1 --crossTraffic=1 --enableAnimation=1 --animationFile=results/netanim/custom-example.xml --outputDir=results/generated/custom-animation-example"

Use routesense-aodv for the baseline and fresh paths for each run. Animation defaults off. [NetAnim instructions and verified smoke traces](docs/NETANIM.md) describe both programs, colors/roles, packet metadata, exact GUI launch/open steps and limitations. The standalone GUI is not installed here; XML was validated without GUI playback.
