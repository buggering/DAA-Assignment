#!/usr/bin/env python3
"""Figures for the DSME CFP-extension study."""
import os
import sys
import statistics
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from parse import parse_file
from metrics import (per_onset, network, summarize, onset_triggered_average,
                     BI_US, HORIZON_US)
from report import discover, LABEL, ORDER, split_tag

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "analysis", "out")

MAIN = ["legacy", "capred", "tacfpext", "ptacfpext"]
ABLATIONS = ["ptacfpext", "abl-notrend", "abl-noonset", "abl-nosab", "tacfpext"]
COLOR = {
    "legacy": "#888888",
    "capred": "#4C72B0",
    "tacfpext": "#DD8452",
    "tacfpext-lv": "#C44E52",
    "ptacfpext": "#55A868",
    "ptacfpext-or": "#8172B3",
    "abl-notrend": "#937860",
    "abl-noonset": "#DA8BC3",
    "abl-nosab": "#8C8C8C",
}
STYLE = {v: ("-" if v in ("ptacfpext", "tacfpext") else "--") for v in COLOR}


def _style(ax, xlabel, ylabel, title):
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title, fontsize=11)
    ax.grid(alpha=0.3, linewidth=0.6)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)


def load_all(tag=""):
    onsets = defaultdict(list)     # (variant, nodes) -> rows
    nets = defaultdict(list)
    logs = defaultdict(list)       # (variant, nodes) -> log paths
    for variant_full, nodes, seed, log in discover():
        variant, vtag = split_tag(variant_full)
        if vtag != tag:
            continue
        recs = parse_file(log)
        onsets[(variant, nodes)].extend(per_onset(recs))
        n = network(recs)
        if n:
            nets[(variant, nodes)].append(n)
        logs[(variant, nodes)].append(log)
    return onsets, nets, logs


