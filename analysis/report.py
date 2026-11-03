#!/usr/bin/env python3
"""Aggregate every simulation run into comparison tables and figures."""
import csv
import json
import re
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse import parse_file
from metrics import per_onset, network, summarize, HORIZON_US

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(ROOT, "results")
OUT = os.path.join(ROOT, "analysis", "out")

# Display order and labels.
VARIANT_LABEL = [
    ("legacy",       "Legacy DSME"),
    ("capred",       "DSME + CAP reduction"),
    ("tacfpext",     "TaCFPext (reactive, oracle demand)"),
    ("tacfpext-lv",  "TaCFPext (reactive, learned demand)"),
    ("ptacfpext",    "P-TaCFPext (this work)"),
    ("ptacfpext-or", "P-TaCFPext (oracle demand)"),
    ("abl-notrend",  "ablation: no trend term"),
    ("abl-noonset",  "ablation: no onset detector"),
    ("abl-nosab",    "ablation: no SAB capacity test"),
    ("abl-noearly",  "ablation: no early plain-GTS request"),
]
ORDER = [v for v, _ in VARIANT_LABEL]
_BASE_LABEL = dict(VARIANT_LABEL)

# Sensitivity variants share the base labels above with a parameter note.
_BASE_LABEL.update({
    "tacfpext-t1": "TaCFPext, trigger after 1 failure",
    "tacfpext-t2": "TaCFPext, trigger after 2 failures",
    "pred-h1": "P-TaCFPext, horizon h = 1 superframe",
    "pred-h8": "P-TaCFPext, horizon h = 8 superframes",
    "pred-a02": "P-TaCFPext, alpha = 0.2",
    "pred-a06": "P-TaCFPext, alpha = 0.6",
    "pred-p1": "P-TaCFPext, persistence = 1",
    "pred-p4": "P-TaCFPext, persistence = 4",
})

# A scenario tag is appended to the variant name by run_experiments.py --tag,
# so a second offered-load setting does not overwrite the first.
_TAG_RE = re.compile(r"^(.*)_([a-z]+[0-9]*)$")


def split_tag(variant):
    m = _TAG_RE.match(variant)
    return (m.group(1), m.group(2)) if m else (variant, "")


class _Labels(dict):
    def __missing__(self, key):
        base, _ = split_tag(key)
        return _BASE_LABEL.get(base, base)

    def get(self, key, default=None):
        base, _ = split_tag(key)
        return _BASE_LABEL.get(base, default if default is not None else key)


LABEL = _Labels(_BASE_LABEL)

ONSET_KEYS = ["trigger_latency_ms", "ext_latency_ms",
              "reach_4_ms", "reach_8_ms", "reach_12_ms",
              "burst_drop_ratio", "early_drop_ratio", "burst_pdr",
              "burst_delay_ms", "peak_slots"]


def discover():
    runs = []
    for variant in sorted(os.listdir(RESULTS)):
        vdir = os.path.join(RESULTS, variant)
        if not os.path.isdir(vdir):
            continue
        for run in sorted(os.listdir(vdir)):
            log = os.path.join(vdir, run, "COOJA.testlog")
            if not os.path.exists(log) or os.path.getsize(log) < 1000:
                continue
            nodes, seed = run.lstrip("n").split("_s")
            runs.append((variant, int(nodes), int(seed), log))
    return runs


def load():
    rows, nets = [], []
    for variant, nodes, seed, log in discover():
        recs = parse_file(log)
        for r in per_onset(recs):
            r.update(variant=variant, nodes=nodes, seed=seed)
            rows.append(r)
        n = network(recs)
        if n:
            n.update(variant=variant, nodes=nodes, seed=seed)
            nets.append(n)
    return rows, nets


def agg(rows, nets):
    """Pool onsets across seeds for each (variant, nodes)."""
    out = {}
    keys = sorted({(r["variant"], r["nodes"]) for r in rows} |
                  {(n["variant"], n["nodes"]) for n in nets})
    for k in keys:
        sub = [r for r in rows if (r["variant"], r["nodes"]) == k]
        subn = [n for n in nets if (n["variant"], n["nodes"]) == k]
        entry = {"onsets": len(sub), "runs": len(subn)}
        for key in ONSET_KEYS:
            entry[key] = summarize(sub, key)
        for key in ("pdr", "mean_delay_ms", "max_delay_ms", "throughput_kbps",
                    "fairness", "control_frames", "control_per_delivered",
                    "ext_waste_ratio", "trig_reactive", "trig_predictive",
                    "gts_alloc_fail", "extgts_alloc_ok", "cap_conversions"):
            vals = [n[key] for n in subn if n.get(key) is not None]
            entry[key] = statistics.fmean(vals) if vals else None
        out[k] = entry
    return out


def fmt(v, nd=1):
    if v is None:
        return "-"
    if isinstance(v, float):
        return "{:.{}f}".format(v, nd)
    return str(v)


