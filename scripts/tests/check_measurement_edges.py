import argparse, csv, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument("--input",type=Path,default=ROOT/"results/generated/validation-final")
parser.add_argument("--output",type=Path,required=True,help="fresh output directory")
args=parser.parse_args()
root=args.input.resolve()
output=args.output.resolve()
output.mkdir(parents=True,exist_ok=False)
for folder in root.iterdir():
 if folder.is_dir() and (folder/'metrics.csv').exists():
  for r in csv.DictReader((folder/'metrics.csv').open()):
   assert int(r['source_port']) > 0
   assert int(r['app_unreceived']) == int(r['app_drop_observed_unreceived'])+int(r['app_unresolved'])
print('All saved source-port and drop decomposition checks PASS')
for protocol in ('routesense','routesense-aodv'):
 folder=output/(protocol+'-one-packet')
 result=subprocess.run(['./ns3','run',f'{protocol} --simulationTime=6 --packetInterval=100 --seed=7 --run=2 --outputDir={folder}'],capture_output=True,text=True,cwd=ROOT)
 assert result.returncode==0,result.stderr
 (folder/'run.txt').write_text(result.stdout+result.stderr)
 r=list(csv.DictReader((folder/'metrics.csv').open()))[0]
 assert r['attempts']==r['accepted']==r['unique_rx']=='1',r
 assert r['app_jitter_ms']==r['ip_jitter_ms']=='NA'
 assert r['app_delay_ms']!='NA'
 print(protocol,'one packet: delay available, jitter NA PASS')
# Fresh output protection must reject a repeated output directory.
before=(folder/'metrics.csv').read_bytes()
r=subprocess.run(['./ns3','run',f'{protocol} --outputDir={folder}'],capture_output=True,text=True,cwd=ROOT)
assert r.returncode!=0 and (folder/'metrics.csv').read_bytes()==before
print('Historical-output protection PASS')
# A changed run is recorded accurately; a changed metric is not assumed.
folder=output/'routesense-run3'
r=subprocess.run(['./ns3','run',f'routesense --simulationTime=6 --seed=7 --run=3 --outputDir={folder}'],capture_output=True,text=True,cwd=ROOT)
assert r.returncode==0
m=list(csv.DictReader((folder/'provenance.csv').open()))[0]
assert m['seed']=='7' and m['run']=='3'
print('Changed-run provenance PASS')
