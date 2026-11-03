#!/usr/bin/env python3
"""Metrics for the DSME CFP-extension study.

Headline metrics, in the order they bear on the objective:

  trigger_latency_ms   time from a burst onset at a node to that node starting
                       a CAP-to-extCFP conversion. This is the quantity the
                       predictive trigger exists to collapse.
  ext_latency_ms       time from the onset to the node holding its first
                       extGTS: the trigger plus the negotiation that the
                       prediction is meant to hide behind the burst ramp.
  reach_k_ms           time from the onset until the node holds k TX slots.
  burst_drop_ratio     share of the packets generated during the burst that
                       missed their deadline -- the drop spike the aim names.
  burst_delay_ms       mean queueing-plus-access delay of packets served
                       during the burst.

Censoring is reported, never hidden. An onset at which the node never
triggered, or never reached k slots, is a worse outcome than a slow one, so
dropping those rows from a mean would flatter whichever variant fails most
often. Every latency summary therefore carries the fraction of onsets that
reached the event at all, and a median computed with the unreached cases
imputed at the observation horizon.

Counter deltas are taken over windows aligned to the burst, because the
per-packet deadline is enforced once per multi-superframe: a window shorter
than that can fall entirely between two flushes and read as zero loss.
"""
import statistics
from collections import defaultdict

SLOT_US = 7680
SF_US = SLOT_US * 16                 # 122880 -- one superframe
MS_US = SF_US * 4                    # 491520 -- one multi-superframe
BI_US = SF_US * 8                    # 983040 -- one beacon interval

# Reference C's dynamic scenario bursts for two beacon intervals.
BURST_LEN_US = 2 * BI_US
EARLY_WINDOW_US = 3 * MS_US        # onset spike: the first three multi-superframes
HORIZON_US = 2 * BURST_LEN_US        # how long we wait for a trigger
REACH_TARGETS = (4, 8, 12)


def _first_after(events, t0, pred, horizon_us):
    for e in events:
        if e["t_us"] < t0:
            continue
        if e["t_us"] > t0 + horizon_us:
            break
        if pred(e):
            return e
    return None


def _sample_at(samples, t):
    """Latest sample at or before t, or None."""
    best = None
    for s in samples:
        if s["t_us"] <= t:
            best = s
        else:
            break
    return best


def _delta(samples, t0, t1, keys):
    s0 = _sample_at(samples, t0)
    s1 = _sample_at(samples, t1)
    if s0 is None or s1 is None or s1 is s0:
        return None
    return {k: s1.get(k, 0) - s0.get(k, 0) for k in keys}


def per_onset(records, settle_us=5 * 1000 * 1000):
    """One row per burst onset at a sender, after the network has settled."""
    by_node = defaultdict(lambda: defaultdict(list))
    for r in records:
        node = r.get("n")
        if node is None:
            continue
        by_node[node][r["event"]].append(r)
    for node in by_node:
        for ev in by_node[node]:
            by_node[node][ev].sort(key=lambda r: r["t_us"])

    rows = []
    for node, ev in by_node.items():
        allocs = ev.get("ALLOC", [])
        trigs = ev.get("TRIG", [])
        oks = ev.get("OK", [])
        samples = ev.get("SAMP", [])
        for o in ev.get("ONSET", []):
            t0 = o["t_us"]
            if t0 < settle_us:
                # The first bursts are discarded: the network is still forming
                # and the estimator is still warming up.
                continue
            row = {
                "node": node,
                "t_onset_us": t0,
                "rate": o.get("rate"),
                "slots_at_onset": o.get("slots", 0),
            }

            tr = _first_after(trigs, t0, lambda e: True, HORIZON_US)
            row["trigger_latency_ms"] = (tr["t_us"] - t0) / 1000.0 if tr else None
            row["trigger_kind"] = tr.get("kind") if tr else None

            ex = _first_after(oks, t0, lambda e: e.get("ext") == 1, HORIZON_US)
            row["ext_latency_ms"] = (ex["t_us"] - t0) / 1000.0 if ex else None

            for k in REACH_TARGETS:
                hit = _first_after(allocs, t0,
                                   lambda e, k=k: e.get("slots", 0) >= k,
                                   HORIZON_US)
                row["reach_{}_ms".format(k)] = (hit["t_us"] - t0) / 1000.0 if hit else None
            row["peak_slots"] = max(
                [a.get("slots", 0) for a in allocs
                 if t0 <= a["t_us"] <= t0 + BURST_LEN_US]
                + [row["slots_at_onset"]])

            full = _delta(samples, t0, t0 + BURST_LEN_US,
                          ("dq", "dg", "gen", "dcnt", "dsum", "txok", "cmd"))
            if full and full["gen"] > 0:
                drops = full["dq"] + full["dg"]
                row["burst_generated"] = full["gen"]
                row["burst_drops"] = drops
                row["burst_drop_ratio"] = drops / full["gen"]
                row["burst_delivered"] = full["txok"]
                row["burst_pdr"] = full["txok"] / full["gen"]
                row["burst_control_frames"] = full["cmd"]
                # dsum is accumulated in milliseconds on the mote.
                row["burst_delay_ms"] = (full["dsum"] / full["dcnt"]
                                         if full["dcnt"] else None)

            early = _delta(samples, t0, t0 + EARLY_WINDOW_US, ("dq", "dg", "gen"))
            if early and early["gen"] > 0:
                row["early_drops"] = early["dq"] + early["dg"]
                row["early_drop_ratio"] = (early["dq"] + early["dg"]) / early["gen"]

            rows.append(row)
    return rows


