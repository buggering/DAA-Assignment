#!/usr/bin/env python3
"""Generate a Cooja simulation configuration for the DSME CFP-extension study.

Topology follows Reference C, Section IV-A: nodes are deployed at random in a
15 x 15 m area with a 10 m transmission range. Node 1 is the PAN coordinator
and sits at the centre; the remaining nodes form sender-receiver pairs, each
pair placed close enough together to be one-hop neighbours.
"""
import argparse
import math
import random
import os

AREA = 15.0
TX_RANGE = 10.0
# Matched to the transmission range by default. Reference C specifies a 10 m
# transmission range and models CAP contention as CSMA/CA collisions; it says
# nothing about hidden terminals. Setting the interference range wider than
# the transmission range introduces them, which makes clear-channel assessment
# blind and costs roughly half of all command frames -- a different experiment
# from the one this study is about. It is exposed as a flag so that case can
# be run deliberately as a robustness check.
INTERFERENCE_RANGE = 10.0
PAIR_SPACING_MAX = 4.0

MOTE_INTERFACES = [
    "org.contikios.cooja.interfaces.Position",
    "org.contikios.cooja.contikimote.interfaces.ContikiMoteID",
    "org.contikios.cooja.contikimote.interfaces.ContikiRS232",
    "org.contikios.cooja.contikimote.interfaces.ContikiBeeper",
    "org.contikios.cooja.contikimote.interfaces.ContikiRadio",
    "org.contikios.cooja.contikimote.interfaces.ContikiButton",
    "org.contikios.cooja.contikimote.interfaces.ContikiClock",
    "org.contikios.cooja.contikimote.interfaces.ContikiLED",
    "org.contikios.cooja.interfaces.Mote2MoteRelations",
    "org.contikios.cooja.interfaces.MoteAttributes",
]

SCRIPT = """TIMEOUT({timeout_ms});
/* Mirror every mote line into COOJA.testlog, prefixed with the simulated
 * time in microseconds and the mote id, so the offline analysis has an
 * authoritative global clock. */
while(true) {{
  log.log(time + ";" + id + ";" + msg + "\\n");
  YIELD();
}}
"""


def positions(n_nodes, seed):
    """Return a list of (x, y), index 0 being node 1 (the PAN coordinator)."""
    rng = random.Random(seed)
    pos = [(AREA / 2.0, AREA / 2.0)]
    # Nodes 2..n form pairs: an even address and the odd address after it.
    n_pairs = (n_nodes - 1) // 2
    for _ in range(n_pairs):
        sx = rng.uniform(0.0, AREA)
        sy = rng.uniform(0.0, AREA)
        # Receiver within range of its sender.
        while True:
            ang = rng.uniform(0.0, 2.0 * math.pi)
            dist = rng.uniform(1.0, PAIR_SPACING_MAX)
            rx = sx + dist * math.cos(ang)
            ry = sy + dist * math.sin(ang)
            if 0.0 <= rx <= AREA and 0.0 <= ry <= AREA:
                break
        pos.append((sx, sy))
        pos.append((rx, ry))
    # An odd leftover node is placed but never paired.
    while len(pos) < n_nodes:
        pos.append((rng.uniform(0.0, AREA), rng.uniform(0.0, AREA)))
    return pos[:n_nodes]


def build(args):
    pos = positions(args.nodes, args.seed)
    motes = []
    for i, (x, y) in enumerate(pos):
        motes.append(
            "      <mote>\n"
            "        <interface_config>\n"
            "          org.contikios.cooja.interfaces.Position\n"
            '          <pos x="{:.6f}" y="{:.6f}" />\n'
            "        </interface_config>\n"
            "        <interface_config>\n"
            "          org.contikios.cooja.contikimote.interfaces.ContikiMoteID\n"
            "          <id>{}</id>\n"
            "        </interface_config>\n"
            "      </mote>".format(x, y, i + 1)
        )
    ifaces = "\n".join(
        "      <moteinterface>{}</moteinterface>".format(m) for m in MOTE_INTERFACES
    )
    script = SCRIPT.format(timeout_ms=args.timeout_ms)
    # Firmwares are pre-built once per variant by run_experiments.py. Letting
    # Cooja invoke make here would have several concurrent simulations of the
    # same variant running make in the same directory at once.
    commands = args.build_command

    return """<?xml version="1.0" encoding="UTF-8"?>
<simconf version="2022112801">
  <simulation>
    <title>DSME {variant} n={nodes} seed={seed}</title>
    <randomseed>{seed}</randomseed>
    <motedelay_us>{motedelay}</motedelay_us>
    <radiomedium>
      org.contikios.cooja.radiomediums.UDGM
      <transmitting_range>{tx}</transmitting_range>
      <interference_range>{itf}</interference_range>
      <success_ratio_tx>1.0</success_ratio_tx>
      <success_ratio_rx>{prr}</success_ratio_rx>
    </radiomedium>
    <events>
      <logoutput>400000</logoutput>
    </events>
    <motetype>
      org.contikios.cooja.contikimote.ContikiMoteType
      <description>DSME node</description>
      <source>{source}</source>
      <commands>{commands}</commands>
{ifaces}
{motes}
    </motetype>
  </simulation>
  <plugin>
    org.contikios.cooja.plugins.ScriptRunner
    <plugin_config>
      <script>{script}</script>
      <active>true</active>
    </plugin_config>
  </plugin>
</simconf>
""".format(
        variant=args.variant,
        nodes=args.nodes,
        seed=args.seed,
        motedelay=args.motedelay_us,
        tx=TX_RANGE,
        itf=args.interference_range,
        prr=args.prr,
        source=os.path.abspath(args.source),
        commands=commands,
        ifaces=ifaces,
        motes="\n".join(motes),
        script=script,
    )


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--nodes", type=int, required=True)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--variant", default="tacfpext")
    p.add_argument("--source", required=True, help="path to the variant node.c")
    p.add_argument("--timeout-ms", type=int, default=70000)
    p.add_argument("--motedelay-us", type=int, default=100000)
    p.add_argument("--prr", type=float, default=1.0)
    p.add_argument("--build-command", default="true")
    p.add_argument("--firmware", help="prebuilt .cooja library; defaults to "
                                      "build/cooja/node.cooja beside --source")
    p.add_argument("--interference-range", type=float, default=INTERFERENCE_RANGE)
    p.add_argument("--out", required=True)
    args = p.parse_args()
    if not args.firmware:
        args.firmware = os.path.join(os.path.dirname(os.path.abspath(args.source)),
                                     "build", "cooja", "node.cooja")
    with open(args.out, "w") as f:
        f.write(build(args))
    print(args.out)


if __name__ == "__main__":
    main()