def text_tables(a, nodes_focus, tag=""):
    lines = []
    def key_order(v):
        base, _ = split_tag(v)
        return ORDER.index(base) if base in ORDER else len(ORDER)
    present = sorted({v for (v, n) in a if n == nodes_focus
                      and split_tag(v)[1] == tag}, key=key_order)
    lines.append("=" * 118)
    lines.append("ONSET RESPONSE  (n = {} nodes, pooled over seeds)".format(nodes_focus))
    lines.append("-" * 118)
    lines.append("{:<38} {:>8} {:>10} {:>9} {:>10} {:>9} {:>9} {:>9}".format(
        "variant", "onsets", "trig_med", "trig_hit", "ext_med", "ext_hit",
        "burstdrop", "burstPDR"))
    for v in present:
        e = a[(v, nodes_focus)]
        t, x = e["trigger_latency_ms"], e["ext_latency_ms"]
        lines.append("{:<38} {:>8} {:>10} {:>8.0%} {:>10} {:>8.0%} {:>9} {:>9}".format(
            LABEL[v][:38], e["onsets"],
            fmt(t.get("median_imputed")), t.get("reach_rate") or 0,
            fmt(x.get("median_imputed")), x.get("reach_rate") or 0,
            fmt(e["burst_drop_ratio"].get("mean_observed"), 3),
            fmt(e["burst_pdr"].get("mean_observed"), 3)))
    lines.append("")
    lines.append("  trig_med / ext_med: median latency in ms from burst onset, with onsets that")
    lines.append("  never reached the event imputed at the {:.1f} s observation horizon.".format(
        HORIZON_US / 1e6))
    lines.append("  trig_hit / ext_hit: fraction of onsets that reached it at all.")
    lines.append("")
    lines.append("=" * 118)
    lines.append("NETWORK TOTALS  (n = {} nodes, mean over seeds)".format(nodes_focus))
    lines.append("-" * 118)
    lines.append("{:<38} {:>7} {:>9} {:>9} {:>8} {:>8} {:>8} {:>8}".format(
        "variant", "PDR", "delay_ms", "thr_kbps", "fair", "ctrl/pkt", "extwast", "trigP"))
    for v in present:
        e = a[(v, nodes_focus)]
        lines.append("{:<38} {:>7} {:>9} {:>9} {:>8} {:>8} {:>8} {:>8}".format(
            LABEL[v][:38], fmt(e["pdr"], 3), fmt(e["mean_delay_ms"]),
            fmt(e["throughput_kbps"]), fmt(e["fairness"], 3),
            fmt(e["control_per_delivered"], 3), fmt(e["ext_waste_ratio"], 3),
            fmt(e["trig_predictive"], 0)))
    lines.append("=" * 118)
    return "\n".join(lines)


def write_csv(a, path):
    keys = sorted(a)
    with open(path, "w", newline="") as f:
        w = None
        for k in keys:
            e = a[k]
            row = {"variant": k[0], "nodes": k[1], "onsets": e["onsets"],
                   "runs": e["runs"]}
            for key in ONSET_KEYS:
                s = e[key]
                row[key + "_median"] = s.get("median_imputed")
                row[key + "_mean"] = s.get("mean_observed")
                row[key + "_reach"] = s.get("reach_rate")
            for key in ("pdr", "mean_delay_ms", "max_delay_ms", "throughput_kbps",
                        "fairness", "control_frames", "control_per_delivered",
                        "ext_waste_ratio", "trig_reactive", "trig_predictive",
                        "gts_alloc_fail", "extgts_alloc_ok", "cap_conversions"):
                row[key] = e[key]
            if w is None:
                w = csv.DictWriter(f, fieldnames=list(row))
                w.writeheader()
            w.writerow(row)


def main():
    os.makedirs(OUT, exist_ok=True)
    rows, nets = load()
    if not rows:
        sys.exit("no results found under " + RESULTS)
    a = agg(rows, nets)
    write_csv(a, os.path.join(OUT, "summary.csv"))
    with open(os.path.join(OUT, "onsets.json"), "w") as f:
        json.dump(rows, f)

    tags = sorted({split_tag(k[0])[1] for k in a})
    report = []
    for tag in tags:
        scen = {
            "": "A. paper load, 21 pkt/MS, bursts of 2 BI in a period of 8",
            "b6": "B. moderate load, 6 pkt/MS, bursts of 2 BI in a period of 8",
            "sb": "C. short bursts, 21 pkt/MS, bursts of 1 BI in a period of 4",
        }.get(tag, "scenario '{}'".format(tag))
        report.append("#" * 118)
        report.append("SCENARIO: " + scen)
        report.append("#" * 118)
        node_counts = sorted({k[1] for k in a if split_tag(k[0])[1] == tag})
        for n in node_counts:
            report.append(text_tables(a, n, tag))
            report.append("")
    text = "\n".join(report)
    with open(os.path.join(OUT, "report.txt"), "w") as f:
        f.write(text)
    print(text)


if __name__ == "__main__":
    main()
