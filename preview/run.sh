#!/bin/sh
# Baut die Mac-Vorschau und rendert alle Beispiele nach preview/out/*.png
set -e
cd "$(dirname "$0")"
R=../lib/render/src
L=../.pio/libdeps/trmnl_og
mkdir -p out
clang -c -O1 -I$L/QRCode/src $L/QRCode/src/qrcode.c -o out/qrcode.o
clang++ -std=c++17 -O1 -Wall -Wno-unused-function -I$R -I$L/ArduinoJson/src -I$L/QRCode/src \
  preview.cpp $R/canvas.cpp $R/text.cpp $R/layout.cpp $R/hyph.cpp out/qrcode.o -o out/preview
for f in sample-*.json; do n=$(basename $f .json); ./out/preview $f out/$n.pbm && magick out/$n.pbm out/$n.png; done
./out/preview x out/qr.pbm qr && magick out/qr.pbm out/qr.png
./out/preview x out/login.pbm login && magick out/login.pbm out/login.png
./out/preview x out/msg.pbm msg && magick out/msg.pbm out/msg.png
ls out/*.png
