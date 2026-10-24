"""Resolve frozen evidence paths without editing historical CSVs."""
import argparse
import csv
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def resolve_path(value):
    path=Path(value)
    relative=path.relative_to(ROOT) if path.is_absolute() and path.is_relative_to(ROOT) else path
    mapping=json.loads((ROOT/"docs/organization/path-map.json").read_text())
    for old,new in sorted(mapping.items(),key=lambda item:len(item[0]),reverse=True):
        try: suffix=relative.relative_to(old)
        except ValueError: continue
        result=ROOT/new/suffix
        if not result.exists(): raise FileNotFoundError(result)
        return result
    result=path if path.is_absolute() else ROOT/path
    if not result.exists(): raise FileNotFoundError(result)
    return result
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("input",type=Path);ap.add_argument("output",type=Path)
    args=ap.parse_args()
    with args.input.open() as f:
        reader=csv.DictReader(f);fields=reader.fieldnames;rows=list(reader)
    for row in rows:
        for key in ("directory","path","file"):
            if key in row: row["resolved_"+key]=str(resolve_path(row[key]))
    with args.output.open("x",newline="") as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]) if rows else fields)
        writer.writeheader();writer.writerows(rows)
if __name__=="__main__": main()
