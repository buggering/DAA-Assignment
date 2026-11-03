#!/usr/bin/env python3
"""Build every DSME variant and run the experiment matrix in Cooja.

Each variant is a separate build directory with its own compile-time flags, so
the variants can be rebuilt and run independently without clobbering one
another's object files.
"""
import argparse
import concurrent.futures as cf
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Location of the Contiki-NG checkout. Override with the CONTIKI environment
# variable; the default is a sibling of this repository, then ~/contiki-ng.
def _find_contiki():
    env = os.environ.get("CONTIKI")
    if env:
        return os.path.abspath(os.path.expanduser(env))
    for cand in (os.path.join(os.path.dirname(ROOT), "contiki-ng"),
                 os.path.expanduser("~/contiki-ng")):
        if os.path.isdir(os.path.join(cand, "os", "net", "mac")):
            return cand
    return os.path.expanduser("~/contiki-ng")


CONTIKI = _find_contiki()
COOJA_JAR = os.path.join(CONTIKI, "tools/cooja/build/libs/cooja-full.jar")

# DSME_MODE: 0 legacy, 1 CAP reduction, 2 TaCFPext, 3 P-TaCFPext
# DSME_DEMAND: 0 oracle, 1 last-value, 2 Holt forecast
VARIANTS = {
    # --- baselines -----------------------------------------------------
    "legacy":       dict(DSME_MODE=0, DSME_DEMAND=0),
    "capred":       dict(DSME_MODE=1, DSME_DEMAND=0),
    # TaCFPext exactly as published: reactive 3-failure trigger, and the
    # node is told its own offered load the instant it changes.
    "tacfpext":     dict(DSME_MODE=2, DSME_DEMAND=0),
    # The same protocol without that oracle: demand is the previous
    # multi-superframe's arrival count.
    "tacfpext-lv":  dict(DSME_MODE=2, DSME_DEMAND=1),

    # --- this work -----------------------------------------------------
    "ptacfpext":    dict(DSME_MODE=3, DSME_DEMAND=2),
    # Predictive trigger, but handed the same oracle demand as the published
    # baseline: isolates the trigger from the forecast.
    "ptacfpext-or": dict(DSME_MODE=3, DSME_DEMAND=0),

    # --- ablations -----------------------------------------------------
    # Holt reduced to a single EWMA: no trend term, so no extrapolation.
    "abl-notrend":  dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_USE_TREND=0),
    # No fast/slow divergence detector: the trigger must wait for the
    # persistence counter.
    "abl-noonset":  dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_USE_ONSET=0),
    # No local free-CFP estimate: the node can no longer foresee that a GTS
    # request would be refused, only that demand is growing.
    "abl-nosab":    dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_USE_SAB_CAPACITY=0),
    # Predictive extension only: the node no longer asks for ordinary GTSs
    # ahead of demand. Isolates what that early-request path costs in CAP
    # traffic and buys in allocated capacity.
    "abl-noearly":  dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_USE_EARLY_GTS=0),

    # --- sensitivity ---------------------------------------------------
    # The obvious objection to this work is "just lower the threshold".
    # These two answer it: a hair-trigger reactive TaCFPext converts CAPs on
    # transient refusals, which costs the neighbourhood contention bandwidth
    # and leaves extGTSs idle.
    "tacfpext-t1":  dict(DSME_MODE=2, DSME_DEMAND=0, DSME_FAIL_THRESHOLD=1),
    "tacfpext-t2":  dict(DSME_MODE=2, DSME_DEMAND=0, DSME_FAIL_THRESHOLD=2),
    # Forecast horizon h, in superframes.
    "pred-h1":      dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_HORIZON_SF=1),
    "pred-h8":      dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_HORIZON_SF=8),
    # Holt level gain alpha.
    "pred-a02":     dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_ALPHA=13107),
    "pred-a06":     dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_ALPHA=39322),
    # Persistence required before the slow path fires.
    "pred-p1":      dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_PERSIST=1),
    "pred-p4":      dict(DSME_MODE=3, DSME_DEMAND=2, DSME_PRED_PERSIST=4),
}

SENSITIVITY = ["tacfpext", "tacfpext-t1", "tacfpext-t2", "ptacfpext",
               "pred-h1", "pred-h8", "pred-a02", "pred-a06",
               "pred-p1", "pred-p4"]

