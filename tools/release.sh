#!/bin/bash
# Baut die Firmware, signiert sie und veroeffentlicht ein GitHub-Release.
# Signaturschluessel: Infisical, Projekt TRMNL, OTA_SIGNING_KEY (nie als Datei).
#
#   tools/release.sh            # Version aus platformio.ini
#   tools/release.sh --probe    # nur bauen und signieren, nichts veroeffentlichen
set -euo pipefail
cd "$(dirname "$0")/.."
PROBE=0; [ "${1:-}" = "--probe" ] && PROBE=1

VERSION=$(sed -n 's/.*EIGEN_FIRMWARE_VERSION=\\"\([0-9.]*\)\\".*/\1/p' platformio.ini)
[ -n "$VERSION" ] || { echo "Version nicht gefunden"; exit 1; }
REPO="christiankriedemann/trmnl-eigenfirmware"
echo "Version $VERSION"

pio run >/dev/null
D=dist/v$VERSION
rm -rf "$D"; mkdir -p "$D"
cp .pio/build/trmnl_og/firmware.bin "$D/firmware.bin"
SIZE=$(stat -f%z "$D/firmware.bin")
SHA=$(shasum -a 256 "$D/firmware.bin" | cut -d' ' -f1)

# Signiert wird "trmnl-ota\n<version>\n<sha256>": Version und Abbild zusammen (kein Downgrade)
printf 'trmnl-ota\n%s\n%s' "$VERSION" "$SHA" > "$D/signiert.txt"
# Schluessel nur im Speicher (Umgebungsvariable aus Infisical)
source ~/dev/claude-code/infisical.sh
ck_run TRMNL -- bash -c 'openssl dgst -sha256 -sign <(printf "%s\n" "$OTA_SIGNING_KEY") -out "'"$D"'/firmware.sig" "'"$D"'/signiert.txt"' 2>/dev/null
openssl dgst -sha256 -verify tools/ota_public.pem -signature "$D/firmware.sig" "$D/signiert.txt" | grep -q "Verified OK" \
  || { echo "Signatur passt nicht zum oeffentlichen Schluessel"; exit 1; }
SIG=$(base64 < "$D/firmware.sig" | tr -d '\n')
rm "$D/firmware.sig" "$D/signiert.txt"

cat > "$D/manifest.json" <<EOF
{"version":"$VERSION","url":"https://github.com/$REPO/releases/download/v$VERSION/firmware.bin","size":$SIZE,"sha256":"$SHA","sig":"$SIG"}
EOF
echo "signiert: $D (Groesse $SIZE, SHA-256 ${SHA:0:12}...)"

# Dateien fuer die Installationsseite (docs/, GitHub Pages): komplettes Abbild fuer ESP Web Tools
W=docs/firmware
mkdir -p "$W"
cp .pio/build/trmnl_og/bootloader.bin .pio/build/trmnl_og/partitions.bin "$W/"
cp ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin "$W/"
cp "$D/firmware.bin" "$W/firmware.bin"
cat > "$W/manifest.json" <<EOF2
{"name":"TRMNL Eigenfirmware","version":"$VERSION","new_install_prompt_erase":false,"new_install_improv_wait_time":0,
 "builds":[{"chipFamily":"ESP32-C3","parts":[
  {"path":"bootloader.bin","offset":0},{"path":"partitions.bin","offset":32768},
  {"path":"boot_app0.bin","offset":57344},{"path":"firmware.bin","offset":65536}]}]}
EOF2

[ $PROBE = 1 ] && { echo "Probe: nicht veroeffentlicht"; exit 0; }
git diff --quiet -- . ':!docs/firmware' && git diff --cached --quiet || { echo "Arbeitskopie nicht sauber, erst committen"; exit 1; }
git add "$W" && git commit -qm "Installationsseite: Version $VERSION" || true
git tag -f "v$VERSION" >/dev/null
git push -q origin main "v$VERSION" -f
gh release create "v$VERSION" "$D/firmware.bin" "$D/manifest.json" --repo "$REPO" \
  --title "Version $VERSION" --notes "Firmware $VERSION fuer das TRMNL OG. Das Display spielt sie per WLAN selbst ein." --latest
echo "veroeffentlicht: https://github.com/$REPO/releases/tag/v$VERSION"
