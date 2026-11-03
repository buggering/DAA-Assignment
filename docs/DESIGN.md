# Design and fidelity notes

## 1. Multi-superframe geometry

Every structural constant is derived in `dsme/dsme-const.h` from `SO`, `MO`,
`BO` and the O-QPSK 250 kbit/s PHY, so changing an order moves the whole
geometry consistently.

With the orders of Reference C, Table 1 (`SO=3, MO=5, BO=6`):

| quantity | value |
|---|---|
| slot | `60·2^SO` symbols = 480 symbols = **7 680 µs** |
| superframe | 16 slots = **122.88 ms** |
| multi-superframe | `2^(MO−SO)` = 4 superframes = **491.52 ms** |
| beacon interval | `2^(BO−SO)` = 8 superframes = **983.04 ms** |
| slots 0 / 1–8 / 9–15 | beacon / CAP / CFP (7 GTSs) |
| extCFP | the 8 CAP slots of a converted superframe |

Table 1 of Reference C prints a superframe duration of 112.88 ms and a slot
length of 7.96 ms. Neither follows from its own parameters: `aBaseSuperframe-
Duration·2^SO = 960·8 = 7680` symbols at 16 µs/symbol is 122.88 ms, and a
sixteenth of that is 7.68 ms. The derived values are used here, and the
multi-superframe duration agrees with the paper's own 491.52 ms.

## 2. Why the slot machine is rtimer-driven

On a Cooja mote, a busy-wait advances simulated time in **1 ms** steps:
`RTIMER_BUSYWAIT_UNTIL_ABS` yields through `cooja_mt_yield()`, and
`ContikiClock.doActionsAfterTick()` reschedules a yielding mote at
`now + Simulation.MILLISECOND`. An rtimer, by contrast, is honoured exactly —
the same function wakes the mote at `simRtimerNextExpirationTime` to the
microsecond.

A 1 ms quantum would smear a 320 µs CSMA backoff period and a 7.68 ms slot
beyond recognition. Every instant inside a slot that has to be accurate is
therefore reached by arming an rtimer and yielding a protothread, never by
spinning — the same structure Contiki-NG's TSCH uses, for the same reason.
Packet transmission is exact too: the Cooja radio schedules the mote's wake-up
at `transmissionEndTime`, so `NETSTACK_RADIO.send()` returns precisely
`8·len/250` µs later.

The radio is put in **poll mode** so the platform's radio process cannot drain
a frame into `packetbuf` at an arbitrary moment; the MAC reads the radio itself
at the instants the slot template dictates.

### Slot template

```
CFP / extCFP, transmitter      CFP / extCFP, receiver
  1500 µs  data frame starts     500 µs  radio on
  5340 µs  frame ends           5840 µs  read the frame
  6140 µs  ACK expected         6140 µs  ACK transmitted
  6632 µs  give up on the ACK   6332 µs  ACK ends
                                           (slot is 7680 µs)
```

Both ends derive the acknowledgement instant from the slot boundary rather
than from the end of the data frame, so a receiver need not know the frame
length before it has read it.

In the CAP the node listens across the whole slot — that is how a one-hop
neighbourhood keeps its SAB current — and interleaves its own slotted CSMA-CA
countdown with that listening, one CCA per 320 µs backoff period, at most one
frame per slot. 21 of the 24 backoff periods in a slot are usable; the rest are
reserved so that the longest command frame still finishes before the slot
boundary. Overrunning here would push the next slot late, which a TDMA MAC must
never do.

## 3. Scenario

Reference C, Section IV-A: nodes are deployed at random in a 15 × 15 m area
with a 10 m transmission range, and each node pairs with a one-hop neighbour.
Node 1 is the PAN coordinator and sits at the centre; nodes 2..N form pairs
(2→3, 4→5, …).

Traffic follows the paper's dynamic scenario: the senders are split into two
equal groups with a period of eight beacon intervals; one group bursts in the
third and fourth beacon intervals of the period, the other in the seventh and
eighth, and outside their burst both generate one packet per multi-superframe.
The burst rate is 21 packets per multi-superframe. Packets are released at a
constant rate across the multi-superframe rather than all at once, so the
per-superframe arrival process the estimator samples is the real one and not an
artefact of the generator.

### Interference range

The Cooja radio medium is configured with the interference range **equal to**
the transmission range. Reference C specifies a 10 m transmission range and
models CAP contention as CSMA/CA collisions; it says nothing about hidden
terminals. Setting the interference range to the conventional 2× makes
clear-channel assessment blind across most of a 15 × 15 m field: measured here,
responders then heard only 40 % of the requests addressed to them, and
allocation failures became dominated by *timeouts* rather than the explicit
"disapproved — lack of availability" refusals that TaCFPext's trigger is
defined on. That is a different experiment. With matched ranges, responders
handle 85 % of requests and refusals dominate, which is the regime the paper
studies. `sim/gen_sim.py --interference-range 20` runs the hidden-terminal case
deliberately as a robustness check.

### Packet deadline

Reference C: "the node drops the data packets that could not be transmitted in
the current multi-superframe." The queue therefore carries a deadline of one
multi-superframe rather than growing without bound — which is also the right
model for an industrial WSN, where a late sample is worth no more than a lost
one. Without it, mean delay runs to several seconds and simply measures queue
depth.

## 4. Experimental design

Four things are varied independently, because a gain cannot otherwise be
attributed:

| variant | demand estimate | extension trigger |
|---|---|---|
| `legacy` | oracle | none (no CAP reduction) |
| `capred` | oracle | none (CAP reduction on) |
| `tacfpext` | oracle | reactive, 3 consecutive failures |
| `tacfpext-lv` | previous multi-superframe | reactive, 3 consecutive failures |
| `ptacfpext` | Holt forecast | predictive + reactive backstop |
| `ptacfpext-or` | oracle | predictive + reactive backstop |
| `abl-notrend` | single EWMA, no trend | predictive |
| `abl-noonset` | Holt, no onset detector | predictive |
| `abl-nosab` | Holt | predictive without the SAB capacity test |

`tacfpext` is the **strongest** reading of the published protocol: the node is
told its own offered load the instant it changes, so it never has to learn its
own demand — only that the CFP is full. `tacfpext-lv` is what an implementation
without a traffic model would actually do. Reporting both keeps the comparison
honest in either direction.

The reactive counter here advances on **any** failed allocation — an explicit
refusal or a `macMaxFrameTotalWaitTime` expiry. Figure 6 of Reference C shows
three explicit refusals, so counting refusals alone would be the literal
reading; counting both makes the baseline trigger sooner, and the baseline is
given that benefit.

### Metrics and censoring

The headline metric is **trigger latency**: time from a burst onset at a node
to that node starting a CAP-to-extCFP conversion. Then **ext latency** (first
extGTS in service), **reach-k** (time to hold k TX slots), and the
**burst-window drop ratio** and **delay**.

An onset at which the node never triggered is a *worse* outcome than a slow
one, so dropping those rows from a mean would flatter whichever variant fails
most often. Every latency summary therefore reports the fraction of onsets that
reached the event at all alongside a median computed with the unreached cases
imputed at the observation horizon.

Counter deltas are taken over windows aligned to the burst, because the
per-packet deadline is enforced once per multi-superframe: a shorter window can
fall entirely between two flushes and read as zero loss.

## 5. Deliberate simplifications

These are modelling choices, listed so they can be judged:

- **Beacon scheduling is static.** Each coordinator beacons in superframe
  `address mod 8` of the beacon interval instead of running DSME's distributed
  beacon-scheduling algorithm. Beacon scheduling is not the object of this
  study, and Cooja motes share one simulated clock, so a single successful
  beacon keeps a node synchronised for the whole run. Association, beacon
  transmission and beacon-timestamp synchronisation are real.
- **Frame format is compact.** A fixed 6-octet header (type, sequence number,
  source, destination) carries the fields this study depends on, instead of the
  full 802.15.4 framer with PAN IDs, security header and IE list. Frame
  *lengths* are kept representative so the air time Cooja derives stays
  faithful to the PHY.
- **Deallocation on expiry is announced in one broadcast** rather than a
  three-message exchange. Both endpoints detect expiry independently from their
  own idle counters, so the extra round trips would carry no information;
  neighbours still learn of the release. This applies identically to every
  variant.
- **Command frames are not acknowledged.** The three-way handshake uses the
  REPLY as its confirmation and `macMaxFrameTotalWaitTime` as its timeout, and
  REPLY and NOTIFY are broadcasts that could not be acknowledged anyway.
- **Payload bytes are not stored**, only frame length — a MAC-layer study only
  needs the air time.
- **One allocation transaction per node at a time**, as
  `macMaxFrameTotalWaitTime` implies.

## 6. Three things the measurements forced

These were not design decisions made up front; each came from looking at why a
number was worse than it should have been.

**Hidden terminals were dominating the CAP.** With the interference range at
the conventional 2x the transmission range, responders heard only 40 % of the
requests addressed to them and allocation failures were dominated by timeouts
rather than the explicit refusals TaCFPext's trigger is defined on. See
Section 3.

**The CAP slot was transmitting a stale frame.** `pt_cap` originally took the
head of the command queue once at the start of the slot. Handling a received
frame mid-slot can queue a REPLY, and a REPLY is pushed to the *front* of the
queue, so by the time the backoff expired the cached pointer named the wrong
frame — the node transmitted one frame and dequeued another. The queue is now
re-read at the moment of transmission.

**GTS and extGTS handshakes were serialised.** A single in-flight transaction
per node meant a node that had just predicted "the CFP is exhausted" still had
to wait out `macMaxFrameTotalWaitTime` on the very GTS request it had predicted
would fail. The trigger-latency distribution showed this plainly: strongly
bimodal, one mode at ~130 ms (the prediction firing immediately) and another at
~2 s (the prediction firing only once the doomed request had timed out). The
two reserve different resources — the CFP and a converted CAP — so they now
have independent transaction slots. This helps the reactive baselines for the
same reason, and does so identically.

One further observation worth recording because it reads like a bug and is not:
the *no early plain-GTS request* ablation produces results identical to the
full protocol. Under `DSME_DEMAND_FORECAST` the node's demand already *is* the
estimator's `g_req`, so `need` and the predictor's deficit are the same number
and the early-request branch cannot fire. It has an effect only when demand
comes from an oracle, where the forecast can run ahead of the declared rate.

## 7. Two corrections made while implementing the published algorithms

- **Algorithm 1, lines 32–34.** As printed, the loop breaks out when `slotCnt`
  is still *below* `numSlots`, which would stop as soon as the demand is not yet
  covered. The surrounding text ("if the number of available slots is less than
  numSlots, the superframe ID of the next extCFP in conArray is appended to SID
  in the same way") makes the intent clear: keep appending until the demand is
  covered, then stop. The corrected sense is used, in
  `dsme_tacfpext_select_changeable_caps()`.
- **Duplicated Allocation Notification.** When two pairs commit to the same
  slot in the same instant, each pair's receiver raises a notification about the
  other and *both* allocations are cancelled, so neither pair makes progress.
  A deterministic tie-break on the lower endpoint address means exactly one
  notification is raised and only the loser backs off. Requesters also choose
  their preferred slot at random, which the standard permits, so simultaneous
  requesters do not all converge on slot 0 in the first place.
