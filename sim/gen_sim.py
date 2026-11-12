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
from xml.sax.saxutils import escape as xml_escape

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


# --- Classroom demo (--gui) --------------------------------------------------
# The live commentary measures exactly what analysis/metrics.py measures, with
# the same settle time and horizon, so the numbers on screen agree with the
# offline comparison (demo/demo.py compare) of the same variant and seed when
# Cooja runs without a window. A run in the window can differ: Cooja's GUI
# mode does not always process events that fall on the same simulated
# microsecond in the same order (demo/README.md).
GUI_SCRIPT = """TIMEOUT({timeout_ms});
var MODES = ["Legacy DSME (no CFP extension)",
             "DSME with CAP reduction",
             "TaCFPext  -  REACTIVE trigger: waits for {fails} consecutive GTS refusals",
             "P-TaCFPext  -  PREDICTIVE trigger: Holt / EWMA traffic forecast"];
var SETTLE_US = 5000000;
var HORIZON_US = 3932160;
var open = {{}};
var trig = [];
var ext = [];
var headed = false;

function sec(t) {{ return (t / 1000000).toFixed(1); }}
function ms(us) {{ return Math.round(us / 1000); }}
function pad(s, n) {{ s = "" + s; while (s.length < n) s = " " + s; return s; }}
function field(m, k) {{
  var i = m.indexOf("|" + k + "=");
  if (i < 0) return null;
  var j = m.indexOf("|", i + 1);
  return m.substring(i + k.length + 2, j < 0 ? m.length : j);
}}
function say(t, n, text) {{
  log.log(pad(sec(t), 6) + " s   node " + pad(n, 2) + "   " + text + "\\n");
}}
function median(a) {{
  if (a.length == 0) return null;
  var b = a.slice().sort(function(x, y) {{ return x - y; }});
  var h = Math.floor(b.length / 2);
  return b.length % 2 ? b[h] : (b[h - 1] + b[h]) / 2;
}}
function tally() {{
  var hit = 0, imputed = [];
  for (var i = 0; i < trig.length; i++) {{
    if (trig[i] != null) hit++;
    imputed.push(trig[i] == null ? HORIZON_US / 1000 : trig[i]);
  }}
  return hit + " of " + trig.length + " bursts triggered, median onset-to-trigger "
         + Math.round(median(imputed)) + " ms";
}}
function close(n, p) {{
  trig.push(p.trig);
  ext.push(p.ext);
  delete open[n];
}}
function expire(now) {{
  for (var n in open) {{
    var p = open[n];
    if (now - p.t0 > HORIZON_US) {{
      if (p.trig == null) {{
        say(now, n, "XX  no trigger within " + sec(HORIZON_US) + " s of its burst start");
      }}
      close(n, p);
      log.log("                         [so far: " + tally() + "]\\n");
    }}
  }}
}}
function summary() {{
  for (var n in open) close(n, open[n]);
  var extHit = 0, extObs = [];
  for (var i = 0; i < ext.length; i++) {{
    if (ext[i] != null) extHit++;
    extObs.push(ext[i] == null ? HORIZON_US / 1000 : ext[i]);
  }}
  log.log("\\n==============================  RESULT  ==============================\\n");
  log.log("  " + MODES[mode] + "\\n");
  log.log("  bursts measured (after the first 5 s)  : " + trig.length + "\\n");
  log.log("  " + tally() + "   (never = " + Math.round(HORIZON_US / 1000) + " ms)\\n");
  log.log("  extended-CFP slot in service           : " + extHit + " of " + ext.length + " bursts"
          + (extObs.length ? ", median " + Math.round(median(extObs)) + " ms after onset" : "") + "\\n");
  log.log("=====================================================================\\n");
}}
/* End of run: print the result and stop cleanly (TEST OK, not TEST TIMEOUT). */
timeout_function = function() {{ summary(); throw new TestOK(); }};

var mode = {mode};
while (true) {{
  if (!headed && time > 0) {{
    headed = true;
    log.log("=====================================================================\\n");
    log.log("  " + MODES[mode] + "\\n");
    log.log("  {nodes} nodes, seed {seed}.  Bursts in the first 5 s are warm-up and not counted.\\n");
    log.log("=====================================================================\\n");
  }}
  expire(time);
  if (msg.indexOf("#D|ONSET|") == 0 && time >= SETTLE_US) {{
    var n = field(msg, "n");
    open[n] = {{ t0: time, trig: null, ext: null }};
    say(time, n, ">>  BURST STARTS  (" + field(msg, "prev") + " -> " + field(msg, "rate")
        + " packets per multi-superframe)");
  }} else if (msg.indexOf("#D|FAIL|") == 0) {{
    var n = field(msg, "n");
    var p = open[n];
    if (p && p.trig == null && field(msg, "ext") == "0") {{
      say(time, n, " -  GTS request refused  (" + field(msg, "fails")
          + " consecutive, " + field(msg, "why") + ")");
    }}
  }} else if (msg.indexOf("#D|TRIG|") == 0) {{
    var n = field(msg, "n");
    var p = open[n];
    if (p && p.trig == null) {{
      p.trig = ms(time - p.t0);
      say(time, n, "**  TRIGGER  " + p.trig + " ms after burst start   ["
          + (field(msg, "kind") == "pred" ? "predicted" : "after " + field(msg, "fails") + " refusals")
          + "]  -> converting a CAP to extended CFP");
    }}
  }} else if (msg.indexOf("#D|OK|") == 0 && field(msg, "ext") == "1") {{
    var n = field(msg, "n");
    var p = open[n];
    if (p && p.ext == null) {{
      p.ext = ms(time - p.t0);
      say(time, n, "OK  extended-CFP slot in service  " + p.ext + " ms after burst start");
    }}
  }}
  YIELD();
}}
"""

