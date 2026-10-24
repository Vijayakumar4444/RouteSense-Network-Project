"""Validated layer-separated analysis. No graphs and no overwrite of old outputs."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics as st
import xml.etree.ElementTree as ET
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[2]
ENDPOINTS = {
    "9": ("10.1.1.1", "10.1.1.6", "main"),
    "10": ("10.1.1.11", "10.1.1.5", "cross_1"),
    "11": ("10.1.1.12", "10.1.1.2", "cross_2"),
}
METRICS = ("attempts", "accepted", "send_failures", "unique_rx", "app_unreceived",
           "app_pdr_pct", "offered_delivery_pct", "app_udp_goodput_kbps",
           "app_delay_ms", "app_jitter_ms", "ip_pdr_pct", "ip_throughput_kbps",
           "ip_delay_ms", "ip_jitter_ms", "ip_unreceived", "ip_unresolved")
COUNTS = ("attempts", "accepted", "send_failures", "unique_rx", "duplicates",
          "out_of_order", "app_unreceived", "app_drop_observed_unreceived",
          "app_unresolved", "ip_tx", "ip_rx", "ip_unreceived",
          "ip_reported_drop", "ip_timeout_inferred", "ip_unresolved")
CONFIG = ("app_start", "app_stop", "simulation_stop", "drain", "interval", "dynamic", "cross", "packet_bytes")

def read(path):
    with path.open() as f:
        reader = csv.DictReader(f)
        if not reader.fieldnames or len(reader.fieldnames) != len(set(reader.fieldnames)):
            raise ValueError("missing or duplicate CSV columns")
        data = list(reader)
        if any(None in r or None in r.values() for r in data):
            raise ValueError("malformed CSV row")
        return data

def write(path, rows, fields=None):
    rows = list(rows)
    with path.open("x", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields or list(rows[0]))
        w.writeheader()
        w.writerows(rows)

def number(value):
    if value == "NA": return None
    x = float(value)
    if not math.isfinite(x) or x < 0: raise ValueError("nonfinite or negative metric")
    return x

def check(value, expected, name, tolerance=1e-8):
    got = number(value)
    if expected is None:
        if got is not None: raise ValueError(f"{name}: must be NA")
    elif got is None or not math.isclose(got, expected, rel_tol=1e-9, abs_tol=tolerance):
        raise ValueError(f"{name}: {got} != {expected}")

def validate(folder):
    metas = read(folder / "provenance.csv")
    if len(metas) != 1: raise ValueError("expected one provenance row")
    m = metas[0]
    if m["schema"] != "2" or m["protocol"] not in ("routesense", "routesense-aodv"):
        raise ValueError("unsupported schema/protocol")
    for k in ("seed", "run", "packet_bytes"):
        if str(int(m[k])) != m[k]: raise ValueError(f"invalid {k}")
    if int(m["seed"]) <= 0 or int(m["run"]) < 0: raise ValueError("invalid RNG provenance")
    if m["dynamic"] not in ("0", "1") or m["cross"] not in ("0", "1"):
        raise ValueError("invalid scenario flags")
    start, stop, end, drain, interval = (float(m[k]) for k in CONFIG[:5])
    if not all(math.isfinite(x) for x in (start, stop, end, drain, interval)) or not (
        0 <= start < stop < end and drain > 0 and interval > 0 and math.isclose(end-stop, drain)):
        raise ValueError("invalid measurement window")
    if m["dynamic"] == "1" and stop <= 22: raise ValueError("dynamic traffic does not span both events")
    data = read(folder / "metrics.csv")
    expected_ports = {"9", "10", "11"} if m["cross"] == "1" else {"9"}
    if len(data) != len(expected_ports) or {r["port"] for r in data} != expected_ports:
        raise ValueError("missing, duplicate or unexpected flow")
    xmls = list(folder.glob("*flowmon.xml"))
    if len(xmls) != 1: raise ValueError("expected one FlowMonitor XML")
    tree = ET.parse(xmls[0])
    tuples = {r.attrib["flowId"]: r.attrib for r in tree.findall("./Ipv4FlowClassifier/Flow")}
    stats = {r.attrib["flowId"]: r for r in tree.findall("./FlowStats/Flow")}
    for r in data:
        src, dst, flow = ENDPOINTS[r["port"]]
        if (r["source"], r["destination"]) != (src, dst) or not 0 < int(r["source_port"]) < 65536:
            raise ValueError("unexpected endpoints/port")
        c = {}
        for k in COUNTS:
            c[k] = int(r[k])
            if c[k] < 0 or str(c[k]) != r[k]: raise ValueError(f"invalid count {k}")
        a, ok, rx, tx, irx = (c[k] for k in ("attempts", "accepted", "unique_rx", "ip_tx", "ip_rx"))
        if not 0 <= rx <= ok <= a or irx > tx: raise ValueError("inconsistent packet counts")
        if a != math.ceil((stop-start)/interval-1e-12): raise ValueError("unexpected attempt count")
        equalities = [
            (c["send_failures"], a-ok), (c["app_unreceived"], ok-rx),
            (c["app_unreceived"], c["app_drop_observed_unreceived"]+c["app_unresolved"]),
            (c["ip_unreceived"], tx-irx),
            (c["ip_unreceived"], sum(c[k] for k in ("ip_reported_drop","ip_timeout_inferred","ip_unresolved")))]
        if any(x != y for x,y in equalities): raise ValueError("loss decomposition mismatch")
        check(r["app_pdr_pct"], 100*rx/ok if ok else None, "app PDR")
        check(r["ip_pdr_pct"], 100*irx/tx if tx else None, "IP PDR")
        check(r["app_udp_goodput_kbps"], rx*int(m["packet_bytes"])*8/(end-start)/1000, "goodput")
        for prefix,n in (("app",rx),("ip",irx)):
            for suffix,threshold in (("delay_ms",1),("jitter_ms",2)):
                val=number(r[f"{prefix}_{suffix}"])
                if (val is None) != (n < threshold): raise ValueError("undefined delay/jitter handling")
        matching = [fid for fid,t in tuples.items() if
            (t["sourceAddress"],t["destinationAddress"],t["sourcePort"],t["destinationPort"],t["protocol"])
            == (src,dst,r["source_port"],r["port"],"17")]
        if len(matching) > 1 or (tx > 0 and len(matching) != 1):
            raise ValueError("ambiguous/missing XML flow")
        if matching:
            x=stats[matching[0]]
            if (int(x.attrib["txPackets"]),int(x.attrib["rxPackets"])) != (tx,irx):
                raise ValueError("CSV/XML packet mismatch")
            drop=sum(int(e.attrib["number"]) for e in x.findall("packetsDropped"))
            if drop != c["ip_reported_drop"] or int(x.attrib["lostPackets"]) != drop+c["ip_timeout_inferred"]:
                raise ValueError("CSV/XML loss mismatch")
            check(r["ip_throughput_kbps"],int(x.attrib["rxBytes"])*8/(end-start)/1000,"IP throughput")
            for k,total,den in (("ip_delay_ms","delaySum",irx),("ip_jitter_ms","jitterSum",irx-1)):
                raw=x.attrib[total]
                if not raw.endswith("ns"): raise ValueError("unsupported XML time unit")
                total_ns=float(raw[:-2])
                # FlowMonitor XML uses six significant digits; CSV retains twelve.
                rounding_ns=0.5*10**(math.floor(math.log10(abs(total_ns)))-5) if total_ns else 0
                tolerance=rounding_ns*1e-6/den+1e-8 if den>0 else 1e-8
                check(r[k],total_ns*1e-6/den if den>0 else None,k,tolerance)
        else:
            check(r["ip_throughput_kbps"],0,"IP throughput with no XML flow")
        r.update(m)
        r.update(directory=str(folder.resolve()), flow=flow,
                 scenario=("dynamic" if m["dynamic"]=="1" else "static")+("_cross" if m["cross"]=="1" else "_single"),
                 offered_delivery_pct=str(100*rx/a) if a else "NA")
    return data

def historical(path):
    text=path.read_text(errors="replace")
    reasons=["legacy log lacks verified seed/run, duration and schema-2 application accounting"]
    blocks=list(re.finditer(r"Flow \d+:\s*(10\.1\.1\.\d+) -> (10\.1\.1\.\d+)\s*\n(.*?)(?=\n\s*Flow \d+:|\Z)",text,re.S))
    records=[]
    for b in blocks:
        src,dst,block=b.groups()
        expected=[(port,flow) for port,(s,d,flow) in ENDPOINTS.items() if (s,d)==(src,dst)]
        if not expected: continue
        row=dict(file=str(path.resolve()),source=src,destination=dst,flow=expected[0][1],port="unlogged")
        for label,key in (("TX packets","ip_tx"),("RX packets","ip_rx"),("PDR","ip_pdr_pct"),
                          ("Throughput","ip_throughput_kbps"),("Average delay","ip_delay_ms"),("Average jitter","ip_jitter_ms")):
            match=re.search(re.escape(label)+r":\s*([\d.]+)",block)
            row[key]=match.group(1) if match else "NA"
        dynamic=re.search(r"Dynamic scenario:\s*(Enabled|Disabled)",text)
        cross=re.search(r"Cross traffic:\s*(Enabled|Disabled)",text)
        row["scenario"]=("dynamic" if dynamic[1]=="Enabled" else "static")+("_cross" if cross[1]=="Enabled" else "_single") if dynamic and cross else "unclassified"
        if "NA" in (row[k] for k in ("ip_tx","ip_rx","ip_pdr_pct","ip_throughput_kbps","ip_delay_ms","ip_jitter_ms")):
            reasons.append("incomplete legacy metrics")
        if row["ip_rx"]!="NA":
            n=int(row["ip_rx"])
            if n==0: row["ip_delay_ms"]="NA"
            if n<2: row["ip_jitter_ms"]="NA"
        records.append(row)
    if not records: reasons.append("no identifiable application flow block")
    return records,"; ".join(sorted(set(reasons)))

def analyze(inputs, output, legacy=None):
    output.mkdir(parents=True,exist_ok=False)
    rejected=[]; historical_rows=[]; candidates={}
    for root in inputs:
        if not root.exists(): raise ValueError(f"missing input root: {root}")
        for file in sorted(root.rglob("metrics.csv")):
            folder=file.parent
            try:
                data=validate(folder)
                m=data[0]
                # Predeclared comparison cohort: existing smoke workloads, 15-second drain.
                if not (float(m["app_start"])==2 and float(m["drain"])==15 and
                        float(m["interval"])==0.5 and int(m["packet_bytes"])==1024 and
                        float(m["app_stop"])==(24 if m["dynamic"]=="1" else 5)):
                    raise ValueError("valid diagnostic run outside declared comparison cohort (timing/load/drain)")
                key=tuple(m[k] for k in ("protocol","seed","run")+CONFIG)
                signature=tuple(tuple(sorted((k,v) for k,v in row.items() if k!="directory")) for row in data)
                if key in candidates:
                    if candidates[key][1] is None:
                        raise ValueError("conflicting same seed/run/config group already excluded")
                    if candidates[key][0]!=signature:
                        old=candidates[key][1]
                        rejected.append(dict(path=old[0]["directory"],reason="conflicting same seed/run/config; both excluded"))
                        candidates[key]=(None,None)
                        raise ValueError("conflicting same seed/run/config; both excluded")
                    raise ValueError("duplicate execution of identical seed/run/config; not an independent trial")
                candidates[key]=(signature,data)
            except (ValueError,KeyError,ET.ParseError,OSError) as e:
                rejected.append(dict(path=str(folder.resolve()),reason=str(e)))
    if legacy:
        for path in sorted(legacy.glob("*.txt")):
            rows,reason=historical(path);historical_rows+=rows
            rejected.append(dict(path=str(path.resolve()),reason=reason))
    accepted=[row for _,data in candidates.values() if data for row in data]
    if not accepted:
        write(output/"excluded.csv",rejected,["path","reason"])
        raise ValueError("no valid comparison records; see excluded.csv")
    groups=defaultdict(list)
    for r in accepted: groups[(r["scenario"],r["flow"],r["protocol"])].append(r)
    summaries=[]
    for (scenario,flow,protocol),rs in sorted(groups.items()):
        for metric in METRICS:
            values=[number(r[metric]) for r in rs if r[metric]!="NA"]
            summaries.append(dict(scenario=scenario,flow=flow,protocol=protocol,metric=metric,
                n_trials=len(rs),n_defined=len(values),mean=st.mean(values) if values else "NA",
                sd=st.stdev(values) if len(values)>1 else "NA"))
    from scipy.stats import t
    paired=[]
    for scenario,flow in sorted({(r["scenario"],r["flow"]) for r in accepted}):
        left=groups.get((scenario,flow,"routesense"),[]);right=groups.get((scenario,flow,"routesense-aodv"),[])
        def pairkey(r): return tuple(r[k] for k in ("seed","run")+CONFIG)
        a={pairkey(r):r for r in left};b={pairkey(r):r for r in right}
        for metric in METRICS:
            differences=[float(a[k][metric])-float(b[k][metric]) for k in sorted(a.keys()&b.keys())
                         if a[k][metric]!="NA" and b[k][metric]!="NA"]
            n=len(differences);mean=st.mean(differences) if n else None
            sd=st.stdev(differences) if n>1 else None
            half=float(t.ppf(.975,n-1))*sd/math.sqrt(n) if n>1 else None
            paired.append(dict(scenario=scenario,flow=flow,metric=metric,n_pairs=n,
                mean_routesense_minus_aodv=mean if n else "NA",sd=sd if sd is not None else "NA",
                ci95_low=mean-half if half is not None else "NA",
                ci95_high=mean+half if half is not None else "NA",
                method="paired Student-t; exploratory; no significance claim"))
    write(output/"records.csv",accepted)
    write(output/"summary.csv",summaries)
    write(output/"paired.csv",paired)
    write(output/"excluded.csv",rejected,["path","reason"])
    if historical_rows: write(output/"historical_inventory.csv",historical_rows)
    files=["VERSION","scratch/routesense/custom/routesense.cc","scratch/routesense/baseline/routesense-aodv.cc","scratch/routesense/common/routesense-measurement.h"]
    (output/"source_provenance.json").write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},indent=2))
    print(f"Accepted {len(accepted)} flow records; {len(groups)} scenario/flow/protocol groups; excluded {len(rejected)} files/runs")
    return summaries,paired

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--input",type=Path,action="append",required=True)
    ap.add_argument("--output",type=Path,required=True,help="fresh output directory")
    ap.add_argument("--legacy",type=Path,default=ROOT/"archive/logs/trials")
    args=ap.parse_args()
    analyze(args.input,args.output,args.legacy)

if __name__=="__main__": main()
