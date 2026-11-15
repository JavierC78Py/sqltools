#!/bin/bash
# Regenera el manual del usuario: capturas (con datos ficticios), documentos generados y HTML/PDF.
# Uso: tools/build_manual.sh            (desde cualquier carpeta)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
CMAKE="${CMAKE:-$HOME/Qt/Tools/CMake/CMake.app/Contents/bin/cmake}"; BUILD="${BUILD:-build-release}"
APP="$BUILD/SQLToolsMac.app/Contents/MacOS/SQLToolsMac"; IMG="docs/manual/img"; OUT="docs/manual/salida"
limit() { perl -e 'alarm shift; exec @ARGV' "$@"; }          # macOS no trae «timeout»: se corta a los N segundos

echo "== 1/5 Compilando…";                "$CMAKE" --build "$BUILD" --target SQLToolsMac dialog_shot build_manual > /dev/null
echo "== 2/5 Capturas de la aplicación…"; mkdir -p "$IMG"; SQLT_NOSHOW=1 SQLT_MANUAL_SHOTS="$ROOT/$IMG" limit 240 "$APP" 2>&1 | grep -c "^captura" | sed 's/^/   capturas: /'
echo "== 3/5 Gestor de conexiones…";      limit 60 "$BUILD/dialog_shot" "$IMG/conexiones.png" > /dev/null 2>&1
echo "== 4/5 Lista de comandos y atajos…"; "$APP" --dump-commands > /tmp/sqlt_commands.tsv 2>/dev/null; python3 tools/gen_commands_doc.py /tmp/sqlt_commands.tsv
echo "== 5/5 HTML y PDF…"
# Diseño cuidado (portada, índice lateral, teclas, avisos): HTML propio + PDF con Chrome/Brave headless. Si no hay navegador, usa el generador de Qt.
python3 tools/build_manual_html.py docs "$OUT" || { echo "   (sin navegador para el PDF: se usa el generador de Qt)"; QT_QPA_PLATFORM=offscreen "$BUILD/build_manual" docs "$OUT" 2>&1 | grep -E "^(HTML|PDF)"; }
echo "Listo: $OUT"
