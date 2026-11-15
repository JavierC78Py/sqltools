#!/usr/bin/env python3
"""Genera el manual (HTML de un solo archivo + PDF) con un diseño cuidado.

- Lee docs/manual/index.txt (mismo índice que usa tools/build_manual.cpp).
- Convierte el Markdown con un conversor mínimo propio (sin dependencias).
- Incrusta las imágenes, y genera el PDF con Chrome/Brave/Edge en modo headless si hay alguno instalado.
Uso: build_manual_html.py <carpeta docs> <carpeta salida> [nombre]
"""
import base64, datetime, html, os, re, subprocess, sys, tempfile

docs = os.path.abspath(sys.argv[1]); out = os.path.abspath(sys.argv[2]); name = sys.argv[3] if len(sys.argv) > 3 else "SQLTools-Mac-Manual"
img_dir = os.path.join(docs, "manual")
os.makedirs(out, exist_ok=True)


def slug(t):
    t = re.sub(r"<[^>]+>", "", t).lower()
    t = re.sub(r"[^\w\s-]", "", t, flags=re.U)
    return re.sub(r"\s+", "-", t.strip())[:60] or "s"


_img_cache = {}
def img_src(path):
    full = os.path.join(img_dir, path)
    if not os.path.exists(full): return path
    if full not in _img_cache:
        with open(full, "rb") as f: _img_cache[full] = "data:image/png;base64," + base64.b64encode(f.read()).decode()
    return _img_cache[full]


KBD = re.compile(r"^(?:[⌘⌥⇧⌃⎋↩⌫⇥␣↑↓←→]|F\d{1,2}|Fn|Esc|Tab|Intro|Return|Alt|Ctrl|Shift|Cmd)(?:[\s,+\-⌘⌥⇧⌃↑↓←→]|[A-Za-z0-9/.\[\]=;'`\\*]|F\d{1,2}|…)*$")
def kbd_ok(s): return bool(KBD.match(s.strip())) and len(s) < 40


def inline(s):
    codes = []
    def keep(m): codes.append(m.group(1)); return "\x00%d\x00" % (len(codes) - 1)
    s = re.sub(r"``\s*(.+?)\s*``|`([^`]+)`", lambda m: keep(type("M", (), {"group": lambda self, i, m=m: m.group(1) or m.group(2)})()), s)
    s = html.escape(s, quote=False)
    s = re.sub(r"!\[([^\]]*)\]\(([^)]+)\)", lambda m: '<img alt="%s" src="%s">' % (html.escape(m.group(1)), img_src(m.group(2))), s)
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', s)
    def strong(m):
        t = m.group(1)
        return "<kbd>%s</kbd>" % t if kbd_ok(t) else "<strong>%s</strong>" % t
    s = re.sub(r"\*\*(.+?)\*\*", strong, s)
    s = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", s)
    s = re.sub(r"\((F\d{1,2}|[⌘⌥⇧⌃][^)]{0,30})\)", lambda m: "(<kbd>%s</kbd>)" % m.group(1), s)
    return re.sub(r"\x00(\d+)\x00", lambda m: "<code>%s</code>" % html.escape(codes[int(m.group(1))], quote=False), s)


def cells(line):
    line = line.strip()
    if line.startswith("|"): line = line[1:]
    if line.endswith("|"): line = line[:-1]
    return [c.strip() for c in re.split(r"(?<!\\)\|", line)]


