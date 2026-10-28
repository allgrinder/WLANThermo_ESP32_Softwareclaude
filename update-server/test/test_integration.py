# Integration test: DNS override, update path, firmware download, proxy path.
import sys, time
import dns.resolver, dns.rdatatype
import httpx

WLT_DNS, WLT_UPDATE = "172.30.0.11", "172.30.0.10"
failures = 0

def check(ok, what):
    global failures
    print(("ok:   " if ok else "FAIL: ") + what, flush=True)
    failures += 0 if ok else 1

def resolver(ns):
    r = dns.resolver.Resolver(configure=False); r.nameservers = [ns]; r.lifetime = 3
    return r

# wait for services
for _ in range(60):
    try:
        if httpx.get(f"http://{WLT_UPDATE}/health", timeout=2).status_code == 200:
            break
    except Exception:
        pass
    time.sleep(1)

# --- DNS ---
a = resolver(WLT_DNS).resolve("api.wlanthermo.de", "A")
check(a[0].to_text() == WLT_UPDATE, f"dig @wlt-dns api.wlanthermo.de -> {a[0].to_text()}")
check(a.rrset.ttl <= 60, f"override TTL {a.rrset.ttl} <= 60")
o = resolver(WLT_DNS).resolve("example.com", "A")
check(o[0].to_text() == "203.0.113.7", f"dig @wlt-dns example.com forwarded -> {o[0].to_text()}")
try:
    v6 = resolver(WLT_DNS).resolve("api.wlanthermo.de", "AAAA")
    addrs = [x.to_text() for x in v6]
    check(addrs == ["::"], f"AAAA api.wlanthermo.de not leaking a real address ({addrs})")
except (dns.resolver.NoAnswer, dns.resolver.NXDOMAIN):
    check(True, "AAAA api.wlanthermo.de: no answer")

# --- update path (as the device: plain POST to the resolved host) ---
host = a[0].to_text()
dev = {"device": "nano", "serial": "abcdef012345", "cpu": "esp32", "flash_size": 16777216, "hw_version": "v3",
       "sw_version": "v1.2.4", "api_version": "1", "language": "de"}
hdr = {"Host": "api.wlanthermo.de", "SN": "abcdef012345", "User-Agent": "WLANThermo ESP32", "Content-Type": "application/json"}
r = httpx.post(f"http://{host}/", json={"device": dev, "update": {"prerelease": False}, "url": {}}, headers=hdr)
u = r.json()["update"]
check(u.get("available") is True and u.get("version") == "v1.2.4-gas.1", f"update offered: {u}")
check("url" not in r.json(), "no url object in update answer")
fw = httpx.get(u["firmware"]["url"])
check(fw.status_code == 200 and fw.content.startswith(b"\xe9") and fw.headers["content-type"] == "application/octet-stream",
      f"firmware download {u['firmware']['url']} ({len(fw.content)} bytes)")
r = httpx.post(f"http://{host}/", json={"device": dict(dev, sw_version="v1.2.4-gas.1"), "update": {}}, headers=hdr)
check(r.json() == {"update": {"available": False}}, "up to date -> available false")
r = httpx.post(f"http://{host}/", json={"device": dict(dev, sw_version="v1.2.4-gas.1"),
                                         "update": {"version": "v1.2.4", "force": True}}, headers=hdr)
check(r.json()["update"].get("version") == "v1.2.4" and r.json()["update"].get("force") is True, "rollback v1.2.4 with force")

# --- proxy path ---
r = httpx.post(f"http://{host}/", json={"device": dev, "cloud": {"task": "save", "data": [
    {"channel": [{"number": 1, "temp": 131.0}], "pitmaster": [{"typ": "auto", "set": 130, "value": 20, "channel": 1}]}]}}, headers=hdr)
check(r.status_code == 200 and r.json() == {"cloud": {"task": True}}, f"cloud proxied, url/update removed: {r.json()}")
seen = httpx.get("http://172.30.0.30/").json()
check(len(seen) == 1 and seen[0]["sn"] == "abcdef012345" and seen[0]["ua"] == "WLANThermo ESP32"
      and seen[0]["host"] == "api.wlanthermo.de", f"upstream saw SN/User-Agent/Host: {seen[:1]}")
r = httpx.post(f"http://{host}/", json={"device": dev}, headers=hdr)
check(r.status_code == 200 and "date" in r.headers, "time request answered with Date header")

# --- logger ---
s = httpx.get(f"http://{WLT_UPDATE}/sessions").json()
check(len(s) == 1 and s[0]["cloud_samples"] == 1, f"cloud data recorded as session: {s}")
h = httpx.get(f"http://{WLT_UPDATE}/health").json()
check(h["ok"] and h["latest"]["nanoV3"] == "v1.2.4-gas.1", f"health: latest {h['latest']}, poll error recorded: {bool(h['last_error'])}")

print("FAILED" if failures else "ALL PASSED", flush=True)
sys.exit(1 if failures else 0)
