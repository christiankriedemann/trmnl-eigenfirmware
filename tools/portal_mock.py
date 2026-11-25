"""Nachbau der Geraete-Schnittstelle, um das Webinterface am Rechner zu testen.

  python3 tools/portal_mock.py [port]   ->  http://localhost:8765
Liefert src/portal/index.html und erfundene Daten; Aenderungen bleiben im Speicher.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

BASE = Path(__file__).resolve().parent.parent
STATE = {
    "version": "0.3.0", "sta": "Zuhause", "last": "25.11. 18:19", "err": "", "ota": "Version 0.3.0 läuft",
    "akku": 64, "ap": "TRMNL-3F2A", "ort": "Bonn",
    "wifis": ["Zuhause", "Handy-Hotspot"],
    "cfg": {"g_user": "name@firma.de", "g_email": "display@projekt.iam.gserviceaccount.com",
            "g_cals": "name@firma.de", "tz_home": "Europe/Berlin", "loc_mode": "auto", "day_min": "10",
            "slots": "7", "sleep_h": "23", "wake_h": "6", "safe_bot": "456", "ms_client": "", "ota_auto": "1"},
    "secrets": {"g_key": True, "td_token": True, "ms_refresh": False},
}


class H(BaseHTTPRequestHandler):
    def _json(self, obj, code=200):
        b = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        u = urlparse(self.path)
        if u.path == "/":
            b = (BASE / "src/portal/index.html").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(b)
        elif u.path == "/api/screen":
            b = (BASE / "preview/out/sample-heute.png").read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.end_headers()
            self.wfile.write(b)
        elif u.path == "/api/state":
            self._json(STATE)
        elif u.path == "/api/scan":
            self._json([{"ssid": "Zuhause", "rssi": -48, "offen": False}, {"ssid": "Handy-Hotspot", "rssi": -63, "offen": False},
                        {"ssid": "Nachbar 5G", "rssi": -80, "offen": False}, {"ssid": "Café Gäste", "rssi": -71, "offen": True}])
        elif u.path == "/api/cals":
            self._json([{"id": "name@firma.de", "name": "Mein Kalender", "primary": True},
                        {"id": "team@firma.de", "name": "Team", "primary": False}, {"id": "feiertage", "name": "Feiertage", "primary": False}])
        elif u.path in ("/api/mscals", "/api/mslists"):
            self._json([{"id": "a", "name": "Kalender", "primary": True}, {"id": "b", "name": "Aufgaben", "flagged": False},
                        {"id": "c", "name": "Gekennzeichnete E-Mails", "flagged": True}])
        elif u.path == "/api/geocode":
            q = parse_qs(u.query).get("q", [""])[0]
            self._json([{"name": q.title() or "Bonn", "region": "Nordrhein-Westfalen", "land": "Deutschland", "lat": 50.73, "lon": 7.1},
                        {"name": q.title() + " (Ort)", "region": "Bayern", "land": "Deutschland", "lat": 48.1, "lon": 11.5}])
        else:
            self._json({"error": "unbekannt"}, 404)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n).decode() if n else ""
        if self.headers.get("X-TRMNL") != "1":
            return self._json({"error": "Header fehlt"}, 403)
        p = urlparse(self.path).path
        if p == "/api/set":
            for k, v in json.loads(body).items():
                STATE["cfg"][k] = v
        elif p == "/api/wifi":
            s = json.loads(body)["ssid"]
            if s not in STATE["wifis"]: STATE["wifis"].append(s)
        elif p == "/api/wifi/forget":
            s = json.loads(body)["ssid"]
            STATE["wifis"] = [w for w in STATE["wifis"] if w != s]
        elif p == "/api/wifi/move":
            d = json.loads(body); w = STATE["wifis"]; i = w.index(d["ssid"]); j = i + (1 if d["dir"] > 0 else -1)
            if 0 <= j < len(w): w[i], w[j] = w[j], w[i]
        elif p == "/api/mslogout":
            STATE["secrets"]["ms_refresh"] = False
        self._json({"ok": True})

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
