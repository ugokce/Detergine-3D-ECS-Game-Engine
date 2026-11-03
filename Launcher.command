#!/usr/bin/env sh
# Double-click in Finder to open the Detergine Launcher (macOS).
cd "$(dirname "$0")" && exec python3 tools/dg.py gui