def render(md):
    L = md.split("\n"); i = 0; o = []; headings = []
    def flush_list(items):
        pass
    while i < len(L):
        ln = L[i]
        if ln.startswith("```"):
            buf = []; i += 1
            while i < len(L) and not L[i].startswith("```"): buf.append(L[i]); i += 1
            i += 1; o.append("<pre><code>%s</code></pre>" % html.escape("\n".join(buf), quote=False)); continue
        m = re.match(r"^(#{1,4})\s+(.*)$", ln)
        if m:
            lvl = len(m.group(1)); txt = m.group(2).strip(); sid = slug(txt); headings.append((lvl, txt, sid))
            o.append("<h%d id=\"%s\">%s</h%d>" % (lvl, sid, inline(txt), lvl)); i += 1; continue
        if ln.strip().startswith("|") and i + 1 < len(L) and re.match(r"^\s*\|?[\s:|-]+\|[\s:|-]*$", L[i + 1]):
            head = cells(ln); i += 2; rows = []
            while i < len(L) and L[i].strip().startswith("|"): rows.append(cells(L[i])); i += 1
            t = "<div class=\"tw\"><table><thead><tr>" + "".join("<th>%s</th>" % inline(h) for h in head) + "</tr></thead><tbody>"
            for r in rows:
                has_img = any("![" in c for c in r)
                t += "<tr%s>" % (' class="shots"' if has_img else "") + "".join("<td>%s</td>" % inline(c) for c in r) + "</tr>"
            o.append(t + "</tbody></table></div>"); continue
        if ln.startswith(">"):
            buf = []
            while i < len(L) and L[i].startswith(">"): buf.append(L[i][1:].strip()); i += 1
            o.append('<aside class="note"><span class="ni">i</span><div>%s</div></aside>' % inline(" ".join(buf))); continue
        if re.match(r"^\s*([-*]|\d+\.)\s+", ln):
            # lista (con anidado por sangría)
            items = []; extra = {}
            while i < len(L) and (re.match(r"^\s*([-*]|\d+\.)\s+", L[i]) or (L[i].startswith("  ") and L[i].strip() and items)):
                if L[i].strip().startswith("```"):      # bloque de código dentro de un elemento de lista
                    buf = []; i += 1
                    while i < len(L) and not L[i].strip().startswith("```"): buf.append(L[i].strip()); i += 1
                    i += 1; extra.setdefault(len(items) - 1, []).append("<pre><code>%s</code></pre>" % html.escape("\n".join(buf), quote=False)); continue
                mm = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)$", L[i])
                if mm: items.append((len(mm.group(1)), mm.group(2)[0].isdigit(), mm.group(3)))
                else: items[-1] = (items[-1][0], items[-1][1], items[-1][2] + " " + L[i].strip())
                i += 1
            html_l = ""; stack = []
            for k, (ind, ordered, txt) in enumerate(items):
                while stack and stack[-1][0] > ind: html_l += "</li></%s>" % stack.pop()[1]
                if not stack or stack[-1][0] < ind:
                    tag = "ol" if ordered else "ul"; stack.append((ind, tag)); html_l += "<%s><li>" % tag
                else: html_l += "</li><li>"
                html_l += inline(txt) + "".join(extra.get(k, []))
            while stack: html_l += "</li></%s>" % stack.pop()[1]
            o.append(html_l); continue
        if re.match(r"^\s*(---+|\*\*\*+)\s*$", ln): o.append("<hr>"); i += 1; continue
        if not ln.strip(): i += 1; continue
        buf = [ln.strip()]; i += 1
        while i < len(L) and L[i].strip() and not re.match(r"^(#{1,4}\s|```|>|\s*([-*]|\d+\.)\s|\s*\|)", L[i]): buf.append(L[i].strip()); i += 1
        txt = " ".join(buf)
        if re.fullmatch(r"!\[[^\]]*\]\([^)]+\)", txt):
            m2 = re.match(r"!\[([^\]]*)\]\(([^)]+)\)", txt); o.append('<figure><img alt="%s" src="%s"><figcaption>%s</figcaption></figure>' % (html.escape(m2.group(1)), img_src(m2.group(2)), html.escape(m2.group(1))))
        else: o.append("<p>%s</p>" % inline(txt))
    return "\n".join(o), headings


# ---------- ensamblar capítulos ----------
chapters = []; pending = None
for raw in open(os.path.join(img_dir, "index.txt"), encoding="utf-8").read().split("\n"):
    l = raw.strip()
    if not l or l.startswith("#"): continue
    if l.startswith("@apendice:"): pending = l[10:]; continue
    p = os.path.join(docs, l)
    if not os.path.exists(p): print("falta", p, file=sys.stderr); continue
    t = open(p, encoding="utf-8").read()
    if pending:
        ls = t.split("\n")
        if ls and ls[0].startswith("# "): ls = ls[1:]
        t = "# " + pending + "\n" + "\n".join(ls); pending = None
    chapters.append(t)