DEFAULT_FLAGS = dict(
    DSME_FAIL_THRESHOLD=3,
    DSME_PRED_USE_TREND=1,
    DSME_PRED_USE_ONSET=1,
    DSME_PRED_USE_SAB_CAPACITY=1,
    DSME_PRED_USE_EARLY_GTS=1,
    DSME_PRED_HORIZON_SF=4,
    DSME_PRED_ALPHA=26214,     # 0.40 in Q16.16
    DSME_PRED_PERSIST=2,
    APP_BURST_RATE=21,
    APP_LOW_RATE=1,
    APP_PERIOD_BI=8,
    APP_BURST_BI=2,
)

MAKEFILE = """CONTIKI_PROJECT = node
all: $(CONTIKI_PROJECT)
CONTIKI ?= {contiki}
MODULES_REL += {dsme}
MAKE_MAC = MAKE_MAC_OTHER
MAKE_NET = MAKE_NET_NULLNET
MAKE_ROUTING = MAKE_ROUTING_NULLROUTING
CFLAGS += -DDSME_CONF_MODE={DSME_MODE}
CFLAGS += -DDSME_CONF_DEMAND_SRC={DSME_DEMAND}
CFLAGS += -DDSME_CONF_FAIL_THRESHOLD={DSME_FAIL_THRESHOLD}
CFLAGS += -DDSME_CONF_PRED_HORIZON_SF={DSME_PRED_HORIZON_SF}
CFLAGS += -DDSME_CONF_PRED_ALPHA_Q16={DSME_PRED_ALPHA}
CFLAGS += -DDSME_CONF_PRED_PERSIST={DSME_PRED_PERSIST}
CFLAGS += -DDSME_PRED_USE_TREND={DSME_PRED_USE_TREND}
CFLAGS += -DDSME_PRED_USE_ONSET={DSME_PRED_USE_ONSET}
CFLAGS += -DDSME_PRED_USE_SAB_CAPACITY={DSME_PRED_USE_SAB_CAPACITY}
CFLAGS += -DDSME_PRED_USE_EARLY_GTS={DSME_PRED_USE_EARLY_GTS}
CFLAGS += -DDSME_CONF_LOG_LEVEL=1
CFLAGS += -DAPP_BURST_RATE={APP_BURST_RATE}
CFLAGS += -DAPP_LOW_RATE={APP_LOW_RATE}
CFLAGS += -DAPP_PERIOD_BI={APP_PERIOD_BI}
CFLAGS += -DAPP_BURST_BI={APP_BURST_BI}
CFLAGS += -DAPP_RUN_SECONDS={APP_RUN_SECONDS}
include $(CONTIKI)/Makefile.include

# Cooja compiles each mote type to its own library path, handed to the build
# through LIBNAME in the environment. The objects are already built by
# run_experiments.py, so each simulation only has to place a copy of the
# prebuilt library at the name Cooja expects. That keeps many simulations of
# the same variant from running a real build in this directory at once.
coojalib:
\tcp -f prebuilt.cooja $(LIBNAME)
"""


def variant_dir(name):
    return os.path.join(ROOT, "sim", "variants", name)


def tagged(name, tag):
    """Variant name for a scenario tag, e.g. ptacfpext at burst rate 6."""
    return name if not tag else "{}_{}".format(name, tag)


