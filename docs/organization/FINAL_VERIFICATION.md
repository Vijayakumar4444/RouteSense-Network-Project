# Final organization and verification

The layout and commands are documented in ../../README.md. The frozen comparison is ../../results/authoritative/comparison/REPORT.md.

## Final tree
- scratch/routesense/custom/: RouteSense C++.
- scratch/routesense/baseline/: conventional AODV C++.
- scratch/routesense/common/: unchanged shared measurement header.
- scripts/{analysis,comparison,tracing,tests,visualization}/: tools by function.
- results/authoritative/{comparison,audit-evidence}/: frozen authoritative comparison and audit evidence.
- results/generated/: preserved raw simulation datasets and legacy simulation outputs.
- results/graphs/: all five existing graphs, unchanged and not regenerated.
- results/verification/final/: post-organization checks, resolved references and lightweight smoke outputs.
- archive/{source,logs,analysis,maintenance}/: backups, historical logs, superseded analyses and maintenance snapshots.
- docs/{audits,upstream,organization}/: preserved reports, upstream README, maps, hashes and this report.

## Moves and modifications
Every moved file is listed with original path, new path and pre-move SHA-256 in preserved-files.json; path-map.json records prefix mappings. No historical files were deleted. duplicate-content.csv identifies 233 identical-content groups; none was removed just because its hash matched.
The C++ sources moved into separate scratch subdirectories. Only the shared-header include paths and default output directory strings changed; normalized-source hashes confirm no routing behavior change. A local CMakeLists preserves ./ns3 build/run names routesense and routesense-aodv. Backups are outside scratch so they no longer become accidental build targets. scratch/.gitignore now exposes the maintained sources.
Active scripts have updated root discovery, imports, fixture paths and source paths. Added authoritative-only reanalysis, frozen-path resolution, integrity verification and two-run final smoke checks. Edge-test outputs now use a fresh explicit --output directory instead of attempting to reuse existing evidence. Diagnostic repeat output is explicit. The legacy graph generator now requires explicit CSV/new output paths; its graph design was not changed.
The old upstream README and earlier RouteSense reports are preserved. Root README is now the project guide. Historical reports, one-off evidence scripts and recorded CSV paths are retained as snapshots; active tools resolve their paths through the map rather than rewriting evidence. records-resolved.csv is a separate convenience output.
The authoritative records.csv, summary.csv, paired.csv, excluded.csv, historical inventory and REPORT.md are byte-preserved.

## Verified results
- Both relocated C++ targets compile successfully in the existing ns-3.47 build.
- Ten analysis regression checks pass after the moves.
- 1,223 preserved non-active-source/script evidence files pass SHA-256 checks; all mapped references tested by the integrity script exist.
- All 24 authoritative trials / 48 flow records revalidate: endpoints, provenance, attempt/acceptance/reception counts, loss decomposition, duration, units, NA and XML consistency.
- Authoritative-only reanalysis reproduces summary.csv and paired.csv byte-for-byte. Superseded directories are not analysis inputs.
- Two dynamic-cross smoke runs produce byte-identical metrics, provenance and FlowMonitor XML to pre-move results.
- Two one-packet edge runs preserve valid delay and NA jitter. Changed run=3 provenance and populated-output rejection pass.
- Preserved ARP PCAP/PHY correlation passes. All active Python files compile.
- Existing graphs were preserved; no graphs or large experiments were run.
Logs are in ../../results/verification/final/: regression.txt, integrity.txt, smoke.txt, edges.txt, reanalysis.txt and arp-diagnosis.txt.

## Remaining issues and limits
No confirmed custom routing defect was found that safely resolves the ARP/PHY failures. RouteSense reciprocal ARP requests are rejected with TXING; AODV run-1 last-hop requests overlap other transmissions and fail preamble detection. The observed failures remain in the data. The precise impact of alternative ARP retry jitter is not tested, and radio/traffic/seed settings were not tuned.
RouteSense's disconnected cross destination causes 20 rejected sends: accepted-send PDR must remain separate from offered-packet delivery. Delay/jitter remain NA with insufficient samples and conditional on received packets.
There are only three run pairs at one seed. Paired confidence intervals are exploratory and unadjusted for multiple comparisons; there is no statistical-significance or universal-superiority claim. RouteSense retains free global topology knowledge and distance-proxy costs. Routing overhead is unmeasured. Drain convergence was verified for tested workloads only.
The complete upstream ns-3 suite, broader experiments, alternative ARP behavior and graph execution were intentionally not run. CMake reports optional documentation/integration dependencies absent; they do not prevent either simulation build. The empty src/routesense placeholder is not a configured ns-3 module.