sections = []; toc = []
for n, t in enumerate(chapters):
    body, hs = render(t)
    first = next((h for h in hs if h[0] == 1), None)
    sec_id = first[2] if first else "cap%d" % n
    if n == 0:     # portada: el título del primer capítulo va en la portada
        body = re.sub(r"<h1[^>]*>.*?</h1>", "", body, count=1)
    sections.append('<section class="chapter%s">%s</section>' % (" intro" if n == 0 else "", body))
    for lvl, txt, sid in hs:
        if lvl == 1 and n > 0: toc.append((1, txt, sid))
        elif lvl == 2 and n > 0 and len(chapters[n]) < 60000: toc.append((2, txt, sid))

def toc_html(pdf):
    s = "<nav class=\"toc\"><ul>"
    for lvl, txt, sid in toc:
        if lvl == 1: s += '<li class="c1"><a href="#%s">%s</a></li>' % (sid, html.escape(txt))
        elif not pdf: s += '<li class="c2"><a href="#%s">%s</a></li>' % (sid, html.escape(txt))
    return s + "</ul></nav>"

CSS = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "manual.css"), encoding="utf-8").read()
today = datetime.date.today().strftime("%d/%m/%Y")
cover = '''<header class="cover"><div class="badge">SQL</div><h1>SQLTools Mac</h1><p class="sub">Manual del usuario</p>
<p class="tag">El cliente Oracle para desarrolladores PL/SQL y APEX, nativo para macOS</p><p class="meta">Versión del %s</p></header>''' % today

def page(pdf):
    side = '' if pdf else '<aside class="side"><div class="brand"><span class="badge s">SQL</span> SQLTools Mac</div>%s</aside>' % toc_html(False)
    toc_block = '<section class="chapter tocpage"><h1>Contenido</h1>%s</section>' % toc_html(True) if pdf else ""
    first = sections[0]
    return '<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SQLTools Mac — Manual del usuario</title><style>%s</style></head><body class="%s">%s<main>%s%s%s%s</main></body></html>' % (
        CSS, "pdf" if pdf else "web", side, cover, toc_block if pdf else "", first, "\n".join(sections[1:]))

html_path = os.path.join(out, name + ".html")
with open(html_path, "w", encoding="utf-8") as f: f.write(page(False))
print("HTML: %s (%d imágenes, %d KB)" % (html_path, len(_img_cache), os.path.getsize(html_path) // 1024))

# ---------- PDF con Chrome headless ----------
browsers = ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser", "/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge", "/Applications/Chromium.app/Contents/MacOS/Chromium"]
br = next((b for b in browsers if os.path.exists(b)), None)
if not br:
    print("Sin Chrome/Brave/Edge: no se genera el PDF (use el generador de Qt)."); sys.exit(3)
pdf_path = os.path.join(out, name + ".pdf")
with tempfile.TemporaryDirectory() as td:
    src = os.path.join(td, "m.html")
    with open(src, "w", encoding="utf-8") as f: f.write(page(True))
    r = None
    for attempt in range(3):          # Chrome headless a veces se queda colgado al terminar: se reintenta
        try:
            r = subprocess.run([br, "--headless=new", "--disable-gpu", "--no-sandbox", "--user-data-dir=" + os.path.join(td, "prof%d" % attempt), "--no-pdf-header-footer", "--virtual-time-budget=20000", "--print-to-pdf=" + pdf_path, "file://" + src], capture_output=True, text=True, timeout=90)
            break
        except subprocess.TimeoutExpired:
            if os.path.exists(pdf_path) and os.path.getsize(pdf_path) > 100000: break
if os.path.exists(pdf_path): print("PDF: %s (%d KB)" % (pdf_path, os.path.getsize(pdf_path) // 1024))
else: print("PDF falló:", (r.stderr[-300:] if r else "tiempo agotado")); sys.exit(4)
