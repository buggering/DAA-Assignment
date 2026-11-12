#!/usr/bin/env python3
"""Classroom demo: TaCFPext (reactive) against P-TaCFPext (predictive).

    python3 demo/demo.py gui reactive        open Cooja with the reactive trigger
    python3 demo/demo.py gui predictive      ... with the predictive trigger
    python3 demo/demo.py compare             run both headless, print a table

Options (all commands):
    --load paper|moderate   paper = Reference C's burst of 21 packets per
                            multi-superframe (default); moderate = 6, where
                            allocation failures are transient
    --nodes N               default 21, the network the paper evaluates
    --seed S                default 1
GUI only:
    --speed X               Cooja speed limit, 1.0 = real time (default);
                            0.5 runs at half speed, which is easier to narrate
    --autostart             start the simulation immediately

The firmware is the experiment firmware with APP_DEMO_LEDS=1, which only
drives the mote LEDs and colours; the protocol code is the same, so a GUI run
and a headless run of the same variant and seed produce the same events.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "sim"))
sys.path.insert(0, os.path.join(ROOT, "analysis"))

import run_experiments as rx           # noqa: E402
from parse import parse_file           # noqa: E402
from metrics import per_onset, network, summarize, HORIZON_US  # noqa: E402

RUN_SECONDS = 58
TIMEOUT_MS = 60000

ARMS = {
    "reactive":   dict(base="tacfpext",  label="TaCFPext (reactive, 3 refusals)"),
    "predictive": dict(base="ptacfpext", label="P-TaCFPext (predictive forecast)"),
}
LOADS = {"paper": None, "moderate": 6}


def variant_name(arm, load):
    name = "demo-" + ARMS[arm]["base"]
    return name if load == "paper" else name + "_b6"


def build(arm, load):
    base = ARMS[arm]["base"]
    extra = dict(rx.VARIANTS[base])
    extra["APP_DEMO_LEDS"] = 1
    tag = "" if load == "paper" else "b6"
    name = "demo-" + base
    sys.stderr.write("building {} ... ".format(rx.tagged(name, tag)))
    sys.stderr.flush()
    ok = rx.build_variant(name, extra, RUN_SECONDS, burst=LOADS[load], tag=tag)
    sys.stderr.write("ok\n" if ok else "FAILED\n")
    if not ok:
        sys.exit(1)
    flags = dict(rx.DEFAULT_FLAGS)
    flags.update(extra)
    return rx.tagged(name, tag), flags


def check_cooja():
    if not os.path.exists(rx.COOJA_JAR):
        sys.exit("Cooja is not built: {} is missing.\n"
                 "Build it with:  cd {}/tools/cooja && ./gradlew build -x test"
                 .format(rx.COOJA_JAR, rx.CONTIKI))


def cmd_gui(args):
    check_cooja()
    name, flags = build(args.arm, args.load)
    outdir = os.path.join(ROOT, "results", "demo-gui", name)
    os.makedirs(outdir, exist_ok=True)
    csc = os.path.join(outdir, "demo.csc")
    label = ARMS[args.arm]["label"]
    if args.load == "moderate":
        label += ", moderate load"
    subprocess.run([sys.executable, os.path.join(ROOT, "sim", "gen_sim.py"),
                    "--gui", "--label", label,
                    "--mode", str(flags["DSME_MODE"]),
                    "--fail-threshold", str(flags["DSME_FAIL_THRESHOLD"]),
                    "--speed", str(args.speed),
                    "--nodes", str(args.nodes), "--seed", str(args.seed),
                    "--variant", name,
                    "--source", os.path.join(rx.variant_dir(name), "node.c"),
                    "--timeout-ms", str(TIMEOUT_MS),
                    "--build-command", "$(MAKE) TARGET=cooja coojalib",
                    "--out", csc], check=True, stdout=subprocess.DEVNULL)
    cmd = ["java", "--enable-preview", "-Xms256m", "-Xmx2g",
           "-jar", rx.COOJA_JAR, "--gui", "--contiki=" + rx.CONTIKI,
           "--logdir=" + outdir, "--random-seed=" + str(args.seed)]
    if args.autostart:
        cmd.append("--autostart")
    cmd.append(csc)
    print("Opening Cooja: {}".format(label))
    print("  press Start in the Cooja toolbar to run; "
          "the run ends by itself after 60 simulated seconds.")
    return subprocess.call(cmd)


def fmt(v, kind):
    if v is None:
        return "-"
    if kind == "pct":
        return "{:.0f} %".format(100 * v)
    if kind == "ms":
        return "{:.0f} ms".format(v)
    if kind == "ratio":
        return "{:.3f}".format(v)
    if kind == "f1":
        return "{:.1f}".format(v)
    if kind == "f2":
        return "{:.2f}".format(v)
    return str(v)


def metrics_for(log):
    recs = parse_file(log)
    rows = per_onset(recs)
    net = network(recs)
    trig = summarize(rows, "trigger_latency_ms")
    ext = summarize(rows, "ext_latency_ms")
    drops = [r["burst_drop_ratio"] for r in rows if "burst_drop_ratio" in r]
    early = [r["early_drop_ratio"] for r in rows if "early_drop_ratio" in r]
    delay = [r["burst_delay_ms"] for r in rows if r.get("burst_delay_ms") is not None]
    return {
        "bursts": trig["n"],
        "trig_rate": trig.get("reach_rate"),
        "trig_med": trig.get("median_imputed"),
        "ext_rate": ext.get("reach_rate"),
        "ext_med": ext.get("median_imputed"),
        "early_drop": sum(early) / len(early) if early else None,
        "burst_drop": sum(drops) / len(drops) if drops else None,
        "burst_delay": sum(delay) / len(delay) if delay else None,
        "pdr": net.get("pdr"),
        "delay": net.get("mean_delay_ms"),
        "kbps": net.get("throughput_kbps"),
        "ctrl": net.get("control_per_delivered"),
    }


def cmd_compare(args):
    check_cooja()
    results = {}
    for arm in ("reactive", "predictive"):
        name, _ = build(arm, args.load)
        sys.stderr.write("running {} ({} nodes, seed {}, {} s simulated) ... "
                         .format(name, args.nodes, args.seed, TIMEOUT_MS // 1000))
        sys.stderr.flush()
        # 10 m interference range: the sweep's default (sim/gen_sim.py).
        _, _, _, ok, log = rx.run_one((name, args.nodes, args.seed, RUN_SECONDS,
                                       TIMEOUT_MS, 10.0))
        sys.stderr.write("ok\n" if ok else "FAILED\n")
        if not ok:
            sys.exit("simulation failed; see results/{}/n{}_s{}/run.out"
                     .format(name, args.nodes, args.seed))
        results[arm] = metrics_for(log)

    never = "{:.0f} ms".format(HORIZON_US / 1000)
    rows = [
        ("Bursts measured (after 5 s warm-up)", "bursts", None),
        ("Bursts at which the trigger fired", "trig_rate", "pct"),
        ("Median burst start -> trigger *", "trig_med", "ms"),
        ("Bursts that got an extended-CFP slot", "ext_rate", "pct"),
        ("Median burst start -> extCFP slot *", "ext_med", "ms"),
        ("Drop ratio, first 1.5 s of a burst", "early_drop", "ratio"),
        ("Drop ratio, whole burst", "burst_drop", "ratio"),
        ("Mean delay during bursts", "burst_delay", "ms"),
        ("Delivery ratio (whole run)", "pdr", "ratio"),
        ("Throughput, kbit/s", "kbps", "f1"),
        ("Control frames per delivered packet", "ctrl", "f2"),
    ]
    a, b = results["reactive"], results["predictive"]
    w0, w = 40, 20
    load = ("paper load: burst of 21 packets per multi-superframe"
            if args.load == "paper" else
            "moderate load: burst of 6 packets per multi-superframe")
    print()
    print("Onset-to-allocation comparison  -  {} nodes, seed {}, {}"
          .format(args.nodes, args.seed, load))
    print("=" * (w0 + 2 * w))
    print("{:<{}}{:>{}}{:>{}}".format("", w0, "TaCFPext", w, "P-TaCFPext", w))
    print("{:<{}}{:>{}}{:>{}}".format("", w0, "(reactive)", w, "(predictive)", w))
    print("-" * (w0 + 2 * w))
    for title, key, kind in rows:
        print("{:<{}}{:>{}}{:>{}}".format(title, w0, fmt(a[key], kind), w,
                                          fmt(b[key], kind), w))
    print("-" * (w0 + 2 * w))
    print("* a burst that never reached the event is counted at {} (the"
          .format(never))
    print("  observation horizon), so failures are not hidden by the median.")
    print("One seed only. The report's figures are means over three seeds.")
    print()


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("gui", help="open the Cooja GUI for one trigger")
    g.add_argument("arm", choices=sorted(ARMS))
    g.add_argument("--speed", type=float, default=1.0)
    g.add_argument("--autostart", action="store_true")
    c = sub.add_parser("compare", help="run both triggers headless and compare")
    for sp in (g, c):
        sp.add_argument("--load", choices=sorted(LOADS), default="paper")
        sp.add_argument("--nodes", type=int, default=21)
        sp.add_argument("--seed", type=int, default=1)
    args = p.parse_args()
    if args.cmd == "gui":
        sys.exit(cmd_gui(args))
    cmd_compare(args)


if __name__ == "__main__":
    main()
