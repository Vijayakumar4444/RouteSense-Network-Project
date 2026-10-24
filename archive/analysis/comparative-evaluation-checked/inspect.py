import csv
from pathlib import Path
from collections import Counter
root=Path('results/comparative-evaluation-checked')
rows=list(csv.DictReader((root/'summary.csv').open()))
for scenario,flow in sorted({(r['scenario'],r['flow']) for r in rows}):
 print(scenario,flow)
 for protocol in ('routesense','routesense-aodv'):
  group={r['metric']:r for r in rows if (r['scenario'],r['flow'],r['protocol'])==(scenario,flow,protocol)}
  print(protocol,'; '.join(f"{k}={group[k]['mean']} +/- {group[k]['sd']} n={group[k]['n_defined']}" for k in ('app_pdr_pct','offered_delivery_pct','app_udp_goodput_kbps','app_delay_ms','app_jitter_ms','send_failures')))
print('EXCLUSIONS',Counter(r['reason'] for r in csv.DictReader((root/'excluded.csv').open())))
for r in csv.DictReader((root/'paired.csv').open()):
 if r['flow']=='main' and r['metric'] in ('app_pdr_pct','app_delay_ms'):
  print('PAIRED',r)