GUI_NOTES = """WHAT YOU ARE LOOKING AT   ({label})

Gold = PAN coordinator.  Lilac = sender.  Grey = receiver.
Senders burst in two alternating groups: 2 s of heavy traffic every 8 s.

LEDs on each sender
  RED     the node is in its traffic burst
  BLUE    its GTS requests are being refused
  GREEN   it holds an extended-CFP slot (CAP converted)

The race to watch: how long from RED to GREEN?
  Reactive  TaCFPext waits for 3 refusals (BLUE) first.
  P-TaCFPext forecasts the burst and converts early.

Commentary window: every burst, refusal and trigger with
its latency, a running tally, and the result at 60 s.
"""


def gui_plugins(args, mode_label):
    """Window layout for a classroom projector, about 1420 x 770 px."""
    senders = [i for i in range(2, args.nodes + 1, 2) if i + 1 <= args.nodes]
    timeline_motes = "\n".join(
        "      <mote>{}</mote>".format(i - 1) for i in senders)
    script = GUI_SCRIPT.format(timeout_ms=args.timeout_ms, mode=args.mode,
                               fails=args.fail_threshold, nodes=args.nodes,
                               seed=args.seed)
    return """  <plugin>
    org.contikios.cooja.plugins.Visualizer
    <plugin_config>
      <skin>org.contikios.cooja.plugins.skins.IDVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.AttributeVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.LEDVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.TrafficVisualizerSkin</skin>
      <skin>org.contikios.cooja.plugins.skins.UDGMVisualizerSkin</skin>
      <viewport>26.0 0.0 0.0 26.0 30.0 25.0</viewport>
    </plugin_config>
    <bounds x="0" y="0" width="460" height="470" z="1" />
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.Notes
    <plugin_config>
      <notes>{notes}</notes>
      <decorations>true</decorations>
    </plugin_config>
    <bounds x="0" y="470" width="460" height="300" z="2" />
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.ScriptRunner
    <plugin_config>
      <script>{script}</script>
      <active>true</active>
    </plugin_config>
    <bounds x="460" y="0" width="960" height="330" z="0" />
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.TimeLine
    <plugin_config>
{timeline_motes}
      <showLEDs />
      <zoomfactor>10000.0</zoomfactor>
    </plugin_config>
    <bounds x="460" y="330" width="960" height="250" z="3" />
  </plugin>
  <plugin>
    org.contikios.cooja.plugins.LogListener
    <plugin_config>
      <filter>ONSET|TRIG|FAIL|\\|OK\\|</filter>
      <formatted_time />
      <coloring />
    </plugin_config>
    <bounds x="460" y="580" width="960" height="190" z="4" />
  </plugin>
""".format(notes=xml_escape(GUI_NOTES.format(label=mode_label)),
           script=xml_escape(script),
           timeline_motes=timeline_motes)


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

    if args.gui:
        label = args.label or args.variant
        title = "DSME demo - {}".format(label)
        speed = "\n    <speedlimit>{}</speedlimit>".format(args.speed)
        plugins = gui_plugins(args, label)
    else:
        title = "DSME {} n={} seed={}".format(args.variant, args.nodes, args.seed)
        speed = ""
        plugins = """  <plugin>
    org.contikios.cooja.plugins.ScriptRunner
    <plugin_config>
      <script>{script}</script>
      <active>true</active>
    </plugin_config>
  </plugin>
""".format(script=script)

    return """<?xml version="1.0" encoding="UTF-8"?>
<simconf version="2022112801">
  <simulation>
    <title>{title}</title>
    <randomseed>{seed}</randomseed>
    <motedelay_us>{motedelay}</motedelay_us>{speed}
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
{plugins}</simconf>
""".format(
        title=title,
        speed=speed,
        plugins=plugins,
        seed=args.seed,
        motedelay=args.motedelay_us,
        tx=TX_RANGE,
        itf=args.interference_range,
        prr=args.prr,
        source=os.path.abspath(args.source),
        commands=commands,
        ifaces=ifaces,
        motes="\n".join(motes),
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
    # Classroom demo: open in the Cooja GUI with a network view, LED timeline,
    # filtered mote output and a live commentary instead of a bare log script.
    p.add_argument("--gui", action="store_true")
    p.add_argument("--label", default="", help="variant name shown on screen")
    p.add_argument("--mode", type=int, default=2,
                   help="DSME_MODE of the firmware, for the on-screen label")
    p.add_argument("--fail-threshold", type=int, default=3)
    p.add_argument("--speed", type=float, default=1.0,
                   help="Cooja speed limit; 1.0 = real time")
    args = p.parse_args()
    if not args.firmware:
        args.firmware = os.path.join(os.path.dirname(os.path.abspath(args.source)),
                                     "build", "cooja", "node.cooja")
    with open(args.out, "w") as f:
        f.write(build(args))
    print(args.out)


if __name__ == "__main__":
    main()
