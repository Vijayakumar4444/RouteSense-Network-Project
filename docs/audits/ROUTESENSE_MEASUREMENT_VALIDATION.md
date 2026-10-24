# Simulation validation and measurement corrections

## Confirmed causes
Seed 7/run 2 reproduces the reported RouteSense cross-flow failure.
PCAP decoding identifies reciprocal ARP requests at nodes 10/11 at exactly 2.002050 s; PHY packet UIDs correlate to TXING rejection at the opposite node at 2.002050267 s. Requests recur simultaneously at 3.000050, 4.000050 and 5.000050 s; neither requester receives an ARP reply. Static traces show 12 ARP packet drops and no MAC MPDU drop or PHY transmit drop. Other receivers report preamble failures, but those are not evidence of an invalid next hop.
The next hops are within 100 m and valid. These accepted cross packets never pass ARP to become data transmissions. Thus the demonstrated primary cause is synchronized reciprocal ARP transmission and half-duplex receive rejection, followed by resolution failure, not a custom forwarding loop or sink error.
Equivalent AODV requests occur at 2.003453 and 2.009144 s, receive replies and deliver both static cross flows. Protocol control exchanges/RNG consumption differ; equal seed/run does not imply identical ARP timing.
Local ns-3 ArpCache::HandleWaitReplyTimeout calls the request callback directly on fixed timeout boundaries; retry requests do not pass through the initial RequestJitter scheduling. This simulator behavior can sustain synchronization. No simulator code, traffic offsets or RNG settings were altered to improve delivery.

The dynamic 24/0 result has a second cause: 44 application attempts, only 24 socket-accepted sends, and 20 ERROR_NOROUTETOHOST failures while node 1 is disconnected (12–22 s). FlowMonitor never counted those rejected attempts. The other cross flow has 44 accepted sends and 0 receptions; all accepted unreceived packets in these tests correlate to terminal drop traces.

## Exact implementation files
- scratch/routesense.cc
- scratch/routesense-aodv.cc
  Both use shared counters, explicit drainTime (default 15 s), provenance and fresh-output protection. Previous IP-only text metrics were replaced by schema-2 CSV.
- scratch/routesense-measurement.h (new)
  Instruments existing UdpClient and PacketSink rather than replacing them. Counts Tx trace attempts, checks GetTotalTx immediately after each synchronous Send to distinguish acceptance, records errno failures, SeqTsHeader unique receptions, duplicates and out-of-order arrivals. Captures source/destination IP and ports. ARP/MAC/IP terminal drops are correlated by packet UID, excluding packets subsequently received. Optional diagnostics adds PHY event CSV.
- scripts/analyze_results.py
  Explicit --input/--output mode combines schema-2 CSV with provenance, preserves NA, keeps layers separate, refuses overwrite. Historical text parsing remains available but refuses to replace an existing historical comparison CSV.
- scripts/validate_measurements.py (new)
  Scenario, repeatability, drain and accounting checks; requires a fresh output root.
- scripts/diagnose_cross_traffic.py (new)
  Standard-library PCAP ARP decoder and PHY UID correlation.
- scripts/check_measurement_edges.py (new)
  Saved accounting invariants, one-packet semantics, changed run and overwrite protection.
- ROUTESENSE_MEASUREMENT_VALIDATION.md (this report).
No historical results or comparison graphs were replaced.

## Metric definitions: before -> after
- PDR: IP RX/TX presented as application metrics -> separate application PDR=unique RX/socket-accepted sends and IP PDR=FlowMonitor RX/TX; zero denominator is NA. Attempts and send failures remain explicit.
- Application loss: previously unavailable -> accepted minus unique RX, described as unreceived by observation end. Observed terminal ARP/MAC/IP drops are counted once by accepted packet UID; the remainder is unresolved, not automatically declared in flight or lost.
- IP loss: TX-RX called loss -> unreceived, split into FlowMonitor reported drops, timeout-inferred loss and unresolved tracked residual. A timeout is not proof of a particular physical drop. ARP/MAC drops may be known by traces while FlowMonitor knows only their later timeout.
- Throughput: IP bytes/(T-3), with an application label -> application UDP-payload goodput and IP throughput separately, both over [2, appStop+drainTime). UDP payload includes the existing 12-byte sequence/timestamp header; IP bytes include IP/UDP overhead. Unique application deliveries count once.
- Delay: zero with no reception -> NA; otherwise mean timestamp-to-delivery delay for unique packets, with separate FlowMonitor IP delay.
- Jitter: zero with fewer than two samples -> NA; otherwise mean absolute difference between successive unique arrival delays. IP jitter uses FlowMonitor jitterSum/(RX-1). Duplicates excluded from application samples; out-of-order means a newly received sequence below the highest seen sequence.

