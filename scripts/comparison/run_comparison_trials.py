"""Two additional paired runs per scenario; reuses run 2 through the analyzer."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve()
out.mkdir(parents=True,exist_ok=False)
manifest=[]
for run in (1,3):
    for dynamic,cross,scenario in ((0,0,"static-single"),(1,0,"dynamic-single"),(0,1,"static-cross"),(1,1,"dynamic-cross")):
        for protocol in ("routesense","routesense-aodv"):
            folder=out/f"{protocol}-{scenario}-run{run}"
            args=f"{protocol} --simulationTime={25 if dynamic else 6} --dynamicScenario={dynamic} --crossTraffic={cross} --seed=7 --run={run} --drainTime=15 --diagnostics={cross} --outputDir={folder}"
            result=subprocess.run(["./ns3","run",args],cwd=ROOT,capture_output=True,text=True)
            folder.mkdir(exist_ok=True)
            (folder/"run.txt").write_text(result.stdout+result.stderr)
            if result.returncode: raise RuntimeError(result.stderr)
            manifest.append(dict(arguments=args,metrics_sha256=hashlib.sha256((folder/"metrics.csv").read_bytes()).hexdigest()))
        print("completed paired run",run,scenario,flush=True)
(out/"trial_manifest.json").write_text(json.dumps(manifest,indent=2))
