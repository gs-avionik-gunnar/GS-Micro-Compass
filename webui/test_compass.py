#!/usr/bin/env python3
"""Product checks of the GS-Micro-Compass WebUI (Playwright, uses mock_server.py). Run after test_shell.py."""
import sys, threading
from playwright.sync_api import sync_playwright
import mock_server as ms

ms.HTML = sys.argv[1] if len(sys.argv) > 1 else "index.html"
srv = ms.serve(0); port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()
res = []
def check(name, cond): res.append(bool(cond)); print(("PASS " if cond else "FAIL ") + name)

with sync_playwright() as p:
    b = p.chromium.launch(); ctx = b.new_context(locale="en-US", viewport={"width": 390, "height": 900}); pg = ctx.new_page()
    errs = []; pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto(f"http://127.0.0.1:{port}/"); pg.wait_for_function("document.querySelector('#conn').textContent=='online'")
    st = pg.inner_text("#status")
    check("status shows heading, bug, style, sensor", "124.4" in st and "95" in st and "Instrument" in st and "QMC5883L" in st)
    check("status shows declination and calibration", "+4" in st and "Calibrated" in st)
    # every English text of the page has a German entry
    missing = pg.evaluate("""()=>{const out=[];const w=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);let n;
      while(n=w.nextNode()){if(n.parentNode.closest('script,.kv,tbody,#conn,#cmsg,#prg,#fw,h1,#lang'))continue;const t=(n._o!==undefined?n._o:n.nodeValue).trim();if(t&&!DE[t])out.push(t)}
      document.querySelectorAll('[title]').forEach(e=>{const t=(e.otitle||e.getAttribute('title'));if(t&&!DE[t]&&t!='Language / Sprache')out.push('title:'+t)});return out}""")
    check("every English text has a German entry (missing: %s)" % missing, not missing)
    # dynamic strings used by T('...') in the script
    dyn = pg.evaluate("""()=>{const src=[...document.scripts].map(s=>s.textContent).join('\\n');const out=[];const re=/T\\('((?:[^'\\\\]|\\\\.)*)'\\)/g;let m;
      while(m=re.exec(src)){const k=m[1].replace(/\\\\'/g,"'");if(!DE[k])out.push(k)}return out}""")
    check("every T('...') string has a German entry (missing: %s)" % dyn, not dyn)
    pg.click("nav button[data-t=config]"); pg.wait_for_function("document.querySelector('#cf [name=decl]').value!=''")
    check("config loads select, decimal and checkbox values", pg.input_value("#cf [name=style]") == "0" and pg.input_value("#cf [name=decl]") == "4" and pg.is_checked("#cf [name=swapXY]") and not pg.is_checked("#cf [name=invY]"))
    check("WPA key field stays empty (never sent by the device)", pg.input_value("#cf [name=wpakey]") == "")
    check("group names: Display, Compass, Sensor mounting, WiFi, Debug and test", pg.eval_on_selector_all("#cf summary", "e=>e.map(x=>x.textContent).join('|')") == "Display|Compass|Sensor mounting|WiFi|Debug and test")
    pg.evaluate("document.querySelectorAll('#cf details').forEach(d=>d.open=true)")
    pg.fill("#cf [name=decl]", "-1.5"); pg.select_option("#cf [name=style]", "2"); pg.check("#cf [name=invY]")
    n = len(ms.posted); pg.click("#cf button[type=submit]"); pg.wait_for_function("document.querySelector('#cmsg').textContent.startsWith('Saved')")
    q = ms.posted[-1][1]
    check("save posts decimal, select and checkboxes", q["decl"] == ["-1.5"] and q["style"] == ["2"] and "invY" in q and q["wpakey"] == [""])
    pg.fill("#cf [name=decl]", "31"); pg.click("#cf button[type=submit]")
    check("declination above 30 is rejected by the browser", "highlighted" in pg.inner_text("#cmsg"))
    pg.fill("#cf [name=wpakey]", "abc"); pg.fill("#cf [name=decl]", "4")
    pg.click("#cf button[type=submit]")
    check("WPA key shorter than 8 characters is rejected by the browser", "highlighted" in pg.inner_text("#cmsg"))
    pg.fill("#cf [name=wpakey]", "")
    pg.screenshot(path="config_en.png", full_page=True)
    pg.click("#lang"); pg.click("nav button[data-t=status]")
    pg.screenshot(path="status_de.png", full_page=True)
    check("German status labels", "Kurs" in pg.inner_text("#status") and "Kalibriert" in pg.inner_text("#status"))
    pg.click("nav button[data-t=config]"); pg.evaluate("document.querySelectorAll('#cf details').forEach(d=>d.open=true)")
    pg.screenshot(path="config_de.png", full_page=True)
    check("German group names", "Anzeige" in pg.inner_text("#cf") and "Sensor-Einbaulage" in pg.inner_text("#cf"))
    check("no JavaScript errors", not errs)
print("\n%d checks, %d failed" % (len(res), res.count(False)))
sys.exit(1 if False in res else 0)
