#!/bin/zsh
# builds putz.app (and with --install copies it to /Applications)
set -e
cd "${0:A:h}"
APP=build/putz.app
rm -rf $APP
mkdir -p $APP/Contents/MacOS $APP/Contents/Resources/fonts
swiftc -O -swift-version 5 -target arm64-apple-macos14.0 \
    -parse-as-library -module-name putz \
    -o $APP/Contents/MacOS/putz Sources/*.swift -lsqlite3
cp Resources/fonts/*.ttf $APP/Contents/Resources/fonts/
[ -f Resources/AppIcon.icns ] && cp Resources/AppIcon.icns $APP/Contents/Resources/
cat > $APP/Contents/Info.plist <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>putz</string>
  <key>CFBundleDisplayName</key><string>putz</string>
  <key>CFBundleIdentifier</key><string>com.faltung.putz</string>
  <key>CFBundleExecutable</key><string>putz</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>LSMinimumSystemVersion</key><string>14.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
  <key>NSAppleEventsUsageDescription</key><string>putz asks System Events for your login items and the Finder to remove apps installed by a package.</string>
</dict></plist>
EOF
codesign --force --deep -s - $APP
echo "built $APP"
if [[ "$1" == "--install" ]]; then
  rm -rf /Applications/putz.app
  cp -R $APP /Applications/putz.app
  echo "installed /Applications/putz.app"
fi
