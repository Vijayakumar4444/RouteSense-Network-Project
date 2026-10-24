from pathlib import Path
p=Path('scripts/tests/check_measurement_edges.py');s=p.read_text()
s=s.replace('import csv, subprocess, sys','import argparse, csv, subprocess, sys')
s=s.replace("root=Path(sys.argv[1] if len(sys.argv)>1 else 'results/generated/validation-final')",'''ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument("--input",type=Path,default=ROOT/"results/generated/validation-final")
parser.add_argument("--output",type=Path,required=True,help="fresh output directory")
args=parser.parse_args()
root=args.input.resolve()
output=args.output.resolve()
output.mkdir(parents=True,exist_ok=False)''')
s=s.replace('folder=root/(protocol+','folder=output/(protocol+').replace("folder=root/'routesense-run3'","folder=output/'routesense-run3'")
s=s.replace('capture_output=True,text=True)', 'capture_output=True,text=True,cwd=ROOT)')
p.write_text(s)
p=Path('scripts/tracing/trace_comparison_failures.py');s=p.read_text()
s=s.replace("import os, subprocess", "import argparse, os, subprocess")
s=s.replace("root=Path('results/generated/comparison-diagnostics-new');root.mkdir(exist_ok=False)",'''ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument("--output",type=Path,required=True)
args=parser.parse_args()
root=args.output.resolve()
root.mkdir(parents=True,exist_ok=False)''')
s=s.replace('capture_output=True,text=True,env=env)', 'capture_output=True,text=True,env=env,cwd=ROOT)')
s=s.replace("original=Path('results/generated/comparison-trials')", "original=ROOT/'results/generated/comparison-trials'")
p.write_text(s)
p=Path('scripts/visualization/generate_graphs.py');s=p.read_text()
s='''"""Legacy schema graph generator; never use it as authoritative evaluation."""
import argparse
'''+s
s=s.replace('csv_file = root / "results" / "comparison_results.csv"\noutput_dir = root / "results" / "graphs"\noutput_dir.mkdir(parents=True, exist_ok=True)', '''parser = argparse.ArgumentParser(description="Legacy plots only; no schema-2 metric inference")
parser.add_argument("--csv", type=Path, required=True, help="explicit legacy CSV")
parser.add_argument("--output", type=Path, required=True, help="new graph directory")
args = parser.parse_args()
csv_file = args.csv
output_dir = args.output
output_dir.mkdir(parents=True, exist_ok=False)''')
s=s.replace('# Keep only the scenario with verified static, single-flow trials.', '# Preserve legacy classification; provenance is not verified.')
p.write_text(s)
