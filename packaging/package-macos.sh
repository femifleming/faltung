#!/bin/bash
# Builds a universal (Apple silicon + Intel) release and packages it as a .zip.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-$(grep -m1 'project(ConvoSolution VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')}"
BUILD=build-release
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build "$BUILD" --config Release
A="$BUILD/ConvoSolution_artefacts/Release"
NAME="ConvoSolution-$VERSION-macOS"
STAGE="dist/$NAME"
rm -rf "$STAGE" "dist/$NAME.zip" && mkdir -p "$STAGE/VST3" "$STAGE/VST" "$STAGE/Components" "$STAGE/Applications"
ditto "$A/VST3/ConvoSolution.vst3" "$STAGE/VST3/ConvoSolution.vst3"
ditto "$A/VST/ConvoSolution.vst" "$STAGE/VST/ConvoSolution.vst"
ditto "$A/AU/ConvoSolution.component" "$STAGE/Components/ConvoSolution.component"
ditto "$A/Standalone/ConvoSolution.app" "$STAGE/Applications/ConvoSolution.app"
find "$STAGE" -maxdepth 2 \( -name "*.vst3" -o -name "*.vst" -o -name "*.component" -o -name "*.app" \) -exec codesign --force --deep -s - {} \;
cp packaging/INSTALL.txt README.md LICENSE Resources/IR-CREDITS.txt "$STAGE/"
(cd dist && ditto -c -k --keepParent "$NAME" "$NAME.zip")
shasum -a 256 "dist/$NAME.zip"
