"""Recompute only trials listed in the authoritative CSV, resolving moved paths."""
import argparse,csv,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"scripts/analysis"))
from resolve_evidence_paths import resolve_path
from analyze_results import analyze
parser=argparse.ArgumentParser()
parser.add_argument("--records",type=Path,default=ROOT/"results/authoritative/comparison/records.csv")
parser.add_argument("--output",type=Path,required=True)
args=parser.parse_args()
with args.records.open() as f: records=list(csv.DictReader(f))
inputs=sorted({resolve_path(r["directory"]) for r in records})
analyze(inputs,args.output)
for name in ("summary.csv","paired.csv"):
    assert (args.output/name).read_bytes()==(args.records.parent/name).read_bytes(),name
print("Reanalysis matches authoritative summary and paired intervals exactly")
