from pathlib import Path
import hashlib,json,shutil
ROOT=Path.cwd().resolve()
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
moves={
"scratch/routesense.cc":"scratch/routesense/custom/routesense.cc",
"scratch/routesense-aodv.cc":"scratch/routesense/baseline/routesense-aodv.cc",
"scratch/routesense-measurement.h":"scratch/routesense/common/routesense-measurement.h",
"scratch/routesense-backup.cc":"archive/source/scratch/routesense-backup.cc",
"scratch/routesense-aodv-backup.cc":"archive/source/scratch/routesense-aodv-backup.cc",
"scratch/routesense.cc.save":"archive/source/scratch/routesense.cc.save",
"results/baseline/routesense-aodv.cc":"archive/source/baseline/routesense-aodv.cc",
"README.md":"docs/upstream/README-ns3.md",
"ROUTESENSE_AUDIT.md":"docs/audits/ROUTESENSE_AUDIT.md",
"ROUTESENSE_MEASUREMENT_VALIDATION.md":"docs/audits/ROUTESENSE_MEASUREMENT_VALIDATION.md",
"scripts/analyze_results.py":"scripts/analysis/analyze_results.py",
"scripts/run_comparison_trials.py":"scripts/comparison/run_comparison_trials.py",
"scripts/diagnose_cross_traffic.py":"scripts/tracing/diagnose_cross_traffic.py",
"scripts/trace_comparison_failures.py":"scripts/tracing/trace_comparison_failures.py",
"scripts/test_analysis.py":"scripts/tests/test_analysis.py",
"scripts/validate_measurements.py":"scripts/tests/validate_measurements.py",
"scripts/check_measurement_edges.py":"scripts/tests/check_measurement_edges.py",
"scripts/generate_graphs.py":"scripts/visualization/generate_graphs.py",
"results/trials":"archive/logs/trials",
"results/comparison_results.csv":"archive/analysis/comparison_results.csv",
"results/comparative-evaluation-audited":"results/authoritative/comparison",
"results/audit":"results/authoritative/audit-evidence",
}
for name in ("validation","validation-final","validation-recheck","comparison-trials","comparison-diagnostics","baseline","routesense"):
 moves["results/"+name]="results/generated/"+name
for name in ("comparative-evaluation","comparative-evaluation-checked","comparative-evaluation-final"):
 moves["results/"+name]="archive/analysis/"+name
early=next((p for p in (ROOT/"results").iterdir() if p.is_dir() and p.name.startswith("pha")),None)
if early: moves[str(early.relative_to(ROOT))]="results/generated/early-experiment"
manifest=[]
for old,new in moves.items():
 src=(ROOT/old).resolve();dest=(ROOT/new).resolve()
 assert src.is_relative_to(ROOT) and dest.is_relative_to(ROOT) and src.exists() and not dest.exists(),(src,dest)
 files=list(src.rglob("*")) if src.is_dir() else [src]
 snapshot=[(p,sha(p)) for p in files if p.is_file()]
 dest.parent.mkdir(parents=True,exist_ok=True)
 shutil.move(str(src),str(dest))
 for p,digest in snapshot:
  target=dest/p.relative_to(src) if dest.is_dir() else dest
  assert sha(target)==digest,target
  manifest.append(dict(old=str(p.relative_to(ROOT)),new=str(target.relative_to(ROOT)),sha256=digest))
organization=ROOT/"docs/organization";organization.mkdir(parents=True,exist_ok=True)
(organization/"path-map.json").write_text(json.dumps(moves,indent=2))
(organization/"preserved-files.json").write_text(json.dumps(manifest,indent=2))
for rel in ("scratch/routesense/custom/routesense.cc","scratch/routesense/baseline/routesense-aodv.cc"):
 p=ROOT/rel;s=p.read_text().replace('#include "routesense-measurement.h"','#include "../common/routesense-measurement.h"')
 s=s.replace('"results/routesense"','"results/generated/routesense-new"').replace('"results/baseline"','"results/generated/aodv-new"')
 p.write_text(s)
for p in (ROOT/"scripts").rglob("*.py"):
 if "__pycache__" in str(p): continue
 s=p.read_text().replace('.resolve().parents[1]','.resolve().parents[2]').replace('.resolve().parent.parent','.resolve().parents[2]')
 for old,new in (
 ('scripts/analyze_results.py','scripts/analysis/analyze_results.py'),
 ('results/validation-final','results/generated/validation-final'),
 ('results/comparative-evaluation-audited','results/authoritative/comparison'),
 ('results/comparison-trials','results/generated/comparison-trials'),
 ('results/comparison-diagnostics','results/generated/comparison-diagnostics-new'),
 ('ROOT/"results/trials"','ROOT/"archive/logs/trials"'),
 ('"scratch/routesense.cc"','"scratch/routesense/custom/routesense.cc"'),
 ('"scratch/routesense-aodv.cc"','"scratch/routesense/baseline/routesense-aodv.cc"'),
 ('"scratch/routesense-measurement.h"','"scratch/routesense/common/routesense-measurement.h"')):
  s=s.replace(old,new)
 p.write_text(s)
p=ROOT/"scratch/.gitignore";p.write_text(p.read_text()+"\n# RouteSense simulations and shared measurement code\n!routesense/\n")
(ROOT/"scratch/routesense/CMakeLists.txt").write_text('''# Preserve existing ns-3 targets after separating implementations.
foreach(program IN ITEMS routesense routesense-aodv)
  if(program STREQUAL "routesense")
    set(source custom/routesense.cc)
  else()
    set(source baseline/routesense-aodv.cc)
  endif()
  build_exec(
    EXECNAME @{program}
    EXECNAME_PREFIX scratch_
    SOURCE_FILES @{source}
    LIBRARIES_TO_LINK "@{ns3-libs}" "@{ns3-contrib-libs}"
    EXECUTABLE_DIRECTORY_PATH "@{CMAKE_OUTPUT_DIRECTORY}/scratch/"
  )
endforeach()
'''.replace("@{",chr(36)+"{"))
print("Moved and hash-verified",len(manifest),"files; no evidence deleted.")
