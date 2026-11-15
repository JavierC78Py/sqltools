#!/usr/bin/env python3
"""Genera docs/MENUS_Y_ATAJOS.md a partir del volcado real de la app (SQLToolsMac --dump-commands):
cada comando de los menús de SQLTools (Windows), su atajo de Windows, el atajo equivalente en Mac y si ya está implementado.
Uso: SQLToolsMac --dump-commands > /tmp/commands.tsv ; python3 tools/gen_commands_doc.py /tmp/commands.tsv"""
import sys, collections, pathlib, datetime
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
by_menu = collections.OrderedDict()
order = ["File", "Edit", "View", "Search", "Text", "Text/Format", "Grid", "Session", "Script", "Tools", "Window", "Help", "(solo barra)"]
for r in rows:
    if len(r) < 6: continue
    id_, label, path, win, mac, done = r[:6]
    top = path.split(" > ")[0] if path else "(solo barra)"
    by_menu.setdefault(top, []).append((path, label, id_, win, mac, done == "si"))
total = len(rows); done = sum(1 for r in rows if len(r) >= 6 and r[5] == "si")
out = [f"# Menús y atajos — SQLTools Mac vs SQLTools (Windows)", "",
       "Generado automáticamente desde la propia aplicación (`SQLToolsMac --dump-commands` + `tools/gen_commands_doc.py`). **No editar a mano.**", "",
       f"**Comandos de los menús de SQLTools: {total}. Implementados: {done}. Pendientes: {total - done}.**", "",
       "Reglas de traducción de atajos: Ctrl de Windows → **Cmd (⌘)**; Alt → **Opción (⌥)**. Los que chocan con el sistema (Cmd+Q salir, Cmd+H ocultar, Cmd+M minimizar, Cmd+W cerrar, Cmd+Espacio Spotlight) usan la tecla **Control (⌃)**. "
       "Los acordes (`Ctrl+K,Q`) se escriben igual: pulsar el primero y luego la segunda tecla. Un atajo solo está activo si el comando está implementado.", "",
       "✅ = implementado · ⏳ = pendiente (aparece en el menú desactivado)", ""]
for top in [t for t in order if t in by_menu] + [t for t in by_menu if t not in order]:
    items = sorted(by_menu[top], key=lambda x: (x[0], x[1]))
    n_done = sum(1 for i in items if i[5])
    out += [f"## {top}  ({n_done}/{len(items)})", "", "| | Submenú | Comando | Windows | Mac | ID |", "|---|---|---|---|---|---|"]
    for path, label, id_, win, mac, d in items:
        sub = " > ".join(path.split(" > ")[1:])
        out.append(f"| {'✅' if d else '⏳'} | {sub} | {label} | {win or '—'} | {mac if d and mac else ('—' if not mac else '(' + mac + ')')} | `{id_}` |")
    out.append("")
out += ["## Diferencias deliberadas respecto a Windows", "",
        "- **Cancelar**: además de `Cmd+D, A`, se puede usar **Esc**.",
        "- **Expand Template / autocompletado**: **Control+Espacio** (Cmd+Espacio es Spotlight).",
        "- **Exit / About / Settings**: usan el menú de la aplicación de macOS (Cmd+Q para salir).",
        "- **Cmd+K** ya no abre las conexiones: es el prefijo de los acordes de edición `Cmd+K, x` (duplicar línea, etc.). Las conexiones se abren con **Cmd+D, C**.",
        "- **F5** ejecuta el script completo o la selección (como Windows); **F10** o **Cmd+Intro** ejecuta la sentencia actual.",
        "- En un MacBook las teclas F suelen requerir **Fn** (o activar «Usar teclas F1, F2, etc. como teclas de función estándar»).", ""]
pathlib.Path(__file__).resolve().parent.parent.joinpath("docs/MENUS_Y_ATAJOS.md").write_text("\n".join(out), encoding="utf-8")
print(f"docs/MENUS_Y_ATAJOS.md: {total} comandos, {done} implementados")
