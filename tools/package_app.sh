#!/bin/bash
# Empaqueta SQLTools Mac como una app AUTÓNOMA (Qt y Oracle Instant Client dentro del .app) lista para copiar a otro Mac.
# Resultado: <OUT_DIR>/SQLTools-Mac-<arquitectura>.zip   (uso: bash tools/package_app.sh; variables opcionales: IC_DIR, BUILD_DIR, OUT_DIR)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QT="${QT_ROOT:-$HOME/Qt/6.8.3/macos}"
IC="${IC_DIR:-$ROOT/instantclient_19_16}"
SRC="${BUILD_DIR:-$ROOT/build-release}/SQLToolsMac.app"
OUT="${OUT_DIR:-$ROOT/dist}"      # solo se borra esta carpeta: use una exclusiva (macIntel/paquete, macM/paquete)
APP="$OUT/SQLTools Mac.app"
ARCH="$(lipo -archs "$SRC/Contents/MacOS/SQLToolsMac")"

[ -d "$SRC" ] || { echo "No existe $SRC: compile primero (cmake --build build-release)"; exit 1; }
echo "== 1/6 Copiando la app ($ARCH)"; rm -rf "$OUT"; mkdir -p "$OUT"; ditto "$SRC" "$APP"
FW="$APP/Contents/Frameworks"; mkdir -p "$FW"

echo "== 2/6 Oracle Instant Client dentro del .app"
CL="$(basename "$(readlink "$IC/libclntsh.dylib" || echo libclntsh.dylib)")"      # p. ej. libclntsh.dylib.19.1 o .23.1
VER="${CL#libclntsh.dylib.}"
NNZ=libnnz.dylib; [ -f "$IC/libnnz${VER%%.*}.dylib" ] && NNZ="libnnz${VER%%.*}.dylib"   # 19: libnnz19.dylib; 23: libnnz.dylib
for f in "$CL" "libclntshcore.dylib.$VER" "$NNZ" libociei.dylib; do cp "$IC/$f" "$FW/$f"; done
ln -sf "$CL" "$FW/libclntsh.dylib"; chmod -R u+w "$FW"

echo "== 3/6 Qt (frameworks y plugins) con macdeployqt"
"$QT/bin/macdeployqt" "$APP" -libpath="$FW" 2>&1 | grep -E 'ERROR|WARNING' || true

echo "== 4/6 Quitando rutas absolutas del equipo de desarrollo"
EXE="$APP/Contents/MacOS/SQLToolsMac"
for rp in "$IC" "$QT/lib"; do install_name_tool -delete_rpath "$rp" "$EXE" 2>/dev/null || true; done
otool -l "$EXE" | grep -A2 LC_RPATH | grep -q '@executable_path/../Frameworks' || install_name_tool -add_rpath @executable_path/../Frameworks "$EXE"

echo "== 5/6 Firma ad-hoc"
codesign --force --deep -s - "$APP" 2>&1 | tail -1 || true

echo "== 6/6 Comprobaciones"
bad=0
while IFS= read -r f; do
  if otool -L "$f" 2>/dev/null | grep -E '/Users/|/Qt/|instantclient_19_16' | grep -v "^$f" >/dev/null; then echo "  ¡ruta absoluta en $f!"; bad=1; fi
done < <(find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) 2>/dev/null)
otool -l "$EXE" | grep -A2 LC_RPATH | grep path | sed 's/^ */  rpath: /'
[ $bad = 0 ] && echo "  sin rutas absolutas: OK"

# Carpeta final que se comparte: la app + guía de instalación + documentación + licencias
STAGE="$OUT/SQLTools-Mac-$ARCH"; mkdir -p "$STAGE/Documentos"
mv "$APP" "$STAGE/"
cp "$ROOT/docs/ATAJOS.md" "$ROOT/docs/MENUS_Y_ATAJOS.md" "$ROOT/docs/GUIA_HERRAMIENTAS.md" "$STAGE/Documentos/" 2>/dev/null || true
cp "$ROOT/docs/manual/salida/SQLTools-Mac-Manual.pdf" "$ROOT/docs/manual/salida/SQLTools-Mac-Manual.html" "$STAGE/Documentos/" 2>/dev/null || true       # manual del usuario (tools/build_manual.sh)
cp "$ROOT/fuente_original/sqlt.25/SQLTools/License.txt" "$STAGE/Documentos/LICENCIA_SQLTools_GPLv3.txt" 2>/dev/null || true
cp "$IC/BASIC_LICENSE" "$STAGE/Documentos/LICENCIA_Oracle_InstantClient.txt" 2>/dev/null || true
cp "$ROOT/docs/LEEME_COMPANERO.md" "$STAGE/LEEME.md" 2>/dev/null || true
ZIP="$OUT/SQLTools-Mac-$ARCH.zip"
( cd "$OUT" && ditto -c -k --keepParent "SQLTools-Mac-$ARCH" "$ZIP" )
echo; du -sh "$STAGE/SQLTools Mac.app" "$ZIP" | sed 's/^/  /'
echo "Listo: $ZIP"