def network(records):
    """Whole-run, network-wide totals from the final STATS lines."""
    stats = {r["n"]: r for r in records if r["event"] == "STATS"}
    if not stats:
        return {}

    def tot(k):
        return sum(s.get(k, 0) for s in stats.values())

    senders = [s for s in stats.values() if s.get("gen", 0) > 0]
    per_node_tx = [s.get("txok", 0) for s in senders]
    gen, txok = tot("gen"), tot("txok")

    weights = [s["txok"] for s in senders if s.get("txok", 0) > 0]
    delays = [s["delayavg"] for s in senders if s.get("txok", 0) > 0]
    mean_delay_us = (sum(d * w for d, w in zip(delays, weights)) / sum(weights)
                     if weights else None)

    fairness = None
    if per_node_tx and sum(per_node_tx) > 0:
        n = len(per_node_tx)
        fairness = (sum(per_node_tx) ** 2) / (n * sum(x * x for x in per_node_tx))

    run_s = max((r["t_us"] for r in records), default=0) / 1e6
    eused, eidle = tot("eused"), tot("eidle")

    return {
        "senders": len(senders),
        "generated": gen,
        "delivered": txok,
        "pdr": txok / gen if gen else None,
        "drop_ratio": (gen - txok) / gen if gen else None,
        "mean_delay_ms": mean_delay_us / 1000.0 if mean_delay_us else None,
        "max_delay_ms": max((s.get("delaymax", 0) for s in senders), default=0) / 1000.0,
        # Reference C Table 1 payload, adjusted to a standard-legal frame.
        "throughput_kbps": (txok * 120 * 8 / 1000.0) / run_s if run_s else None,
        "fairness": fairness,
        "control_frames": tot("cmdtx"),
        "control_per_delivered": tot("cmdtx") / txok if txok else None,
        "gts_alloc_ok": tot("gok"),
        "gts_alloc_fail": tot("gfail"),
        "extgts_alloc_ok": tot("eok"),
        "extgts_alloc_fail": tot("efail"),
        "trig_reactive": tot("trigr"),
        "trig_predictive": tot("trigp"),
        "cap_conversions": tot("conv"),
        "ext_slots_used": eused,
        "ext_slots_idle": eidle,
        "ext_waste_ratio": eidle / (eused + eidle) if (eused + eidle) else None,
        "run_seconds": run_s,
    }


def summarize(rows, key, horizon_ms=HORIZON_US / 1000.0):
    """Censoring-aware summary of a per-onset latency or count."""
    present = [r for r in rows if key in r]
    vals = [r[key] for r in present if r[key] is not None]
    censored = sum(1 for r in present if r[key] is None)
    total = len(present)
    out = {"n": total, "observed": len(vals), "censored": censored,
           "reach_rate": (len(vals) / total) if total else None}
    if not vals:
        return out
    # Unreached cases imputed at the horizon: a latency that never arrived is
    # at least this bad, so the median stays comparable across variants with
    # different failure rates.
    imputed = sorted(vals + [horizon_ms] * censored)
    out.update({
        "mean_observed": statistics.fmean(vals),
        "median_observed": statistics.median(vals),
        "median_imputed": statistics.median(imputed),
        "p90_imputed": imputed[int(0.9 * (len(imputed) - 1))],
        "max_observed": max(vals),
    })
    return out


def onset_triggered_average(records, before_us=BI_US, after_us=3 * BI_US,
                            bin_us=SF_US, settle_us=5 * 1000 * 1000):
    """Average a node's trajectory around its own burst onsets.

    Folding on the onset rather than on absolute time is what makes the two
    traffic groups comparable: they burst in different beacon intervals, so an
    absolute-time average would smear the very transient being measured.

    Returns {t_s, slots, ext, queue, drops, delivered, n_onsets}, where t_s is
    seconds relative to the onset and drops/delivered are per-superframe rates
    averaged over onsets.
    """
    by_node = defaultdict(lambda: defaultdict(list))
    for r in records:
        n = r.get("n")
        if n is None:
            continue
        by_node[n][r["event"]].append(r)
    for n in by_node:
        for e in by_node[n]:
            by_node[n][e].sort(key=lambda r: r["t_us"])

    nbins = int((before_us + after_us) // bin_us)
    acc = [{"slots": 0.0, "ext": 0.0, "q": 0.0, "drops": 0.0, "deliv": 0.0,
            "n": 0} for _ in range(nbins)]
    n_onsets = 0

    for n, ev in by_node.items():
        samples = ev.get("SAMP", [])
        if not samples:
            continue
        for o in ev.get("ONSET", []):
            t0 = o["t_us"]
            if t0 < settle_us:
                continue
            window = [s for s in samples
                      if t0 - before_us <= s["t_us"] < t0 + after_us]
            if len(window) < nbins // 2:
                continue
            n_onsets += 1
            prev = None
            for s in window:
                b = int((s["t_us"] - (t0 - before_us)) // bin_us)
                if not 0 <= b < nbins:
                    continue
                a = acc[b]
                a["slots"] += s.get("slots", 0)
                a["ext"] += s.get("ext", 0)
                a["q"] += s.get("q", 0)
                a["n"] += 1
                if prev is not None:
                    a["drops"] += ((s.get("dq", 0) + s.get("dg", 0))
                                   - (prev.get("dq", 0) + prev.get("dg", 0)))
                    a["deliv"] += s.get("txok", 0) - prev.get("txok", 0)
                prev = s

    def avg(key):
        return [(acc[b][key] / acc[b]["n"]) if acc[b]["n"] else None
                for b in range(nbins)]

    return {
        "t_s": [((b + 0.5) * bin_us - before_us) / 1e6 for b in range(nbins)],
        "slots": avg("slots"),
        "ext": avg("ext"),
        "queue": avg("q"),
        "drops": avg("drops"),
        "delivered": avg("deliv"),
        "n_onsets": n_onsets,
    }
