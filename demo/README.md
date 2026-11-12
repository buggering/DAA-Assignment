# Classroom demo: reactive vs predictive CFP-extension trigger

Two Cooja runs on the same 21-node network with the same seed, one per trigger,
plus a headless comparison table. Each GUI run lasts 60 simulated seconds,
which on this laptop takes about 64 s from launch to the result box.

## Before class (once)

```bash
cd "~/claude work development/Daa assignment/DAA-Assignment"
make demo-compare        # ~1.5 min; builds both firmwares and prints the table
```

That also confirms Contiki-NG (`~/contiki-ng`) and Cooja are working.

## In class

Double-click in Finder (`demo/` folder):

- `1 Reactive (TaCFPext).command`
- `2 Predictive (P-TaCFPext).command`
- `3 Compare both.command`

Or from a terminal in this folder:

```bash
make demo-reactive       # opens Cooja: TaCFPext, reactive 3-refusal trigger
make demo-predictive     # opens Cooja: P-TaCFPext, predictive trigger
```

Press **Start** in the Cooja toolbar. The run stops by itself at 60 s and
prints a result box in the commentary window. Close Cooja before opening the
other arm. If Cooja asks whether to save the simulation on exit, choose **No**.

Two one-time tweaks for a cleaner screen. Press **F1** in Cooja to hide the
Quick help panel on the right (Cooja remembers this). Click the small **▲** on
the divider inside the commentary window to fold away the script code, so the
commentary gets the whole window.

Pace: the default runs in real time. To slow down at a burst, use Cooja's
speed control in the toolbar or Pause. Alternatively start slower with
`make demo-predictive SPEED=0.5`. `LOAD=moderate` uses a burst of 6 packets
instead of 21, the regime where failures are transient. `SEED=2` uses a
different random topology.

## Quote the table, not the result box

A run in the Cooja window is not exactly repeatable, at any speed. Senders in
the same group start their bursts on the same simulated microsecond, and in
window mode Cooja does not always process those simultaneous events in the
same order, which changes how the contention for the CAP plays out. Runs
without the window (`make demo-compare`, and every experiment behind the
main README) give the same numbers every time.

Predictive arm, seed 1, so far:

| run | trigger fired | median to trigger | extCFP slot | median to slot |
|---|---:|---:|---:|---:|
| no window (`make demo-compare`), every run | 69 of 70 | 128 ms | 53 of 70 | 278 ms |
| window, outcome A (2 runs) | 69 of 70 | 128 ms | 53 of 70 | 278 ms |
| window, outcome B (1 run) | 69 of 70 | 129 ms | 50 of 70 | 290 ms |
| window, outcome C (2 runs) | 69 of 70 | 128 ms | 42 of 70 | 1494 ms |

The trigger line is stable. The extended-CFP line is not: in outcome C the
predictive arm's median slot time is slower than the reactive arm's 1431 ms
in the table. One window run of the reactive arm gave 742 ms to trigger and
1306 ms to slot (table: 864 and 1431).

## What is on screen

| window | shows |
|---|---|
| Network view (top left) | gold = PAN coordinator, lilac = sender, grey = receiver; arrows = radio frames |
| LEDs on each sender | **red** in its burst · **blue** GTS requests being refused (Cooja draws the yellow LED as blue) · **green** holds an extended-CFP slot |
| Notes (bottom left) | the legend above |
| Commentary (top right) | every burst start, refusal and trigger with its latency, a running tally, final result |
| Timeline (middle right) | LED history of the 10 senders, so the red-to-green gap is visible per burst |
| Mote output (bottom right) | raw DSME log, filtered to bursts, triggers, refusals and allocations |

**The thing to point at:** the gap between a sender going **red** (burst starts)
and going **green** (extended CFP in service). Under the reactive trigger it
first has to collect three refusals (blue). The predictive trigger converts
from the forecast, before the refusals arrive.

## Expected numbers (21 nodes, seed 1, paper load)

From `make demo-compare` on this laptop:

| | TaCFPext | P-TaCFPext |
|---|---:|---:|
| bursts at which the trigger fired | 86 % | 99 % |
| median burst start → trigger | 864 ms | 128 ms |
| median burst start → extended-CFP slot | 1431 ms | 278 ms |
| drop ratio, first 1.5 s of a burst | 0.239 | 0.171 |
| delivery ratio | 0.424 | 0.532 |
| queue loss (deadline missed or queue full) | 0.557 | 0.448 |
| control frames per delivered packet | 0.69 | 1.02 |

One seed. The main README's figures are pooled over three seeds and were produced
on the lab machine. They do not all reproduce on this laptop. Seeds 1 to 3 here
give the reactive trigger a median of 865 ms, against 1355 ms in the main README,
while P-TaCFPext's 128 ms and TaCFPext's delivery ratio (0.385 against 0.386)
match. This laptop runs Contiki-NG v5.1 built with Apple clang; the lab results
came from a different Contiki-NG checkout and compiler, and the cause has not
been pinned down. The control-frame row is the cost of predicting, and the main
README discusses it.

## How it is built

- `app/node.c`: with `APP_DEMO_LEDS=1` the senders drive their LEDs from DSME
  state and print a `#A color=` line for the role colours. Off by default; the
  experiment builds never set it. The protocol event log of a demo build is
  byte-identical to the experiment build of the same variant and seed.
- `sim/gen_sim.py --gui`: the window layout, the legend and the commentary
  script. Without `--gui` its output is unchanged.
- `demo/demo.py`: builds `sim/variants/demo-*`, opens Cooja, or runs both
  headless and computes the table with `analysis/metrics.py`, the same code
  that produced `docs/RESULTS.md`.
