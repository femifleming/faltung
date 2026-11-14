#!/usr/bin/env bash
# Collects every built plug-in, app and both manuals into dist/faltung-<os>/<product>/
# usage: ci/package.sh <macOS|Windows|Linux> <build dir>
set -euo pipefail
OS="$1"; B="$2"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/dist/faltung-$OS"
rm -rf "$OUT"; mkdir -p "$OUT"

art() {   # artefact folder of a target (single- or multi-config generators)
    local tgt="$1"
    for d in "$B/${tgt}_artefacts/Release" "$B/${tgt}_artefacts" "$B/partials/${tgt}_artefacts/Release" "$B/partials/${tgt}_artefacts"; do
        [ -d "$d/VST3" ] && { echo "$d"; return; }
    done
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
            [ -f "$A/VST/$file.dll" ] && cp "$A/VST/$file.dll" "$P/" ;;
        Linux)
            cp "$A/Standalone/$file" "$P/"
            [ -d "$A/LV2/$file.lv2" ] && cp -R "$A/LV2/$file.lv2" "$P/"
            [ -f "$A/VST/lib$file.so" ] && cp "$A/VST/lib$file.so" "$P/$file.so"
            [ -f "$A/VST/$file.so" ] && cp "$A/VST/$file.so" "$P/" ;;
    esac
}

for p in kt1 kt2 bd1 sd2 rm3 hh4 cp6 pc7 cl8 tm9 mc3 dl4 rv5 sw6 fl7 gl8 wb1; do
    product "faltung $p" "faltung_$p" "faltung $p"
done
product Partials Partials Partials

cp "$ROOT/docs/manual/faltung Manual.pdf" "$OUT/"
cp "$ROOT/partials/docs/manual/manual.pdf" "$OUT/Partials Manual.pdf"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$OUT/"

cd "$ROOT/dist"
case "$OS" in
    macOS)   ditto -c -k --keepParent "faltung-$OS" "faltung-$OS.zip" ;;
    Windows) 7z a -tzip -r "faltung-$OS.zip" "faltung-$OS" > /dev/null ;;
    Linux)   tar -czf "faltung-$OS.tar.gz" "faltung-$OS" ;;
esac
ls -la "$ROOT/dist"
