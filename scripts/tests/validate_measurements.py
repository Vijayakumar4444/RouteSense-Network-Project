"""Small validation suite; writes fresh outputs and never changes historical results."""
import argparse
import csv
import os
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]

def rows(path):
    with path.open() as f:
        return list(csv.DictReader(f))

def run_case(out, protocol, case, dynamic, cross, drain=15, diagnostics=False):
    dest = out / f"{protocol}-{case}"
    dest.mkdir()
    args = (f"{protocol} --simulationTime={25 if dynamic else 6} "
            f"--dynamicScenario={int(dynamic)} --crossTraffic={int(cross)} "
            f"--seed=7 --run=2 --drainTime={drain} --diagnostics={int(diagnostics)} "
            f"--outputDir={dest}")
    env = os.environ.copy()
    if diagnostics:
        env["NS_LOG"] = "ArpL3Protocol=level_logic|prefix_time"
    result = subprocess.run(["./ns3", "run", args], cwd=ROOT, env=env,
                            capture_output=True, text=True)
    (dest / "run.txt").write_text(result.stdout + result.stderr)
    assert result.returncode == 0, f"{args}\n{result.stderr[-3000:]}"
    data = rows(dest / "metrics.csv")
    meta = rows(dest / "provenance.csv")[0]
    assert meta["seed"] == "7" and meta["run"] == "2"
    assert len(data) == (3 if cross else 1)
    expected = 44 if dynamic else 6
    for row in data:
        a, ok, rx = (int(row[k]) for k in ("attempts", "accepted", "unique_rx"))
        assert a == expected and 0 <= rx <= ok <= a, row
        assert int(row["send_failures"]) == a-ok
        assert int(row["app_unreceived"]) == ok-rx
        assert int(row["source_port"]) > 0
        assert int(row["app_drop_observed_unreceived"]) + int(row["app_unresolved"]) == ok-rx
        tx, iprx = int(row["ip_tx"]), int(row["ip_rx"])
        assert tx-iprx == sum(int(row[k]) for k in (
            "ip_reported_drop", "ip_timeout_inferred", "ip_unresolved"))
        if not rx:
            assert row["app_delay_ms"] == row["app_jitter_ms"] == "NA"
        if rx < 2:
            assert row["app_jitter_ms"] == "NA"
    assert ("Topology event" in result.stdout) == dynamic
    print(protocol, case, [(r["attempts"], r["accepted"], r["unique_rx"]) for r in data],
          "unresolved", [r["ip_unresolved"] for r in data], flush=True)
    return dest, data

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for protocol in ("routesense", "routesense-aodv"):
        saved = {}
        for case, dynamic, cross in (
            ("static-single", False, False), ("dynamic-single", True, False),
            ("static-cross", False, True), ("dynamic-cross", True, True)):
            saved[case] = run_case(out, protocol, case, dynamic, cross,
                                   diagnostics=cross)
        repeat, _ = run_case(out, protocol, "repeat", True, True)
        original = saved["dynamic-cross"][0]
        for name in ("metrics.csv", "provenance.csv"):
            assert (repeat/name).read_bytes() == (original/name).read_bytes(), name
        assert next(repeat.glob("*flowmon.xml")).read_bytes() == next(original.glob("*flowmon.xml")).read_bytes()
        short, short_data = run_case(out, protocol, "drain-1", True, True, drain=1)
        long, long_data = run_case(out, protocol, "drain-30", True, True, drain=30)
        base_data = saved["dynamic-cross"][1]
        for b, l in zip(base_data, long_data):
            for k in ("attempts", "accepted", "unique_rx", "ip_rx", "ip_reported_drop"):
                assert b[k] == l[k], (protocol, "drain did not converge", k)
        print(protocol, "repeat and drain-15/30 convergence PASS", flush=True)
    print("All validation checks PASS", flush=True)

if __name__ == "__main__":
    main()
