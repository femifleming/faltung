#!/usr/bin/env bash
# Installs every built faltung and faltung deep plug-in for the current user.
# Apps: /Applications/faltung and /Applications/faltung deep.
set -euo pipefail
B="${1:-$(cd "$(dirname "$0")/.." && pwd)/build}"
P="$HOME/Library/Audio/Plug-Ins"
mkdir -p "$P/VST3" "$P/Components" "$P/VST" "/Applications/faltung" "/Applications/faltung deep"
for d in "$B"/faltung_*_artefacts/Release; do
    [ -d "$d" ] || continue
    t="$(basename "$(dirname "$d")" _artefacts)"; n="${t//_/ }"          # faltung_deep_kt1 -> "faltung deep kt1"
    case "$t" in faltung_deep_*) apps="/Applications/faltung deep" ;; *) apps="/Applications/faltung" ;; esac
    if [ -d "$d/VST3/$n.vst3" ]; then rm -rf "$P/VST3/$n.vst3"; cp -R "$d/VST3/$n.vst3" "$P/VST3/"; fi
    if [ -d "$d/AU/$n.component" ]; then rm -rf "$P/Components/$n.component"; cp -R "$d/AU/$n.component" "$P/Components/"; fi
    if [ -d "$d/VST/$n.vst" ]; then rm -rf "$P/VST/$n.vst"; cp -R "$d/VST/$n.vst" "$P/VST/"; fi
    if [ -d "$d/Standalone/$n.app" ]; then rm -rf "$apps/$n.app"; cp -R "$d/Standalone/$n.app" "$apps/"; fi
    echo "installed $n"
done
# Partials, Cloudfin, ConvoSolution, ObliqSound and ObliqSonic: their apps go straight into /Applications
for n in Partials Cloudfin ConvoSolution ObliqSound ObliqSonic; do
    for d in "$B/partials/${n}_artefacts/Release" "$B/cloudfin/${n}_artefacts/Release" "$B/convosolution/${n}_artefacts/Release" "$B/obliqsound/${n}_artefacts/Release"; do
        [ -d "$d" ] || continue
        if [ -d "$d/VST3/$n.vst3" ]; then rm -rf "$P/VST3/$n.vst3"; cp -R "$d/VST3/$n.vst3" "$P/VST3/"; fi
        if [ -d "$d/AU/$n.component" ]; then rm -rf "$P/Components/$n.component"; cp -R "$d/AU/$n.component" "$P/Components/"; fi
        if [ -d "$d/VST/$n.vst" ]; then rm -rf "$P/VST/$n.vst"; cp -R "$d/VST/$n.vst" "$P/VST/"; fi
        if [ -d "$d/Standalone/$n.app" ]; then rm -rf "/Applications/$n.app"; cp -R "$d/Standalone/$n.app" "/Applications/"; fi
        echo "installed $n"
    done
done
# ballonwinkel and ballonpaleis (the twins of Pinkie Pie's Balloon Shop / Palace, whose own installs are left alone):
# apps in /Applications/faltung (the dark ones in /Applications/faltung deep)
for t in ballonwinkel ballonpaleis ballonwinkel_deep ballonpaleis_deep; do
    d="$B/ballon/${t}_artefacts/Release"; n="${t//_/ }"
    [ -f "$d/VST3/$n.vst3/Contents/MacOS/$n" ] || continue   # (not built: the bundles CMake lays out are empty)
    case "$t" in *_deep) apps="/Applications/faltung deep" ;; *) apps="/Applications/faltung" ;; esac
    if [ -d "$d/VST3/$n.vst3" ]; then rm -rf "$P/VST3/$n.vst3"; cp -R "$d/VST3/$n.vst3" "$P/VST3/"; fi
    if [ -d "$d/AU/$n.component" ]; then rm -rf "$P/Components/$n.component"; cp -R "$d/AU/$n.component" "$P/Components/"; fi
    if [ -d "$d/VST/$n.vst" ]; then rm -rf "$P/VST/$n.vst"; cp -R "$d/VST/$n.vst" "$P/VST/"; fi
    if [ -d "$d/Standalone/$n.app" ]; then rm -rf "$apps/$n.app"; cp -R "$d/Standalone/$n.app" "$apps/"; fi
    echo "installed $n"
done
killall -9 AudioComponentRegistrar 2>/dev/null || true   # make Logic / Live see new AUs
