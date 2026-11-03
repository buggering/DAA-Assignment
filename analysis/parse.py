#!/usr/bin/env python3
"""Parse a Cooja testlog of a DSME run into structured events.

Each line is  <sim_time_us>;<mote_id>;<message>  and every message this study
cares about is  #D|<EVENT>|<key>=<value>|...
"""
import re
from collections import defaultdict

LINE = re.compile(r"^(\d+);(\d+);#D\|([A-Z]+)\|?(.*)$")


def parse_file(path):
    """Yield dicts: {t_us, node, event, ...fields}."""
    out = []
    with open(path, "r", errors="replace") as f:
        for line in f:
            m = LINE.match(line.rstrip("\n"))
            if not m:
                continue
            t_us, mote, event, rest = m.groups()
            rec = {"t_us": int(t_us), "mote": int(mote), "event": event}
            for kv in rest.split("|"):
                if not kv or "=" not in kv:
                    continue
                k, _, v = kv.partition("=")
                try:
                    rec[k] = int(v)
                except ValueError:
                    rec[k] = v
            out.append(rec)
    return out


def by_event(records):
    d = defaultdict(list)
    for r in records:
        d[r["event"]].append(r)
    return d


def by_node(records):
    d = defaultdict(list)
    for r in records:
        d[r.get("n", r["mote"])].append(r)
    return d


if __name__ == "__main__":
    import sys
    recs = parse_file(sys.argv[1])
    ev = by_event(recs)
    print("records:", len(recs))
    for k in sorted(ev):
        print("  {:10s} {}".format(k, len(ev[k])))
