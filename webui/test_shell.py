#!/usr/bin/env python3
"""Browser test of the WebUI shell (Playwright). Usage: python3 test_shell.py [index.html]
Checks the behaviour that must be identical in all products: language switch, help toggle,
collapsible groups, save/reset, validation, firmware update. Extend it with your product checks."""
import sys, threading, tempfile, os
from playwright.sync_api import sync_playwright
import mock_server as ms

ms.HTML = sys.argv[1] if len(sys.argv) > 1 else "index.html"
srv = ms.serve(0); port = srv.server_address[1]
threading.Thread(target=srv.serve_forever, daemon=True).start()
res = []
def check(name, cond): res.append(bool(cond)); print(("PASS " if cond else "FAIL ") + name)

with sync_playwright() as p:
    b = p.chromium.launch(); ctx = b.new_context(locale="en-US", viewport={"width": 390, "height": 800}); pg = ctx.new_page()
    errs = []; pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.on("console", lambda m: errs.append(m.text) if m.type == "error" and "400" not in m.text else None)
    pg.on("dialog", lambda d: d.accept())
    pg.goto(f"http://127.0.0.1:{port}/"); pg.wait_for_function("document.querySelector('#conn').textContent=='online'")
    check("status shows firmware, hardware and uptime", "GS-Micro-Compass" in pg.inner_text("#sys") and "1h 2m 5s" in pg.inner_text("#sys"))
    check("tabs: Status, Configuration, Firmware in this order", pg.eval_on_selector_all("nav button", "e=>e.map(x=>x.textContent).join('|')") == "Status|Configuration|Firmware")
    check("default language follows the browser (English)", pg.inner_text("#lang") == "Deutsch")
    pg.click("#lang")
    check("switch to German translates tabs, tag and buttons", pg.inner_text("nav") .split("\n")[1] == "Konfiguration" and pg.inner_text("#conn") == "online" and pg.inner_text("#lang") == "English")
    pg.click("nav button[data-t=update]")
    check("firmware page: 'Running' is translated to 'Aktuell'", "Aktuell" in pg.inner_text("#update"))
    pg.click("#lang"); check("switch back to English", pg.inner_text("nav").split("\n")[1] == "Configuration")
    pg.reload(); pg.wait_for_function("document.querySelector('#conn').textContent=='online'")
    check("language is remembered", pg.inner_text("#lang") == "Deutsch")
    pg.click("nav button[data-t=config]"); pg.wait_for_function("document.querySelector('#cf [name=dialBright]').value=='80'")
    check("all settings groups are collapsed details", pg.evaluate("[...document.querySelectorAll('#cf details')].length>=2&&[...document.querySelectorAll('#cf details')].every(d=>!d.open)"))
    check("every field has a help text", pg.evaluate("[...document.querySelectorAll('#cf .f')].every(f=>f.querySelector('.help'))"))
    pg.evaluate("document.querySelectorAll('#cf details').forEach(d=>d.open=true)")
    check("help texts visible by default", pg.is_visible("#cf .help"))
    pg.uncheck("#showhelp"); check("help switch hides help texts", not pg.is_visible("#cf .help"))
    pg.reload(); pg.click("nav button[data-t=config]"); pg.wait_for_function("document.querySelector('#cf [name=dialBright]').value=='80'")
    pg.evaluate("document.querySelectorAll('#cf details').forEach(d=>d.open=true)")
    check("help switch is remembered", not pg.is_visible("#cf .help") and not pg.is_checked("#showhelp"))
    pg.check("#showhelp")
    pg.evaluate("document.querySelectorAll('#cf details').forEach(d=>d.open=false)")
    pg.evaluate("document.querySelector('#cf [name=dialBright]').value='500'"); pg.click("#cf button[type=submit]")
    check("invalid value: message shown, group opens automatically", "highlighted" in pg.inner_text("#cmsg") or "markierten" in pg.inner_text("#cmsg"))
    check("invalid value opens its details group", pg.evaluate("document.querySelector('#cf [name=dialBright]').closest('details').open"))
    n = len(ms.posted); pg.fill("#cf [name=dialBright]", "99"); pg.click("#cf button[type=submit]")
    pg.wait_for_function("document.querySelector('#cmsg').textContent.startsWith('Error') || document.querySelector('#cmsg').textContent.startsWith('Fehler')")
    check("server error text is shown", "from 10 to 98" in pg.inner_text("#cmsg"))
    pg.fill("#cf [name=dialBright]", "20"); pg.uncheck("#cf [name=wifiBoot]"); pg.click("#cf button[type=submit]")
    pg.wait_for_function("document.querySelector('#cmsg').textContent.startsWith('Saved')")
    q = ms.posted[-1][1]
    check("save posts the form (checkbox only when on)", ms.posted[-1][0] == "/api/config" and q["dialBright"] == ["20"] and "wifiBoot" not in q and "showhelp" not in q)
    pg.click("#rst"); pg.wait_for_function("document.querySelector('#cmsg').textContent.startsWith('Reset')")
    check("reset posts /api/reset", ms.posted[-1][0] == "/api/reset")
    pg.click("nav button[data-t=update]"); pg.click("#go")
    check("update without file asks for a file", "Select a file" in pg.inner_text("#prg"))
    f = tempfile.NamedTemporaryFile(suffix=".bin", delete=False); f.write(b"x" * 5000); f.close()
    pg.set_input_files("#bin", f.name); pg.click("#go"); pg.wait_for_function("document.querySelector('#prg').textContent.includes('Update OK')")
    check("firmware upload posts to /update", ms.posted[-1][0] == "/update" and ms.posted[-1][1] > 5000); os.unlink(f.name)
    pg.screenshot(path="shell.png", full_page=True)
    check("no JavaScript errors", not errs)
    if errs: print(errs)
    b.close()
print("\n%d checks, %d failed" % (len(res), res.count(False))); sys.exit(1 if False in res else 0)
