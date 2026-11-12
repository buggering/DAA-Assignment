#!/bin/bash
# Double-click in Finder to run. Opens a Terminal window, then Cooja.
cd "$(dirname "$0")/.." || exit 1
# Java 21 and GNU make 4 from Homebrew; macOS's own make (3.81) is too old.
export PATH="/opt/homebrew/opt/openjdk@21/bin:/opt/homebrew/opt/make/libexec/gnubin:/opt/homebrew/bin:$PATH"
python3 demo/demo.py gui reactive
echo
read -n 1 -s -r -p "Done. Press any key to close this window."
