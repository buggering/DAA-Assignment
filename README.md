# Predictive CFP Extension for IEEE 802.15.4 DSME in Contiki-NG

A full IEEE 802.15.4 DSME MAC for Contiki-NG, the TaCFPext CFP-extension
protocol on top of it, and **P-TaCFPext** — a replacement for TaCFPext's
reactive trigger with a predictive, EWMA-based one.

Everything runs in Cooja on Contiki-NG Cooja motes. The DSME MAC is a new,
self-contained module; **no file in the Contiki-NG tree is modified**.

---

## 1. The problem

TaCFPext¹ lets a node borrow a contention access period (CAP) and use it as an
extended contention-free period (extCFP) when the CFP of the current
multi-superframe cannot accommodate its traffic. The mechanism works, but its
trigger is purely reactive: conversion begins only after the node has failed to
allocate a GTS `2^(MO−SO)/k` times in a row — three consecutive failures in the
configuration the paper evaluates.

By then the damage is done. Each failed allocation costs a full
`macMaxFrameTotalWaitTime`, and the extGTS negotiation that follows is three
more command frames through a contended CAP. In a bursty industrial workload
that recovery window recurs at *every* burst onset, and it is long enough to
swallow most of the burst.

Measured here, that is exactly what happens. Under the paper's own dynamic
scenario on a 21-node network, the median time from a burst starting to
TaCFPext even *beginning* to convert a CAP is **1.35 s** — and at 30 % of
burst onsets it never converts at all. At a moderate offered load, where
allocation failures are transient rather than permanent, the trigger reaches
three consecutive failures at only **22 %** of onsets: TaCFPext spends most of
its time behaving like plain DSME.

## 2. The contribution

`dsme/dsme-predictor.c` replaces the fixed failure counter with a forecast.
Two observations drive it.

**A node can forecast its own demand.** Arrivals are sampled once per
superframe and smoothed with Holt's linear-trend method — two coupled EWMAs,
one tracking the level `L` and one the slope `T` — so the estimate extrapolates
`h` superframes ahead as `L + h·T` instead of merely tracking the past. A
single EWMA cannot do this: it lags a step change by roughly `1/α` samples,
which is the very delay we are trying to remove. A second, independent
fast/slow EWMA pair acts as a burst-onset detector; when the fast average pulls
away from the slow one the ramp has started, and that fires within one or two
samples, before the trend term has had time to build.

**A node can predict the failure itself, with no extra signalling.** The slot
allocation bitmap (SAB) that a node already maintains by overhearing its
one-hop neighbours records exactly which GTS positions in the multi-superframe
are taken. If forecast demand exceeds what is still free there, the next GTS
Request is *already known* to come back "disapproved — lack of availability".
TaCFPext holds this information and throws it away, choosing instead to learn
the same fact three handshakes later. P-TaCFPext reads it directly and goes
straight to changeable-CAP selection.

The published reactive trigger is kept underneath as a backstop, so
P-TaCFPext degrades into TaCFPext — never below it — when a burst genuinely
cannot be predicted.

Acting early costs nothing when the prediction is right and costs one reserved
extGTS when it is wrong, so the trigger is damped three ways: a deficit must
persist for `DSME_PRED_PERSIST` superframes (the onset detector deliberately
bypasses this), a cooldown follows every conversion, and unused extGTSs expire
after `macDsmeGtsExpirationTime/2` idle multi-superframes as in TaCFPext. Idle
extGTS slot-instances are counted and reported, so the cost of a mispredicted
burst is visible rather than hidden.

All estimator arithmetic is Q16.16 fixed point — no FPU is assumed.

## 3. What it does

Full tables are in [docs/RESULTS.md](docs/RESULTS.md); these are the headline
figures, each a mean over three seeds.

**Against the published TaCFPext, at the paper's own offered load (n = 21):**

| | TaCFPext | P-TaCFPext |
|---|---:|---:|
| median time from burst onset to trigger | 1355 ms | **128 ms** |
| burst onsets that ever trigger | 70 % | **98 %** |
| median time to the first extGTS in service | never (55 % of onsets) | **1001 ms** |
| delivery ratio | 0.386 | **0.450** |
| aggregate throughput | 42.6 kbit/s | **49.7 kbit/s** |

**At a moderate load, where allocation failures are transient (n = 21):**

| | legacy DSME | TaCFPext | P-TaCFPext |
|---|---:|---:|---:|
| burst onsets that ever trigger | – | 22 % | **78 %** |
| drop ratio during the burst | 0.217 | 0.202 | **0.082** |
| delivery ratio | 0.750 | 0.766 | **0.884** |
| mean delay | 276 ms | 272 ms | **217 ms** |

This is the regime the aim names, and it is where the reactive trigger is
weakest: at 22 % coverage TaCFPext barely improves on plain DSME, while the
forecast cuts burst losses by **59 %** and delay by **20 %**.

