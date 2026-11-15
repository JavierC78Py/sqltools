#!/bin/bash
# Compila y empaqueta SQLTools Mac para Macs con chip INTEL (x86_64).
# Requiere: Qt 6.8 en ~/Qt/6.8.3/macos y el Oracle Instant Client Intel (Basic + SDK) en ../instantclient_19_16
# Resultado: macIntel/paquete/SQLTools-Mac-x86_64.zip
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT="${QT_ROOT:-$HOME/Qt/6.8.3/macos}"
cmake -S "$ROOT" -B "$ROOT/build-release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT" \
      -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)" -DORACLE_IC_DIR="$ROOT/instantclient_19_16"
cmake --build "$ROOT/build-release"
IC_DIR="$ROOT/instantclient_19_16" BUILD_DIR="$ROOT/build-release" OUT_DIR="$ROOT/macIntel/paquete" bash "$ROOT/tools/package_app.sh"
