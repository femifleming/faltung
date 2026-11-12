#!/bin/bash
# Builds and packages the Linux release (VST3 + LV2 + VST2 + standalone) as a .tar.gz.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-$(grep -m1 'project(ConvoSolution VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')}"
BUILD=build-release
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --config Release
A="$BUILD/ConvoSolution_artefacts/Release"
NAME="ConvoSolution-$VERSION-Linux-$(uname -m)"
STAGE="dist/$NAME"
rm -rf "$STAGE" && mkdir -p "$STAGE/vst3" "$STAGE/lv2" "$STAGE/vst" "$STAGE/bin"
cp -r "$A/VST3/"*.vst3 "$STAGE/vst3/"
cp -r "$A/LV2/"*.lv2 "$STAGE/lv2/"
cp "$A/VST/"*.so "$STAGE/vst/"
cp "$A/Standalone/ConvoSolution" "$STAGE/bin/"
cp packaging/INSTALL.txt README.md LICENSE Resources/IR-CREDITS.txt "$STAGE/"
tar -C dist -czf "dist/$NAME.tar.gz" "$NAME"
sha256sum "dist/$NAME.tar.gz"
