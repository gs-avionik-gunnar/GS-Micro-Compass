#!/usr/bin/env python3
"""Generates GS-Micro-Compass/WebPage.h from webui/index.html (single source of truth for the WebUI)."""
import sys
src = open("webui/index.html", encoding="utf-8").read()
assert ')HTML"' not in src
out = ('// Single-page WebUI (no external frameworks, works fully offline in the module\'s own WiFi).\n'
       '// Generated from webui/index.html by tools/make_webpage.py - edit the HTML file, not this one.\n'
       '#pragma once\n#include <pgmspace.h>\n\n'
       'static const char kIndexHtml[] PROGMEM = R"HTML(' + src + ')HTML";\n')
open("GS-Micro-Compass/WebPage.h", "w", encoding="utf-8").write(out)
print("WebPage.h: %d bytes" % len(out))
