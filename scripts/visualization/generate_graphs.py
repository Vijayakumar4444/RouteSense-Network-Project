"""Legacy schema graph generator; never use it as authoritative evaluation."""
import argparse

import pandas as pd
import matplotlib.pyplot as plt
from pathlib import Path

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description="Legacy plots only; no schema-2 metric inference")
parser.add_argument("--csv", type=Path, required=True, help="explicit legacy CSV")
parser.add_argument("--output", type=Path, required=True, help="new graph directory")
args = parser.parse_args()
csv_file = args.csv
output_dir = args.output
output_dir.mkdir(parents=True, exist_ok=False)

df = pd.read_csv(csv_file)

metrics = {
    "pdr": ("Packet Delivery Ratio (%)", "PDR"),
    "loss": ("Packet Loss Ratio (%)", "Packet Loss"),
    "throughput_kbps": ("Throughput (Kbps)", "Throughput"),
    "delay_ms": ("Average End-to-End Delay (ms)", "Delay"),
    "jitter_ms": ("Average Jitter (ms)", "Jitter"),
}

# Preserve legacy classification; provenance is not verified.
df = df[df["scenario"] == "static_single"].copy()

if df.empty:
    raise SystemExit(
        "No static_single records found. Check the scenario column "
        "in results/comparison_results.csv."
    )

for column, (ylabel, title) in metrics.items():
    if column not in df.columns:
        print(f"Skipping {title}: column '{column}' not found")
        continue

    plot_data = df.dropna(subset=[column])

    if plot_data.empty:
        print(f"Skipping {title}: no numeric values")
        continue

    summary = plot_data.groupby("protocol")[column].agg(["mean", "std"])
    summary = summary.reindex(["RouteSense", "AODV"]).dropna(subset=["mean"])

    if summary.empty:
        print(f"Skipping {title}: no protocol data")
        continue

    fig, ax = plt.subplots(figsize=(7, 5))

    ax.bar(
        summary.index,
        summary["mean"],
        yerr=summary["std"].fillna(0),
        capsize=5
    )

    ax.set_title(f"RouteSense vs AODV: {title}")
    ax.set_ylabel(ylabel)
    ax.grid(axis="y", linestyle="--", alpha=0.4)

    fig.tight_layout()
    filename = output_dir / f"{column}_comparison.png"
    fig.savefig(filename, dpi=300)
    plt.close(fig)

    print(f"Saved: {filename}")

print("Graph generation completed.")

