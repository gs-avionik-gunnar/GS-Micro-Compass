#!/usr/bin/env python3
"""Mock of the device HTTP API for testing the WebUI on a PC.
Usage: python3 mock_server.py [index.html] [port]   then open http://127.0.0.1:8080/
Adapt STATUS / CONFIG to your product. The API contract is described in TEMPLATE_SPEC.md."""
import json, sys, http.server, socketserver, urllib.parse

HTML = sys.argv[1] if len(sys.argv) > 1 else "index.html"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
STATUS = {"fw": "2.0", "hw": "GS-Micro-Compass", "up": 3725, "heap": 180000, "minHeap": 150000, "led": 1, "heading": 124.4, "valid": True,
          "bug": 95, "style": 0, "sensor": "QMC5883L", "calibrated": True, "calActive": False, "spanX": 1180, "spanY": 1150,
          "rawX": -120, "rawY": 340, "rawZ": -900, "offset": 180, "decl": 4.0, "stations": 1}
CONFIG = {"ssid": "GS-MC-E5F6", "wifiBoot": 0, "dialBright": 80, "style": 0, "startPage": 0, "bug": 95, "offsetDeg": 180,
          "decl": 4.0, "smooth": 15, "swapXY": 1, "invX": 1, "invY": 0, "debug": 0}
posted = []   # every POST (path, parsed body) for tests

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _send(self, code, body, ctype="application/json"):
        b = body.encode()
        self.send_response(code); self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/": self._send(200, open(HTML, encoding="utf-8").read(), "text/html")
        elif p == "/api/status": self._send(200, json.dumps(STATUS))
        elif p == "/api/config": self._send(200, json.dumps(CONFIG))
        else: self._send(404, "not found", "text/plain")
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = self.rfile.read(n)
        if self.path == "/update":
            posted.append((self.path, len(body))); self._send(200, "OK", "text/plain"); return
        posted.append((self.path, urllib.parse.parse_qs(body.decode(errors="replace"), keep_blank_values=True)))
        if self.path == "/api/config":
            q = posted[-1][1]
            if q.get("dialBright", ["80"])[0] == "99": self._send(400, "Brightness must be a whole number from 10 to 98", "text/plain"); return
        self._send(200, "OK", "text/plain")

def serve(port=PORT):
    srv = socketserver.TCPServer(("127.0.0.1", port), Handler)
    return srv

if __name__ == "__main__":
    s = serve(); print("http://127.0.0.1:%d/" % s.server_address[1]); s.serve_forever()
