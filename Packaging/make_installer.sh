#!/bin/bash
# Builds the Mac installer (.pkg inside a .dmg) after the plugin has been built.
#   cd ~/Plugins/VocalRider && bash Packaging/make_installer.sh [version]
# Choices shown in the installer depend on what exists in build/: AU, VST3, AAX, CLAP (+ Documents).
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd); P="$ROOT/Packaging"
VERSION="${1:-1.0.0}"
ID=com.daemonjmultimedia.vocaliserider
PB="${PLISTBUDDY:-/usr/libexec/PlistBuddy}"
WORK=$(mktemp -d)
mkdir -p "$WORK/pkgs" "$WORK/res" "$WORK/dmg"
cp "$P"/res/*.html "$WORK/res/"

CHOICES=""; OUTLINE=""; REFS=""

find_one() { find build -path "*Release*" -name "$1" -maxdepth 8 2>/dev/null | head -n 1; }

# add_pkg <key> <found-bundle-path> <install-location> <title> <description> <selected> [nosign]
# 'nosign' (used for AAX) skips the local signature, which would break an Avid/PACE signature.
add_pkg() {
  local key="$1" src="$2" loc="$3" title="$4" desc="$5" sel="$6" nosign="$7"
  local name; name=$(basename "$src")
  local stage="$WORK/stage_$key" scripts="$WORK/scripts_$key"
  mkdir -p "$stage" "$scripts"
  cp -R "$src" "$stage/"
  xattr -cr "$stage/$name" 2>/dev/null || true
  [ "$nosign" = nosign ] || codesign --force --deep --sign - "$stage/$name" 2>/dev/null || true
  cat > "$scripts/postinstall" <<POST
#!/bin/bash
# clear the quarantine flag and sign locally so the DAW can load it
xattr -cr "$loc/$name" 2>/dev/null
$( [ "$nosign" = nosign ] && echo "# AAX: keep the Avid signature" || echo "codesign --force --deep --sign - \"$loc/$name\" 2>/dev/null" )
killall -9 AudioComponentRegistrar 2>/dev/null
exit 0
POST
  chmod +x "$scripts/postinstall"
  pkgbuild --analyze --root "$stage" "$WORK/$key.plist" >/dev/null
  i=0; while "$PB" -c "Set :$i:BundleIsRelocatable false" "$WORK/$key.plist" 2>/dev/null; do i=$((i+1)); done
  pkgbuild --root "$stage" --component-plist "$WORK/$key.plist" --install-location "$loc" \
           --scripts "$scripts" --identifier "$ID.$key" --version "$VERSION" --ownership recommended \
           "$WORK/pkgs/$key.pkg" >/dev/null
  OUTLINE="$OUTLINE<line choice=\"$key\"/>"
  CHOICES="$CHOICES<choice id=\"$key\" title=\"$title\" description=\"$desc\" start_selected=\"$sel\"><pkg-ref id=\"$ID.$key\"/></choice>"
  REFS="$REFS<pkg-ref id=\"$ID.$key\" version=\"$VERSION\" onConclusion=\"none\">$key.pkg</pkg-ref>"
  echo "  + $title"
}

echo "Packaging Vocalise-Rider $VERSION"
AU=$(find_one "Vocalise-Rider.component"); [ -n "$AU" ] || { echo "Build first: no Vocalise-Rider.component found in build/"; exit 1; }
add_pkg au "$AU" "/Library/Audio/Plug-Ins/Components" "Audio Unit (Logic Pro, GarageBand)" "For Logic Pro and other Audio Unit hosts." true
V3=$(find_one "Vocalise-Rider.vst3");  [ -z "$V3" ] || add_pkg vst3 "$V3" "/Library/Audio/Plug-Ins/VST3" "VST3" "For Reaper, Cubase, Studio One, Ableton Live, FL Studio and other VST3 hosts." true
AX=$(find_one "Vocalise-Rider.aaxplugin"); [ -z "$AX" ] || add_pkg aax "$AX" "/Library/Application Support/Avid/Audio/Plug-Ins" "AAX (Pro Tools)" "For Avid Pro Tools." true nosign
CL=$(find_one "Vocalise-Rider.clap");  [ -z "$CL" ] || add_pkg clap "$CL" "/Library/Audio/Plug-Ins/CLAP" "CLAP" "For Bitwig, Reaper and other CLAP hosts." false

# documents
DOCS="$WORK/stage_docs/Daemonj Multimedia/Vocalise-Rider/Documents"
mkdir -p "$DOCS"; cp -R "$P"/docs/. "$DOCS/"; chmod +x "$DOCS"/*.command
pkgbuild --root "$WORK/stage_docs" --install-location "/Users/Shared" --identifier "$ID.docs" --version "$VERSION" \
         --ownership recommended "$WORK/pkgs/docs.pkg" >/dev/null
OUTLINE="$OUTLINE<line choice=\"docs\"/>"
CHOICES="$CHOICES<choice id=\"docs\" visible=\"false\" title=\"Documents\" start_selected=\"true\"><pkg-ref id=\"$ID.docs\"/></choice>"   # hidden: always installed
REFS="$REFS<pkg-ref id=\"$ID.docs\" version=\"$VERSION\" onConclusion=\"none\">docs.pkg</pkg-ref>"
echo "  + Documents"

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
  <title>Vocalise-Rider $VERSION</title>
  <welcome file="welcome.html"/>
  <readme file="readme.html"/>
  <conclusion file="conclusion.html"/>
  <options customize="always" require-scripts="false" hostArchitectures="x86_64,arm64"/>
  <domains enable_anywhere="false" enable_localSystem="true" enable_currentUserHome="false"/>
  <choices-outline>$OUTLINE</choices-outline>
  $CHOICES
  $REFS
</installer-gui-script>
XML

productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/res" --package-path "$WORK/pkgs" \
             "$WORK/dmg/Install Vocalise-Rider.pkg" >/dev/null

# disk image: the installer, the fix-it script, and the documents
cp "$P/Fix Blocked Installer.command" "$WORK/dmg/"
mkdir -p "$WORK/dmg/Documents"; cp -R "$P"/docs/. "$WORK/dmg/Documents/"
OUT="$ROOT/Vocalise-Rider-$VERSION-Installer.dmg"
rm -f "$OUT"
hdiutil create -volname "Vocalise-Rider" -srcfolder "$WORK/dmg" -ov -format UDZO "$OUT" >/dev/null
echo ""; echo "Created $OUT"
