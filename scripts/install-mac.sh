#!/usr/bin/env bash
# Installs every built faltung plug-in for the current user, and the apps into /Applications/faltung.
set -euo pipefail
B="${1:-$(cd "$(dirname "$0")/.." && pwd)/build}"
P="$HOME/Library/Audio/Plug-Ins"
mkdir -p "$P/VST3" "$P/Components" "$P/VST" "/Applications/faltung"
for d in "$B"/faltung_*_artefacts/Release; do
    t="$(basename "$(dirname "$d")" _artefacts)"; n="${t/faltung_/faltung }"
    [ -d "$d/VST3/$n.vst3" ]       && rm -rf "$P/VST3/$n.vst3"        && cp -R "$d/VST3/$n.vst3" "$P/VST3/"
    [ -d "$d/AU/$n.component" ]    && rm -rf "$P/Components/$n.component" && cp -R "$d/AU/$n.component" "$P/Components/"
    [ -d "$d/VST/$n.vst" ]         && rm -rf "$P/VST/$n.vst"          && cp -R "$d/VST/$n.vst" "$P/VST/"
    [ -d "$d/Standalone/$n.app" ]  && rm -rf "/Applications/faltung/$n.app" && cp -R "$d/Standalone/$n.app" "/Applications/faltung/"
    echo "installed $n"
done
killall -9 AudioComponentRegistrar 2>/dev/null || true   # make Logic / Live see new AUs