def build_variant(name, extra, run_seconds, rebuild=False, burst=None, tag="",
                  period_bi=None, burst_bi=None):
    d = variant_dir(tagged(name, tag))
    os.makedirs(d, exist_ok=True)
    for src in ("node.c", "project-conf.h"):
        link = os.path.join(d, src)
        target = os.path.join(ROOT, "app", src)
        if os.path.islink(link) or os.path.exists(link):
            os.remove(link)
        os.symlink(os.path.relpath(target, d), link)

    flags = dict(DEFAULT_FLAGS)
    flags.update(extra)
    flags["APP_RUN_SECONDS"] = run_seconds
    if burst is not None:
        flags["APP_BURST_RATE"] = burst
    if period_bi is not None:
        flags["APP_PERIOD_BI"] = period_bi
    if burst_bi is not None:
        flags["APP_BURST_BI"] = burst_bi
    mk = MAKEFILE.format(contiki=CONTIKI,
                         dsme=os.path.relpath(os.path.join(ROOT, "dsme"), d),
                         **flags)
    mk_path = os.path.join(d, "Makefile")
    old = open(mk_path).read() if os.path.exists(mk_path) else None
    if old != mk or rebuild:
        with open(mk_path, "w") as f:
            f.write(mk)
        subprocess.run(["make", "TARGET=cooja", "clean"], cwd=d,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    r = subprocess.run(["make", "-j4", "TARGET=cooja", "node.cooja"], cwd=d,
                       capture_output=True, text=True)
    fw = os.path.join(d, "build", "cooja", "node.cooja")
    if r.returncode != 0 or not os.path.exists(fw):
        sys.stderr.write("BUILD FAILED {}\n{}\n".format(name, r.stderr[-3000:]))
        return False
    shutil.copyfile(fw, os.path.join(d, "prebuilt.cooja"))
    return True


def run_one(job):
    name, nodes, seed, run_seconds, timeout_ms, itf = job
    outdir = os.path.join(ROOT, "results", name, "n{}_s{}".format(nodes, seed))
    shutil.rmtree(outdir, ignore_errors=True)
    os.makedirs(outdir, exist_ok=True)

    csc = os.path.join(outdir, "sim.csc")
    subprocess.run([sys.executable, os.path.join(ROOT, "sim", "gen_sim.py"),
                    "--nodes", str(nodes), "--seed", str(seed),
                    "--variant", name,
                    "--source", os.path.join(variant_dir(name), "node.c"),
                    "--timeout-ms", str(timeout_ms),
                    "--interference-range", str(itf),
                    "--build-command", "$(MAKE) TARGET=cooja coojalib",
                    "--out", csc], check=True, capture_output=True)

    cmd = ["java", "--enable-preview", "-Xms256m", "-Xmx2g",
           "-jar", COOJA_JAR, "--no-gui",
           "--contiki=" + CONTIKI, "--logdir=" + outdir,
           "--random-seed=" + str(seed), csc]
    with open(os.path.join(outdir, "run.out"), "w") as f:
        # Cooja exits non-zero because the script times out rather than
        # calling testOK(); the log is what matters.
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT,
                       timeout=3600)
    log = os.path.join(outdir, "COOJA.testlog")
    ok = os.path.exists(log) and os.path.getsize(log) > 1000
    return (name, nodes, seed, ok, log if ok else None)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--variants", nargs="*", default=sorted(VARIANTS))
    p.add_argument("--sensitivity", action="store_true",
                   help="run the parameter-sensitivity set instead")
    p.add_argument("--nodes", nargs="*", type=int, default=[21, 41, 61])
    p.add_argument("--seeds", nargs="*", type=int, default=[1, 2, 3])
    p.add_argument("--run-seconds", type=int, default=58)
    p.add_argument("--timeout-ms", type=int, default=60000)
    p.add_argument("--jobs", type=int, default=8)
    p.add_argument("--build-only", action="store_true")
    p.add_argument("--rebuild", action="store_true")
    p.add_argument("--burst", type=int, default=None,
                   help="burst rate in packets per multi-superframe "
                        "(default 21, the value used in Reference C)")
    p.add_argument("--tag", default="",
                   help="suffix for variant and result directory names, so a "
                        "second scenario does not overwrite the first")
    p.add_argument("--interference-range", type=float, default=10.0)
    p.add_argument("--period-bi", type=int, default=None,
                   help="traffic period in beacon intervals (default 8)")
    p.add_argument("--burst-bi", type=int, default=None,
                   help="burst length in beacon intervals (default 2)")
    args = p.parse_args()
    if args.sensitivity:
        args.variants = SENSITIVITY

    for name in args.variants:
        if name not in VARIANTS:
            sys.exit("unknown variant: " + name)
        sys.stderr.write("building {} ... ".format(tagged(name, args.tag)))
        ok = build_variant(name, VARIANTS[name], args.run_seconds, args.rebuild,
                           args.burst, args.tag, args.period_bi, args.burst_bi)
        sys.stderr.write("ok\n" if ok else "FAILED\n")
        if not ok:
            sys.exit(1)
    if args.build_only:
        return

    jobs = [(tagged(v, args.tag), n, s, args.run_seconds, args.timeout_ms,
             args.interference_range)
            for v in args.variants for n in args.nodes for s in args.seeds]
    sys.stderr.write("running {} simulations on {} workers\n".format(len(jobs), args.jobs))
    done = 0
    with cf.ProcessPoolExecutor(max_workers=args.jobs) as ex:
        for res in ex.map(run_one, jobs):
            done += 1
            sys.stderr.write("[{}/{}] {} n={} seed={} {}\n".format(
                done, len(jobs), res[0], res[1], res[2], "ok" if res[3] else "NO LOG"))


if __name__ == "__main__":
    main()
