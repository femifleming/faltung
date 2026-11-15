#!/usr/bin/env python3
"""Builds "Cloudfin Manual.pdf" from ../HELP.md: turns the Markdown into a page and prints it with
Chrome (run it from anywhere; it needs Google Chrome or Chromium)."""
import html, os, re, shutil, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
docs = os.path.dirname(here)
fonts = os.path.abspath(os.path.join(docs, "..", "..", "Resources", "fonts"))   # the faltung fonts
md = open(os.path.join(docs, "HELP.md"), encoding="utf-8").read()

def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"!\[([^\]]*)\]\(([^)]+)\)", lambda m: '<img src="../%s" alt="%s">' % (m.group(2), m.group(1)), t)
    t = re.sub(r"\[([^\]]+)\]\((#[^)]+)\)", r"\1", t)                       # links inside the page: just the words
    t = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", t)
    t = re.sub(r"\*([^*]+)\*", r"<i>\1</i>", t)
    return t

out, in_list, in_contents = [], False, False
for line in md.split("\n"):
    if line.startswith("## Contents"): in_contents = True; continue        # (the PDF has no use for the link list)
    if in_contents:
        if line.startswith("## "): in_contents = False
        else: continue
    if line.startswith("- "):
        if not in_list: out.append("<ul>"); in_list = True
        out.append("<li>%s</li>" % inline(line[2:])); continue
    if in_list: out.append("</ul>"); in_list = False
    if line.startswith("# "):    out.append('<h1>%s</h1>' % inline(line[2:].replace(": help", "")))
    elif line.startswith("## "): out.append('<h2>%s</h2>' % inline(line[3:]))
    elif line.strip():           out.append('<p>%s</p>' % inline(line))
if in_list: out.append("</ul>")

page = """<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Cloudfin Manual</title><style>
@font-face{font-family:Inter;src:url("file://""" + fonts + """/Inter-Regular.ttf");font-weight:400}
@font-face{font-family:Inter;src:url("file://""" + fonts + """/Inter-Medium.ttf");font-weight:600}
@font-face{font-family:Plex;src:url("file://""" + fonts + """/IBMPlexMono-Regular.ttf");font-weight:400}
@font-face{font-family:Plex;src:url("file://""" + fonts + """/IBMPlexMono-Medium.ttf");font-weight:600}
@page{size:A4;margin:16mm 16mm 18mm 16mm}
html{-webkit-print-color-adjust:exact;print-color-adjust:exact}
body{font:9.8pt/1.5 Inter,Helvetica,sans-serif;color:#2a1f24}
h1{font:600 26pt Inter;margin:0 0 1mm;color:#2a1f24}
h2{font:600 11pt Plex;text-transform:lowercase;border-bottom:.6pt solid #2a1f24;padding-bottom:2pt;margin:8mm 0 3mm;break-after:avoid}
h2:before{content:"";display:inline-block;width:16pt;height:8pt;margin-right:7pt;background:#e06a2e;transform:skewX(-28deg)}
p{margin:0 0 7pt} img{max-width:100%;margin:3pt 0 6pt} ul{margin:0 0 8pt;padding-left:14pt}
i{font-style:normal;font-family:Plex;font-size:9pt}
.sub{font:9.5pt Plex;color:#6b5a60;margin-bottom:7mm}
a{color:inherit}
</style></head><body>""" + "\n".join(out).replace("</h1>", '</h1><p class="sub">part of faltung · manual, version %s</p>' % (sys.argv[1] if len(sys.argv) > 1 else ""), 1) + "</body></html>"
src = os.path.join(here, "manual.html")
open(src, "w", encoding="utf-8").write(page)
chrome = next((c for c in ("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome", shutil.which("google-chrome"), shutil.which("chromium")) if c and os.path.exists(c)), None)
if chrome is None: sys.exit("Chrome not found: open manual.html and print it to PDF by hand")
subprocess.run([chrome, "--headless", "--disable-gpu", "--no-pdf-header-footer", "--allow-file-access-from-files", "--print-to-pdf=" + os.path.join(here, "Cloudfin Manual.pdf"), "file://" + src],
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("wrote", os.path.join(here, "Cloudfin Manual.pdf"))
