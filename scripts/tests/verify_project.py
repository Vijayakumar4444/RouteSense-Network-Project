"""Verify preservation and all authoritative records after organization."""
import csv,hashlib,importlib.util,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"scripts/analysis"))
from resolve_evidence_paths import resolve_path
import analyze_results as analysis
manifest=json.loads((ROOT/"docs/organization/preserved-files.json").read_text())
checked=0
for entry in manifest:
    p=ROOT/entry["new"]
    if entry["new"].startswith(("scripts/","scratch/")): continue
    assert p.is_file(),p
    assert hashlib.sha256(p.read_bytes()).hexdigest()==entry["sha256"],p
    assert resolve_path(entry["old"])==p
    checked+=1
for entry in manifest:
    if entry["old"] in ("scratch/routesense.cc","scratch/routesense-aodv.cc"):
        source=(ROOT/entry["new"]).read_text()
        source=re.sub(r"^[ \t]*// BEGIN optional NetAnim\n.*?^[ \t]*// END optional NetAnim\n", "", source, flags=re.M|re.S)
        s=source.replace('#include "../common/routesense-measurement.h"','#include "routesense-measurement.h"')
        s=s.replace('"results/generated/routesense-new"','"results/routesense"').replace('"results/generated/aodv-new"','"results/baseline"')
        assert hashlib.sha256(s.encode()).hexdigest()==entry["sha256"],"Unexpected simulation behavior change"
authoritative=ROOT/"results/authoritative/comparison"
with (authoritative/"records.csv").open() as f: records=list(csv.DictReader(f))
assert len(records)==48
folders={r["directory"] for r in records}
assert len(folders)==24
for folder in folders:
    validated=analysis.validate(resolve_path(folder))
    original=[r for r in records if r["directory"]==folder]
    for r in original:
        actual=next(v for v in validated if v["port"]==r["port"])
        for key,value in r.items():
            if key!="directory": assert actual[key]==value,(folder,key)
print(f"PASS: {checked} evidence files unchanged; 24 trials / 48 records validated; routing behavior unchanged")
