import argparse, os, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument("--output",type=Path,required=True)
args=parser.parse_args()
root=args.output.resolve()
root.mkdir(parents=True,exist_ok=False)
for dynamic in (0,1):
 case='dynamic-single' if dynamic else 'static-single'
 folder=root/case
 args=f'routesense-aodv --simulationTime={25 if dynamic else 6} --dynamicScenario={dynamic} --crossTraffic=0 --seed=7 --run=1 --drainTime=15 --diagnostics=1 --outputDir={folder}'
 env=os.environ.copy();env['NS_LOG']='ArpL3Protocol=level_logic|prefix_time:ArpCache=level_logic|prefix_time'
 r=subprocess.run(['./ns3','run',args],capture_output=True,text=True,env=env,cwd=ROOT)
 assert r.returncode==0,r.stderr
 (folder/'run.txt').write_text(r.stdout+r.stderr)
 original=ROOT/'results/generated/comparison-trials'/f'routesense-aodv-{case}-run1'
 assert (folder/'metrics.csv').read_bytes()==(original/'metrics.csv').read_bytes()
 print(case,'diagnostics repeat matches original',flush=True)
