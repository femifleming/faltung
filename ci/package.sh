#!/usr/bin/env bash
# Collects the built plug-ins, apps and manuals of one suite into dist/<suite>-<os>/<product>/
# usage: ci/package.sh <macOS|Windows|Linux> <build dir> [faltung|deep]
set -euo pipefail
OS="$1"; B="$2"; SUITE="${3:-faltung}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="faltung"; [ "$SUITE" = "deep" ] && NAME="faltung-deep"
OUT="$ROOT/dist/$NAME-$OS"
rm -rf "$OUT"; mkdir -p "$OUT"

art() {   # artefact folder of a target (single- or multi-config generators)
    local tgt="$1"
    for sub in "" partials/ cloudfin/ convosolution/ obliqsound/ ballon/; do for d in "$B/${sub}${tgt}_artefacts/Release" "$B/${sub}${tgt}_artefacts"; do
        [ -d "$d/VST3" ] && { echo "$d"; return; }
    done; done
    echo "missing artefacts for $tgt" >&2; exit 1
}

product() {   # product <folder name> <target> <file name>
    local name="$1" tgt="$2" file="$3"
    local A; A="$(art "$tgt")"
    local P="$OUT/$name"; mkdir -p "$P"
    cp -R "$A/VST3/$file.vst3" "$P/"
    case "$OS" in
        macOS)
            cp -R "$A/AU/$file.component" "$A/Standalone/$file.app" "$P/"
            [ -d "$A/VST/$file.vst" ] && cp -R "$A/VST/$file.vst" "$P/"
            for f in "$P"/*.app "$P"/*.component "$P"/*.vst3 "$P"/*.vst; do
                [ -e "$f" ] && codesign --force --deep -s - "$f"
            done ;;
        Windows)
            cp "$A/Standalone/$file.exe" "$P/"
            [ -f "$A/VST/$file.dll" ] && cp "$A/VST/$file.dll" "$P/" || true
            # ObliqSound / ObliqSonic: the program and the VST2 read an "<Name> Resources" folder beside them
            [ -d "$A/Standalone/$file Resources" ] && cp -R "$A/Standalone/$file Resources" "$P/" || true ;;
        Linux)
            cp "$A/Standalone/$file" "$P/"
            [ -d "$A/LV2/$file.lv2" ] && cp -R "$A/LV2/$file.lv2" "$P/" || true
            [ -f "$A/VST/lib$file.so" ] && cp "$A/VST/lib$file.so" "$P/$file.so" || true
            [ -f "$A/VST/$file.so" ] && cp "$A/VST/$file.so" "$P/" || true
            [ -d "$A/Standalone/$file Resources" ] && cp -R "$A/Standalone/$file Resources" "$P/" || true ;;
    esac
    return 0   # optional formats above may be missing; that is not a failure
}

if [ "$SUITE" = "deep" ]; then
    for p in kt1 kt2 bd1 sd2 rm3 hh4 cp6 pc7 cl8 tm9 mc3 dl4 rv5 sw6 fl7 gl8 sb1; do
        product "faltung deep $p" "faltung_deep_$p" "faltung deep $p"
    done
    product "ballonwinkel deep" ballonwinkel_deep "ballonwinkel deep"
    product "ballonpaleis deep" ballonpaleis_deep "ballonpaleis deep"
    cp "$ROOT/docs/manual-deep/faltung deep Manual.pdf" "$OUT/"
else
    for p in kt1 kt2 bd1 sd2 rm3 hh4 cp6 pc7 cl8 tm9 mc3 dl4 rv5 sw6 fl7 gl8 wb1 ms0; do
        product "faltung $p" "faltung_$p" "faltung $p"
    done
    product Partials Partials Partials
    product Cloudfin Cloudfin Cloudfin
    product ConvoSolution ConvoSolution ConvoSolution
    product ballonwinkel ballonwinkel ballonwinkel
    product ballonpaleis ballonpaleis ballonpaleis
    cp "$ROOT/convosolution/Resources/IR-CREDITS.txt" "$OUT/ConvoSolution/"
    if [ -d "$B/obliqsound/ObliqSound_artefacts" ]; then   # (needs SuperCollider's server: skipped when it was not built)
        product ObliqSound ObliqSound ObliqSound
        product ObliqSonic ObliqSonic ObliqSonic
        cp "$ROOT/obliqsound/docs/manual/obliqsound Manual.pdf" "$OUT/"
    fi
    cp "$ROOT/docs/manual/faltung Manual.pdf" "$ROOT/docs/manual/ms0 master Manual.pdf" "$OUT/"
    cp "$ROOT/partials/docs/manual/manual.pdf" "$OUT/Partials Manual.pdf"
    cp "$ROOT/cloudfin/docs/manual/Cloudfin Manual.pdf" "$OUT/"
    cp "$ROOT/convosolution/docs/manual/convosolution Manual.pdf" "$OUT/"
fi
cp "$ROOT/README.md" "$ROOT/LICENSE" "$OUT/"

cd "$ROOT/dist"
case "$OS" in
    macOS)   ditto -c -k --keepParent "$NAME-$OS" "$NAME-$OS.zip" ;;
    Windows) 7z a -tzip -r "$NAME-$OS.zip" "$NAME-$OS" > /dev/null ;;
    Linux)   tar -czf "$NAME-$OS.tar.gz" "$NAME-$OS" ;;
esac
ls -la "$ROOT/dist"
