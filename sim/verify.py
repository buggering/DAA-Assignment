#!/usr/bin/env python3
"""Check that every expected simulation produced a usable log.

A run that silently produced nothing would quietly bias every average, so this
is checked explicitly rather than assumed.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS = os.path.join(ROOT, "results")

MIN_BYTES = 50_000          # a 58 s run of any size produces far more
REQUIRED_EVENTS = ("INIT", "JOIN", "SAMP", "STATS", "ONSET")


def main():
    bad, total = [], 0
    for variant in sorted(os.listdir(RESULTS)):
        vdir = os.path.join(RESULTS, variant)
        if not os.path.isdir(vdir):
            continue
        for run in sorted(os.listdir(vdir)):
            log = os.path.join(vdir, run, "COOJA.testlog")
            total += 1
            rel = os.path.join(variant, run)
            if not os.path.exists(log):
                bad.append((rel, "missing"))
                continue
            size = os.path.getsize(log)
            if size < MIN_BYTES:
                bad.append((rel, "only {} bytes".format(size)))
                continue
            with open(log, errors="replace") as f:
                text = f.read()
            missing = [e for e in REQUIRED_EVENTS if "|{}|".format(e) not in text]
            if missing:
                bad.append((rel, "no " + ",".join(missing)))

    print("{} runs checked, {} usable".format(total, total - len(bad)))
    for rel, why in bad:
        print("  BAD  {:<40} {}".format(rel, why))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
