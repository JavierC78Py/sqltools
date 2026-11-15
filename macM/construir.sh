#!/bin/bash
# Compila y empaqueta SQLTools Mac para Macs con chip APPLE SILICON (M1/M2/M3/M4, arm64).
# Requiere: Qt 6.8 (universal) en ~/Qt/6.8.3/macos y el Oracle Instant Client **ARM64** (Basic + SDK)
# descomprimido en ../instantclient_arm64  (ver LEEME.md de esta carpeta).
# Resultado: macM/paquete/SQLTools-Mac-arm64.zip
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT="${QT_ROOT:-$HOME/Qt/6.8.3/macos}"
IC="${IC_DIR:-$ROOT/instantclient_arm64}"
[ -f "$IC/libclntsh.dylib" ] || { echo "Falta el Instant Client ARM64 en $IC (vea macM/LEEME.md)"; exit 1; }
cmake -S "$ROOT" -B "$ROOT/build-arm64" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT" \
      -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)" -DORACLE_IC_DIR="$IC"
cmake --build "$ROOT/build-arm64"
IC_DIR="$IC" BUILD_DIR="$ROOT/build-arm64" OUT_DIR="$ROOT/macM/paquete" bash "$ROOT/tools/package_app.sh"
