"""Two deterministic dynamic-cross smoke runs; no experiment batch."""
import argparse,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument("--output",type=Path,required=True);args=parser.parse_args()
root=args.output.resolve();root.mkdir(parents=True,exist_ok=False)
for protocol in ("routesense","routesense-aodv"):
    folder=root/protocol
    run=f"{protocol} --simulationTime=25 --dynamicScenario=1 --crossTraffic=1 --seed=7 --run=2 --outputDir={folder}"
    result=subprocess.run(["./ns3","run",run],cwd=ROOT,capture_output=True,text=True)
    assert result.returncode==0,result.stdout+result.stderr
    (folder/"run.txt").write_text(result.stdout+result.stderr)
    previous=ROOT/"results/generated/validation-final"/f"{protocol}-dynamic-cross"
    for name in ("metrics.csv","provenance.csv"):
        assert (folder/name).read_bytes()==(previous/name).read_bytes(),name
    assert next(folder.glob("*flowmon.xml")).read_bytes()==next(previous.glob("*flowmon.xml")).read_bytes()
    print(protocol,"metrics/provenance/XML identical after relocation PASS")