## Timing and drain
Existing sources retain start 2 s and stop T-1 s. Sinks/monitor now continue until T-1+drainTime. Thus simulationTime retains its original nominal traffic scheduling role; actual simulation end is explicitly recorded. For T=25, source stop=24, default end=39 and measurement duration=37 s.
All throughput values use that actual observation duration, including drain; extending drain intentionally lowers window-average throughput even if RX is unchanged. Use the same drain across compared protocols.
The old one-second drain leaves 17/4 unresolved cross IP packets in RouteSense and 1/16 unresolved main/third-flow packets in AODV. At 15 and 30 s, tested dynamic reception totals and reported-drop totals match and unresolved IP counts are zero. Fifteen seconds is sufficient for these tested workloads, not a universal guarantee. FlowMonitor's default 10 s per-hop timeout is unchanged; timeout classifications are not forced by a zero timeout.

## Actual tests and outcomes
Both targets compile on existing ns-3 3.47:
./ns3 build routesense routesense-aodv

Each tuple below is attempts / socket accepted / unique received.
All scenario tests use seed 7/run 2 and interval 0.5 s.

| Scenario | RouteSense main; cross 1; cross 2 | AODV main; cross 1; cross 2 |
|---|---|---|
| Static single, T=6 | 6/6/6 | 6/6/4 |
| Dynamic single, T=25 | 44/44/44 | 44/44/42 |
| Static cross, T=6 | 6/6/6; 6/6/0; 6/6/0 | 6/6/6; 6/6/6; 6/6/6 |
| Dynamic cross, T=25 | 44/44/44; 44/44/0; 44/24/0 | 44/44/43; 44/44/44; 44/44/22 |

All eight scenario checks passed; passing means correct accounting/execution, not successful delivery of every flow.
Both dynamic cross cases repeated with byte-identical metrics.csv, provenance.csv and FlowMonitor XML.
Both protocols passed 1/15/30-second drain checks.
One-packet tests for both protocols delivered 1/1 with valid delay and NA jitter.
Changed run=3 was recorded accurately; existing-output rejection preserved file bytes.
Source-port and terminal-drop decomposition checks passed across saved cases.
All measured duplicate/out-of-order counts were zero; nonzero cases were not injected.
Schema-2 extraction passed (34 flow records before the later edge tests). Python syntax checks passed.
No large experiment batch or graph regeneration.

Evidence: results/validation-final/ subdirectories, diagnosis.txt, edge-checks.txt and measurements.csv.
Reproduce into a new directory:
python3 scripts/validate_measurements.py --output results/new-validation
python3 scripts/diagnose_cross_traffic.py results/new-validation
python3 scripts/check_measurement_edges.py results/new-validation
python3 scripts/analyze_results.py --input results/new-validation --output results/new-validation/measurements.csv

## Trust and limitations
These measurements are trustworthy for controlled comparison of the tested scenarios with explicit denominators and observation windows. They do not establish a general performance ranking.
Retain unsuccessful-send counts alongside accepted-send PDR: the accepted denominator alone excludes RouteSense's 20 disconnected-destination rejections.
Preserve the observed ARP failure as a result; correcting measurements does not justify tuning it away. Broader evaluation needs multiple paired runs, confidence intervals, load/drain convergence checks and attention to ARP synchronization sensitivity.
RouteSense still uses free global instantaneous topology and distance proxies rather than measured link stability; AODV pays discovery/control costs. Routing overhead remains unmeasured.
UID-based terminal-drop correlation applies to these unfragmented UDP flows in one simulation process; it is not a general fragmented/multipath packet lineage system. NA residuals and timeouts must not be presented as proven physical loss. Longer queues or higher load may require a longer drain.
