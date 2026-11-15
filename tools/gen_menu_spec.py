#!/usr/bin/env python3
"""Extrae del código de SQLTools (Windows) sus menús, atajos y barras de herramientas y los guarda como datos (JSON).
La app Mac construye su barra de menús y sus barras de iconos a partir de ese JSON.
Uso: python3 tools/gen_menu_spec.py"""
import json, re, sys, collections, pathlib

SRC = pathlib.Path(__file__).resolve().parent.parent / "fuente_original/sqlt.25/SQLTools"
OUT = pathlib.Path(__file__).resolve().parent.parent / "ui/spec/menu_spec.json"

rc = (SRC / "SQLTools.rc").read_text(encoding="latin-1", errors="replace")

# ---- 1) comando (nombre del keymap) <-> ID ----
name_of = {}
for f in SRC.rglob("*.cpp"):
    for m in re.finditer(r'InsertCommand\(\s*"([^"]+)"\s*,\s*(ID_[A-Z0-9_]+)\s*\)', f.read_text(encoding="latin-1", errors="replace")):
        name_of.setdefault(m.group(2), m.group(1))

# ---- 2) keymap por defecto: nombre -> lista de atajos ----
keys = collections.defaultdict(list)
for line in (SRC / "Settings/Data/default.keymap").read_text(encoding="utf-8", errors="replace").splitlines():
    line = line.strip()
    if not line or line.startswith("#"): continue
    parts = line.split(None, 1)
    if len(parts) == 2: keys[parts[0]].append(parts[1].strip())

# ---- 3) menús del .rc ----
def parse_menu(rc_text, name):
    m = re.search(r'^' + name + r'\s+MENU\s*\nBEGIN\n(.*?)\n^END', rc_text, re.S | re.M)
    if not m: return []
    lines = [l.rstrip() for l in m.group(1).splitlines()]
    pos = 0
    def parse_block():
        nonlocal pos
        items = []
        while pos < len(lines):
            l = lines[pos].strip(); pos += 1
            if not l or l.startswith("//"): continue
            if l == "END": return items
            if l == "BEGIN": continue
            pm = re.match(r'POPUP\s+"([^"]*)"', l)
            if pm:
                if pos < len(lines) and lines[pos].strip() == "BEGIN": pos += 1
                items.append({"type": "popup", "label": pm.group(1), "children": parse_block()})
                continue
            if l.startswith("MENUITEM SEPARATOR"):
                items.append({"type": "separator"}); continue
            im = re.match(r'MENUITEM\s+"([^"]*)"\s*,\s*(ID_[A-Z0-9_]+|\d+)\s*(?:,\s*(.*))?', l)
            if im:
                flags = (im.group(3) or "").replace(" ", "")
                items.append({"type": "item", "label": im.group(1), "id": im.group(2),
                              "grayed": "GRAYED" in flags, "checked": "CHECKED" in flags})
        return items
    return parse_block()

menu_doc = parse_menu(rc, "IDR_SQLTYPE")        # menú cuando hay un script abierto (el de la captura)
menu_frame = parse_menu(rc, "IDR_MAINFRAME")    # menú sin documentos

# ---- 4) barras de herramientas ----
toolbars = {}
for tb in re.finditer(r'^(IDT_[A-Z_]+)\s+TOOLBAR\s+(\d+),\s*(\d+)\s*\nBEGIN\n(.*?)\n^END', rc, re.S | re.M):
    btns = []
    for l in tb.group(4).splitlines():
        l = l.strip()
        if l.startswith("SEPARATOR"): btns.append(None)
        else:
            m = re.match(r'BUTTON\s+(ID_[A-Z0-9_]+)', l)
            if m: btns.append(m.group(1))
    toolbars[tb.group(1)] = {"size": int(tb.group(2)), "buttons": btns}

# ---- 5) textos de ayuda (barra de estado\nitem) ----
help_text = {}
for m in re.finditer(r'^\s*(ID_[A-Z0-9_]+)\s+"([^"]*)"', rc, re.M):
    help_text.setdefault(m.group(1), m.group(2).replace("\\n", "\n"))

def collect_ids(items, acc):
    for it in items:
        if it["type"] == "popup": collect_ids(it["children"], acc)
        elif it["type"] == "item": acc.add(it["id"])
ids = set(); collect_ids(menu_doc, ids)
spec = {"menu": menu_doc, "menuNoDocument": menu_frame, "toolbars": toolbars,
        "commands": {i: {"name": name_of.get(i, ""), "keys": keys.get(name_of.get(i, ""), []), "help": help_text.get(i, "")} for i in sorted(ids | {b for t in toolbars.values() for b in t["buttons"] if b})}}
OUT.write_text(json.dumps(spec, ensure_ascii=False, indent=1), encoding="utf-8")

popups = [p for p in menu_doc if p["type"] == "popup"]
print(f"Menú principal (con script abierto): {len(popups)} menús: " + ", ".join(p['label'].replace('&','') for p in popups))
print(f"Comandos distintos en los menús: {len(ids)}   | con atajo en el keymap: {sum(1 for i in ids if keys.get(name_of.get(i,''), []))}")
for k, t in toolbars.items(): print(f"Barra {k}: {sum(1 for b in t['buttons'] if b)} botones ({t['size']} px)")
print("escrito:", OUT)
