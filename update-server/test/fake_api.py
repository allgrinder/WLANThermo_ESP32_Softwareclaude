# fake api.wlanthermo.de for the integration test
import http.server, json
class H(http.server.BaseHTTPRequestHandler):
    seen = []
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        H.seen.append({"sn": self.headers.get("SN"), "ua": self.headers.get("User-Agent"), "host": self.headers.get("Host"), "body": json.loads(body)})
        out = json.dumps({"cloud": {"task": True}, "url": {"api": {"host": "evil.example"}}, "update": {"available": True}}).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(out))); self.end_headers(); self.wfile.write(out)
    def do_GET(self):
        out = json.dumps(H.seen).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(out))); self.end_headers(); self.wfile.write(out)
    def log_message(self, *a): pass
http.server.HTTPServer(("0.0.0.0", 80), H).serve_forever()
