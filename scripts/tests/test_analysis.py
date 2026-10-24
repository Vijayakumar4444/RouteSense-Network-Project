"""Regression checks for extraction, exclusions, duplicate handling and pairing."""
import csv
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("analysis",ROOT/"scripts/analysis/analyze_results.py")
analysis=importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)
class Tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()
    def copy(self,name="sample",protocol="routesense",case="dynamic-cross"):
        dest=self.root/name;dest.mkdir()
        source=ROOT/"results/generated/validation-final"/f"{protocol}-{case}"
        for pattern in ("metrics.csv","provenance.csv","*flowmon.xml"):
            for p in source.glob(pattern): shutil.copy2(p,dest/p.name)
        return dest
    def change(self,folder,key,value,row=0):
        path=folder/"metrics.csv";data=analysis.read(path);data[row][key]=value
        with path.open("w",newline="") as f:
            w=csv.DictWriter(f,fieldnames=data[0]);w.writeheader();w.writerows(data)
    def test_real_endpoints_flags_xml_rounding_and_na(self):
        path=self.copy("misleading-static-alone-name")
        rows=analysis.validate(path)
        self.assertEqual([r["flow"] for r in rows],["main","cross_1","cross_2"])
        self.assertTrue(all(r["scenario"]=="dynamic_cross" for r in rows))
        self.assertEqual(rows[1]["app_delay_ms"],"NA")
    def test_undefined_cannot_be_zero(self):
        p=self.copy();self.change(p,"app_delay_ms","0",1)
        with self.assertRaisesRegex(ValueError,"undefined"): analysis.validate(p)
    def test_wrong_endpoint(self):
        p=self.copy();self.change(p,"destination","10.1.1.7")
        with self.assertRaisesRegex(ValueError,"endpoint"): analysis.validate(p)
    def test_wrong_packet_accounting(self):
        p=self.copy();self.change(p,"accepted","43")
        with self.assertRaises(ValueError): analysis.validate(p)
    def test_wrong_units(self):
        p=self.copy();self.change(p,"ip_throughput_kbps","10008.2162162")
        with self.assertRaisesRegex(ValueError,"throughput"): analysis.validate(p)
    def test_missing_and_negative_metrics(self):
        p=self.copy();self.change(p,"app_delay_ms","")
        with self.assertRaises(ValueError): analysis.validate(p)
        self.change(p,"app_delay_ms","-1")
        with self.assertRaises(ValueError): analysis.validate(p)
    def test_xml_packet_mismatch(self):
        p=self.copy();xml=next(p.glob("*flowmon.xml"))
        xml.write_text(xml.read_text().replace('txPackets="44"','txPackets="45"',1))
        with self.assertRaisesRegex(ValueError,"XML packet"): analysis.validate(p)
    def test_deduplication(self):
        self.copy("one");self.copy("two")
        summaries,paired=analysis.analyze([self.root],self.root/"output")
        self.assertTrue(all(s["n_trials"]==1 for s in summaries))
        self.assertTrue(all(s["sd"]=="NA" for s in summaries))
        excluded=analysis.read(self.root/"output/excluded.csv")
        self.assertEqual(len(excluded),1)
        self.assertIn("duplicate execution",excluded[0]["reason"])
    def test_conflicting_duplicates_exclude_both(self):
        self.copy("one");p=self.copy("two");self.copy("three")
        self.change(p,"app_delay_ms","7.4")
        self.copy("baseline",protocol="routesense-aodv")
        analysis.analyze([self.root],self.root/"output")
        rows=analysis.read(self.root/"output/records.csv")
        self.assertEqual({r["protocol"] for r in rows},{"routesense-aodv"})
    def test_pair_means_and_counts(self):
        folder=ROOT/"results/authoritative/comparison"
        records=analysis.read(folder/"records.csv")
        for p in analysis.read(folder/"paired.csv"):
            groups={}
            for r in records:
                if (r["scenario"],r["flow"])==(p["scenario"],p["flow"]):
                    groups.setdefault((r["seed"],r["run"]),{})[r["protocol"]]=r
            differences=[]
            for group in groups.values():
                a,b=(group[k][p["metric"]] for k in ("routesense","routesense-aodv"))
                if a!="NA" and b!="NA": differences.append(float(a)-float(b))
            self.assertEqual(int(p["n_pairs"]),len(differences))
            if differences: self.assertAlmostEqual(float(p["mean_routesense_minus_aodv"]),sum(differences)/len(differences))

if __name__=="__main__": unittest.main()