def fig_trigger_cdf(onsets, nodes, path):
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2))
    horizon_ms = HORIZON_US / 1000.0
    for ax, key, title in (
            (axes[0], "trigger_latency_ms",
             "Onset to extCFP trigger"),
            (axes[1], "ext_latency_ms",
             "Onset to first extGTS in service")):
        for v in MAIN + ["abl-notrend", "abl-noonset", "abl-nosab"]:
            rows = onsets.get((v, nodes))
            if not rows or key not in rows[0]:
                continue
            vals = sorted(r[key] for r in rows if r.get(key) is not None)
            censored = sum(1 for r in rows if key in r and r[key] is None)
            if not vals:
                continue
            total = len(vals) + censored
            xs, ys = [0.0], [0.0]
            for i, x in enumerate(vals):
                xs.append(x)
                ys.append((i + 1) / total)
            # Flat to the horizon: the censored onsets never got there.
            xs.append(horizon_ms)
            ys.append(len(vals) / total)
            ax.step(xs, ys, where="post", label=LABEL[v],
                    color=COLOR[v], linestyle=STYLE[v],
                    linewidth=2.0 if v in ("ptacfpext", "tacfpext") else 1.3)
        ax.set_xlim(0, horizon_ms)
        ax.set_ylim(0, 1.0)
        _style(ax, "latency after burst onset (ms)",
               "fraction of onsets reached", title)
    axes[0].legend(fontsize=7.5, loc="upper left", framealpha=0.9,
                   bbox_to_anchor=(0.02, 0.98))
    fig.suptitle("Reaching the CFP extension after a burst starts "
                 "(n = {} nodes; curves end below 1.0 where onsets never "
                 "reached the event)".format(nodes), fontsize=11)
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def fig_onset_trajectory(logs, nodes, path):
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.0))
    for v in MAIN:
        paths = logs.get((v, nodes))
        if not paths:
            continue
        curves = [onset_triggered_average(parse_file(p)) for p in paths]
        if not curves or curves[0]["n_onsets"] == 0:
            continue
        t = curves[0]["t_s"]

        def mean_of(key):
            out = []
            for i in range(len(t)):
                vals = [c[key][i] for c in curves
                        if i < len(c[key]) and c[key][i] is not None]
                out.append(statistics.fmean(vals) if vals else None)
            return out

        kw = dict(color=COLOR[v], linestyle=STYLE[v], label=LABEL[v],
                  linewidth=2.0 if v in ("ptacfpext", "tacfpext") else 1.3)
        axes[0].plot(t, mean_of("slots"), **kw)
        axes[1].plot(t, mean_of("queue"), **kw)
        axes[2].plot(t, mean_of("drops"), **kw)

    burst_s = 2 * BI_US / 1e6
    for ax in axes:
        ax.axvspan(0, burst_s, color="#FFD9A0", alpha=0.45, zorder=0)
        ax.axvline(0, color="#B5651D", linewidth=1.0, zorder=1)
    _style(axes[0], "time relative to burst onset (s)",
           "TX slots allocated", "Allocated capacity")
    _style(axes[1], "time relative to burst onset (s)",
           "packets queued", "Backlog")
    _style(axes[2], "time relative to burst onset (s)",
           "packets dropped per superframe", "Deadline misses")
    axes[0].legend(fontsize=8, loc="upper left")
    fig.suptitle("Onset-triggered average over every burst "
                 "(n = {} nodes; shaded band is the burst)".format(nodes),
                 fontsize=11)
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def fig_scaling(onsets, nets, path):
    node_counts = sorted({k[1] for k in nets})
    fig, axes = plt.subplots(2, 3, figsize=(14, 7.5))
    panels = [
        ("pdr", "delivery ratio", "Packet delivery ratio", False),
        ("mean_delay_ms", "mean delay (ms)", "Delay of delivered packets", False),
        ("throughput_kbps", "throughput (kbit/s)", "Aggregate throughput", False),
        ("fairness", "Jain fairness index", "Fairness across senders", False),
        ("control_per_delivered", "command frames / delivered packet",
         "Control overhead", False),
        ("ext_waste_ratio", "idle share of reserved extGTS",
         "Over-reservation cost", False),
    ]
    for ax, (key, ylab, title, _) in zip(axes.flat, panels):
        for v in MAIN:
            xs, ys = [], []
            for n in node_counts:
                runs = nets.get((v, n))
                if not runs:
                    continue
                vals = [r[key] for r in runs if r.get(key) is not None]
                if vals:
                    xs.append(n)
                    ys.append(statistics.fmean(vals))
            if xs:
                ax.plot(xs, ys, marker="o", markersize=4, color=COLOR[v],
                        linestyle=STYLE[v], label=LABEL[v],
                        linewidth=2.0 if v in ("ptacfpext", "tacfpext") else 1.3)
        _style(ax, "number of nodes", ylab, title)
    axes.flat[0].legend(fontsize=8)
    fig.suptitle("Network-wide performance against network size", fontsize=12)
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def fig_burst_scaling(onsets, path):
    node_counts = sorted({k[1] for k in onsets})
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.0))
    panels = [("trigger_latency_ms", "median trigger latency (ms)",
               "Time to trigger the extension"),
              ("burst_drop_ratio", "drop ratio during burst",
               "Losses while the burst lasts"),
              ("burst_pdr", "delivery ratio during burst",
               "Packets delivered during the burst")]
    for ax, (key, ylab, title) in zip(axes, panels):
        for v in MAIN:
            xs, ys = [], []
            for n in node_counts:
                rows = onsets.get((v, n))
                if not rows:
                    continue
                s = summarize(rows, key)
                val = (s.get("median_imputed") if key.endswith("_ms")
                       else s.get("mean_observed"))
                if val is not None:
                    xs.append(n)
                    ys.append(val)
            if xs:
                ax.plot(xs, ys, marker="o", markersize=4, color=COLOR[v],
                        linestyle=STYLE[v], label=LABEL[v],
                        linewidth=2.0 if v in ("ptacfpext", "tacfpext") else 1.3)
        _style(ax, "number of nodes", ylab, title)
    axes[0].legend(fontsize=8)
    fig.suptitle("Burst response against network size", fontsize=12)
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def fig_ablation(onsets, nodes, path):
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.0))
    present = [v for v in ABLATIONS if (v, nodes) in onsets]
    labels = [LABEL[v].replace("ablation: ", "no ") for v in present]
    x = range(len(present))

    med = [summarize(onsets[(v, nodes)], "trigger_latency_ms").get("median_imputed") or 0
           for v in present]
    hit = [(summarize(onsets[(v, nodes)], "trigger_latency_ms").get("reach_rate") or 0) * 100
           for v in present]
    drop = [(summarize(onsets[(v, nodes)], "burst_drop_ratio").get("mean_observed") or 0)
            for v in present]

    for ax, vals, ylab, title in (
            (axes[0], med, "median trigger latency (ms)", "Trigger latency"),
            (axes[1], hit, "onsets that triggered (%)", "Trigger coverage"),
            (axes[2], drop, "drop ratio during burst", "Burst losses")):
        ax.bar(x, vals, color=[COLOR[v] for v in present], width=0.62)
        ax.set_xticks(list(x))
        ax.set_xticklabels(labels, rotation=28, ha="right", fontsize=8)
        _style(ax, "", ylab, title)
    fig.suptitle("Contribution of each component of the predictive trigger "
                 "(n = {} nodes)".format(nodes), fontsize=11)
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def main():
    os.makedirs(OUT, exist_ok=True)
    tags = sorted({split_tag(v)[1] for v, _, _, _ in discover()})
    for tag in tags:
        onsets, nets, logs = load_all(tag)
        if not nets:
            continue
        node_counts = sorted({k[1] for k in nets})
        focus = 41 if 41 in node_counts else node_counts[len(node_counts) // 2]
        sfx = "_{}".format(tag) if tag else ""
        print("scenario '{}': node counts {}, focus {}".format(
            tag or "paper load", node_counts, focus))
        for n in node_counts:
            tagn = "{}_n{}".format(sfx, n)
            fig_trigger_cdf(onsets, n, os.path.join(OUT, "fig1_trigger_cdf%s.png" % tagn))
            fig_onset_trajectory(logs, n, os.path.join(OUT, "fig2_onset_trajectory%s.png" % tagn))
        fig_trigger_cdf(onsets, focus, os.path.join(OUT, "fig1_trigger_cdf%s.png" % sfx))
        fig_onset_trajectory(logs, focus, os.path.join(OUT, "fig2_onset_trajectory%s.png" % sfx))
        fig_burst_scaling(onsets, os.path.join(OUT, "fig3_burst_scaling%s.png" % sfx))
        fig_scaling(onsets, nets, os.path.join(OUT, "fig4_network_scaling%s.png" % sfx))
        fig_ablation(onsets, focus, os.path.join(OUT, "fig5_ablation%s.png" % sfx))
    print("figures written to", OUT)


if __name__ == "__main__":
    main()