**What does not work, reported as found.** The obvious cheap alternative to a
forecast is to lower the threshold from three failures to one. Under the
paper's *sustained* overload that alternative is simply better than predicting:
a single refusal is already a reliable signal when the overload never goes
away, and it costs a third of the control traffic. P-TaCFPext beats it only
where failures are transient and the network is small (n = 21 at moderate
load, where it wins on drops, delay and delivery alike). From n = 41 upward the
extra CAP traffic the estimator generates — 2.3 command frames per delivered
packet against 0.75 — costs more than the earlier trigger buys. The estimator
predicts *when* to extend well; it does not yet use the same prediction to
suppress the ordinary GTS requests it can already tell will be refused, and
that is where the overhead goes.

## 4. What was built

```
dsme/                        the MAC (a Contiki-NG MODULES_REL module)
  dsme-conf.h                orders, thresholds, estimator gains, ablation switches
  dsme-const.h               multi-superframe geometry and slot timing, all derived
  dsme-slot-operation.c      rtimer-driven beacon / CAP / CFP / extCFP state machine
  dsme-packet.c              beacon and GTS / extGTS command frames
  dsme-sab.c                 SAB, taSAB, CCB bitmaps and the slot geometry they index
  dsme-act.c                 ACT / taACT, idle counters, expiry
  dsme-gts.c                 three-way allocation handshake, deallocation, triggers
  dsme-tacfpext.c            Algorithm 1 (changeable CAP selection), Algorithm 2 (CCB)
  dsme-predictor.c           the predictive estimator  <-- the contribution
  dsme-queue.c               data and command FIFOs
app/node.c                   alternating low/burst traffic generator
sim/gen_sim.py               Cooja .csc generator (topology, radio medium, log script)
sim/run_experiments.py       builds every variant, runs the matrix in parallel
tests/test_predictor.c       host-compiled unit tests for the estimator
analysis/parse.py            log -> structured events
analysis/metrics.py          onset-response and network metrics
analysis/report.py           comparison tables and CSV
analysis/plots.py            figures
```

Implemented from the standard: the multi-superframe structure (BO/MO/SO),
enhanced beacons and beacon-based association, slotted CSMA-CA in the CAP, TDMA
GTS transmission with acknowledgements in the CFP, channel hopping (Eq. 1),
SAB and ACT maintenance by overhearing, the three-way
Request/Reply/Notify allocation handshake, Duplicated Allocation Notification,
GTS expiry via idle counters, and the CAP reduction option.

Implemented from TaCFPext: changeable CAP selection (Algorithm 1), extGTS
allocation over taSAB sub-blocks, taACT, the CCB update (Algorithm 2),
listen-only periods, the shortened extGTS expiry, and the extCFP hopping
sequence with the control channel excluded (Eq. 3).

## 5. Checking the estimator on its own

`make test` compiles `dsme-predictor.c` on the host against a small shim and
exercises it on synthetic arrival traces — no radio, no slot machine, no
simulator. It checks that the level converges on a constant rate and the trend
decays to zero; that an idle link never converts a CAP; that a step in arrivals
fires the trigger within one superframe; that on a rising ramp the forecast
leads the latest sample (14.25 against a measured 10); that with the CFP still
free the node asks for ordinary GTSs instead of taking a CAP, and converts only
when the CFP is exhausted; that the cooldown bounds the conversion rate; and
that the Q16.16 arithmetic does not overflow at saturation.

## 6. Running it

**Prerequisites.** A Contiki-NG checkout with Cooja built
(`cd tools/cooja && ./gradlew build`), a C compiler, Java 21+, and Python 3.
Point the build at Contiki-NG either by placing the checkout beside this
repository, or explicitly:

```bash
export CONTIKI=/path/to/contiki-ng
```

Then:

```bash
make test        # host-compiled estimator unit tests, no simulator needed
make run-all     # every scenario (A, B, C) plus the parameter sweep
make analyse     # tables, CSV and docs/RESULTS.md
make figures     # figures into analysis/out/   (needs numpy + matplotlib)
```

Or drive the sweep directly:

```bash
python3 sim/run_experiments.py --nodes 21 41 61 81 101 --seeds 1 2 3 --jobs 10
```

```bash
python3 analysis/report.py && .venv/bin/python analysis/plots.py
```

`make run-all` runs all three traffic scenarios plus the parameter sweep;
`make analyse` and `make figures` produce `docs/RESULTS.md` and the figures.

Variants (`sim/run_experiments.py`): `legacy`, `capred`, `tacfpext`,
`tacfpext-lv`, `ptacfpext`, `ptacfpext-or`, and the ablations `abl-notrend`,
`abl-noonset`, `abl-nosab`.

---

¹ S.-W. Lee, J.-H. Kwon, X. Zhang, E.-J. Kim, "Traffic-Adaptive CFP Extension
for IEEE 802.15.4 DSME MAC in Industrial Wireless Sensor Networks",
IEEE Access, vol. 9, pp. 94454–94469, 2021.
