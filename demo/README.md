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

Pace: the default runs in real time. To slow down at a burst, use Cooja's
speed control in the toolbar or Pause. Alternatively start slower with
`make demo-predictive SPEED=0.5`. `LOAD=moderate` uses a burst of 6 packets
instead of 21, the regime where failures are transient. `SEED=2` uses a
different random topology.

## What is on screen

| window | shows |
|---|---|
| Network view (top left) | blue = PAN coordinator, lilac = sender, grey = receiver; arrows = radio frames |
| LEDs on each sender | **red** in its burst · **yellow** GTS requests being refused · **green** holds an extended-CFP slot |
| Notes (bottom left) | the legend above |
| Commentary (top right) | every burst start, refusal and trigger with its latency, a running tally, final result |
| Timeline (middle right) | LED history of the 10 senders, so the red-to-green gap is visible per burst |
| Mote output (bottom right) | raw DSME log, filtered to bursts, triggers, refusals and allocations |

**The thing to point at:** the gap between a sender going **red** (burst starts)
and going **green** (extended CFP in service). Under the reactive trigger it
first has to collect three refusals (yellow). The predictive trigger converts
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
| control frames per delivered packet | 0.69 | 1.02 |

One seed. The main README's figures are means over three seeds, so they differ
slightly (for example 1355 ms vs 128 ms for the trigger latency). The control-frame
row is the cost of predicting, and the main README discusses it.

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
