# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
import hashlib
import ipaddress
import json
import os
import sys

import httpx
import pytest

os.environ["WLT_NO_AUTOAPP"] = "1"
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fastapi.testclient import TestClient  # noqa: E402

from app.config import Config  # noqa: E402
from app.firmware import version_key  # noqa: E402
from app.main import create_app  # noqa: E402

FW_GAS1 = b"\xe9" + b"gas1" * 100
FW_GAS2 = b"\xe9" + b"gas2" * 100
FW_OFFICIAL = b"\xe9" + b"official" * 100


def github_handler(request: httpx.Request) -> httpx.Response:
    path = request.url.path
    if path == "/repos/me/fork/releases":
        def rel(tag, data, draft=False):
            return {"tag_name": tag, "draft": draft, "published_at": "2026-10-28T00:00:00Z", "assets": [
                {"name": "nanoV3.bin", "url": f"https://api.github.test/assets/{tag}"},
                {"name": "nanoV3.bin.sha256", "url": f"https://api.github.test/assets/{tag}.sha256"}]}
        return httpx.Response(200, json=[rel("v1.2.4-gas.2", FW_GAS2), rel("v1.2.4-gas.1", FW_GAS1),
                                         rel("v1.2.4-gas.3", FW_GAS2, draft=True)])
    if path == "/repos/up/stream/releases/tags/v1.2.4":
        return httpx.Response(200, json={"tag_name": "v1.2.4", "assets": [
            {"name": "nano_v3_esp32_firmware_v1.2.4.bin", "url": "https://api.github.test/assets/official"}]})
    blobs = {"/assets/v1.2.4-gas.1": FW_GAS1, "/assets/v1.2.4-gas.2": FW_GAS2, "/assets/official": FW_OFFICIAL}
    if path in blobs:
        return httpx.Response(200, content=blobs[path])
    if path.endswith(".sha256"):
        data = blobs[path[:-len(".sha256")]]
        return httpx.Response(200, text=hashlib.sha256(data).hexdigest() + "  nanoV3.bin\n")
    return httpx.Response(404)


class UpstreamRecorder:
    def __init__(self):
        self.requests = []

    def __call__(self, request: httpx.Request) -> httpx.Response:
        self.requests.append(request)
        body = json.loads(request.content)
        if "cloud" in body:
            return httpx.Response(200, json={"cloud": {"task": True}, "url": {"api": {"host": "evil"}}},
                                  headers={"Date": "Wed, 28 Oct 2026 12:00:00 GMT"})
        return httpx.Response(200, json={"notification": {"task": "ok"}},
                              headers={"Date": "Wed, 28 Oct 2026 12:00:00 GMT"})


@pytest.fixture()
def env(tmp_path):
    config = Config()
    config.data_dir = str(tmp_path)
    config.public_base_url = "http://192.168.178.250"
    config.github_repo = "me/fork"
    config.github_api = "https://api.github.test"
    config.upstream_repo = "up/stream"
    config.fetch_on_start = False
    config.allowed_nets = [ipaddress.ip_network("192.168.178.0/24"), ipaddress.ip_network("127.0.0.0/8")]
    recorder = UpstreamRecorder()
    gh = httpx.Client(transport=httpx.MockTransport(github_handler), follow_redirects=True)
    app = create_app(config, upstream_transport=httpx.MockTransport(recorder), github_client=gh)
    app.state.firmware.poll()
    client = TestClient(with_client(app, "192.168.178.40"))
    return app, client, recorder


def with_client(app, host):
    """ASGI wrapper that sets the client address (TestClient of starlette 0.41 cannot)"""
    async def wrapper(scope, receive, send):
        if scope["type"] in ("http", "websocket"):
            scope = dict(scope, client=(host, 50000))
        await app(scope, receive, send)
    return wrapper


def device(sw="v1.2.4", dev="nano", hw="v3"):
    return {"device": dev, "serial": "abcdef012345", "cpu": "esp32", "flash_size": 16777216,
            "hw_version": hw, "sw_version": sw, "api_version": "1", "language": "de"}


def update_req(sw="v1.2.4", **update):
    body = {"device": device(sw), "update": {"prerelease": False}, "url": {}}
    body["update"].update(update)
    return body


def test_version_order():
    versions = ["v1.2.5", "v1.2.4-gas.10", "v1.2.4", "v1.2.4-gas.2"]
    assert sorted(versions, key=version_key) == ["v1.2.4", "v1.2.4-gas.2", "v1.2.4-gas.10", "v1.2.5"]


def test_poll_caches_releases_and_rollback(env):
    app, client, _ = env
    files = sorted(app.state.firmware.index)
    assert files == ["nanoV3-v1.2.4-gas.1.bin", "nanoV3-v1.2.4-gas.2.bin", "nanoV3-v1.2.4.bin"]
    assert app.state.firmware.last_error == ""


def test_update_offers_latest_fork_release(env):
    _, client, recorder = env
    r = client.post("/", json=update_req("v1.2.4"))
    assert r.status_code == 200
    assert r.json() == {"update": {"available": True, "version": "v1.2.4-gas.2",
                                   "firmware": {"url": "http://192.168.178.250/fw/nanoV3-v1.2.4-gas.2.bin"}}}
    assert "url" not in r.json()
    assert recorder.requests == []  # never forwarded


def test_update_not_available_when_current(env):
    _, client, _ = env
    r = client.post("/", json=update_req("v1.2.4-gas.2"))
    assert r.json() == {"update": {"available": False}}


def test_requested_version_and_force(env):
    _, client, _ = env
    r = client.post("/", json=update_req("v1.2.4-gas.2", version="v1.2.4", force=True))
    u = r.json()["update"]
    assert u["available"] is True and u["version"] == "v1.2.4" and u["force"] is True
    assert u["firmware"]["url"].endswith("/fw/nanoV3-v1.2.4.bin")
    r = client.post("/", json=update_req("v1.2.4", version="v1.2.4-gas.1"))
    u = r.json()["update"]
    assert u["version"] == "v1.2.4-gas.1" and "force" not in u
    r = client.post("/", json=update_req("v1.2.4", version="v9.9.9"))
    assert r.json() == {"update": {"available": False}}


def test_no_force_key_without_force(env):
    _, client, _ = env
    r = client.post("/", json=update_req("v1.2.4", force=False))
    assert "force" not in r.json()["update"]


def test_display_file_and_unknown_device(env):
    _, client, _ = env
    assert client.post("/", json=update_req(file="NX3224T028-0")).json() == {"update": {"available": False}}
    body = update_req()
    body["device"] = device(dev="mini", hw="v1")
    assert client.post("/", json=body).json() == {"update": {"available": False}}


def test_firmware_download(env):
    _, client, _ = env
    r = client.get("/fw/nanoV3-v1.2.4-gas.2.bin")
    assert r.status_code == 200 and r.content == FW_GAS2
    assert r.headers["content-type"] == "application/octet-stream"
    assert int(r.headers["content-length"]) == len(FW_GAS2)
    assert client.get("/fw/../wlt.sqlite").status_code == 404
    assert client.get("/fw/unknown.bin").status_code == 404


def test_proxy_forwards_with_headers_and_strips_url(env):
    app, client, recorder = env
    body = {"device": device(), "cloud": {"task": "save", "api_token": "x", "data": [
        {"system": {}, "channel": [{"number": 1, "temp": 128.5}, {"number": 2, "temp": 999}],
         "pitmaster": [{"id": 0, "channel": 1, "typ": "auto", "set": 130, "value": 22}]}]}}
    r = client.post("/", content=json.dumps(body), headers={"SN": "abcdef012345", "User-Agent": "WLANThermo ESP32",
                                                            "Content-Type": "application/json"})
    assert r.status_code == 200
    assert r.json() == {"cloud": {"task": True}}  # url removed
    assert r.headers["date"] == "Wed, 28 Oct 2026 12:00:00 GMT"
    fwd = recorder.requests[0]
    assert fwd.headers["SN"] == "abcdef012345"
    assert fwd.headers["User-Agent"] == "WLANThermo ESP32"
    assert fwd.headers["Host"] == "api.wlanthermo.de"
    assert json.loads(fwd.content) == body
    # cloud data recorded as session
    sessions = client.get("/sessions").json()
    assert len(sessions) == 1 and sessions[0]["cloud_samples"] == 1
    cloud_csv = client.get(f"/sessions/{sessions[0]['id']}/cloud.csv").text.splitlines()
    assert cloud_csv[0].startswith("ts,iso,source,ch1,ch2")
    assert ",128.5,," in cloud_csv[1]


def test_proxy_notification_and_time(env):
    _, client, recorder = env
    note = {"device": device(), "notification_v2": {"message": {"type": 1, "channel": 0, "reason": "flameout"},
                                                    "services": []}}
    assert client.post("/", json=note).json() == {"notification": {"task": "ok"}}
    assert client.post("/", json={"device": device()}).status_code == 200
    assert len(recorder.requests) == 2


def test_upstream_down_time_request_still_answers(tmp_path):
    config = Config()
    config.data_dir = str(tmp_path)
    config.fetch_on_start = False
    config.allowed_nets = [ipaddress.ip_network("0.0.0.0/0")]

    def down(request):
        raise httpx.ConnectError("down")

    app = create_app(config, upstream_transport=httpx.MockTransport(down))
    client = TestClient(with_client(app, "192.168.178.40"))
    r = client.post("/", json={"device": device()})
    assert r.status_code == 200 and r.json() == {} and "date" in r.headers
    assert client.post("/", json={"device": device(), "cloud": {"data": []}}).status_code == 502


def batch(start_ms, n, set_=130.0, now=None, typ=2):
    cols = ["ms", "pm", "type", "raw", "temp", "set", "out", "servo_us", "p", "i", "d", "esum", "flags",
            "at_state", "at_phase", "at_cycle"]
    rows = [[start_ms + 1000 * k, 0, typ, 120.0 + k * 0.1, 120.0 + k * 0.1, set_, 30.0, 1200, 5, 20, 1, 2000, 0, 0, 0, 0]
            for k in range(n)]
    return {"sn": "abcdef012345", "fw": "v1.2.4-gas.1", "now": now or 0, "now_ms": start_ms + 1000 * (n - 1),
            "heap": 100000, "loop_us_max": 900, "dropped": 0, "failed": 0, "interval": 1, "cols": cols, "rows": rows}


def test_telemetry_sessions_and_csv(env):
    app, client, _ = env
    import time as _t
    now = _t.time()
    assert client.post("/telemetry", json=batch(10_000, 10, now=now)).json() == {}
    assert client.post("/telemetry", json=batch(20_000, 10, now=now + 10)).json() == {}
    sessions = client.get("/sessions").json()
    assert len(sessions) == 1 and sessions[0]["telemetry_samples"] == 20
    text = client.get(f"/sessions/{sessions[0]['id']}.csv").text.splitlines()
    assert text[0] == "ts,iso,pm,type,raw,temp,set,out,servo_us,p,i,d,esum,flags,at_state,at_phase,at_cycle"
    assert len(text) == 21
    # 31 min pause -> new session
    client.post("/telemetry", json=batch(30_000, 5, now=now + 10 + 31 * 60))
    assert len(client.get("/sessions").json()) == 2
    # pitmaster off and cold: not part of a cook
    cold = batch(40_000, 5, now=now + 40 * 60, typ=0)
    for row in cold["rows"]:
        row[3] = 22.0
    client.post("/telemetry", json=cold)
    assert sum(s["telemetry_samples"] for s in client.get("/sessions").json()) == 25
    assert client.get("/sessions/999.csv").status_code == 404


def test_custom_cloud_answer_is_empty(env):
    _, client, _ = env
    r = client.post("/custom", json={"version": 1, "interval": 30, "channel": [{"number": 1, "temp": 110.2}]},
                    headers={"SN": "abcdef012345"})
    assert r.json() == {}


def test_only_home_network(env):
    app, _, _ = env
    outside = TestClient(with_client(app, "10.1.2.3"))
    assert outside.post("/", json=update_req()).status_code == 403
    assert outside.get("/sessions").status_code == 403


def test_health_and_request_log(env):
    _, client, _ = env
    client.post("/", json=update_req())
    h = client.get("/health").json()
    assert h["ok"] and h["latest"] == {"nanoV3": "v1.2.4-gas.2"}
    log = client.get("/requests").json()
    assert log[0]["kind"] == "update" and "offer v1.2.4-gas.2" in log[0]["summary"]


def test_manual_import(env):
    app, client, _ = env
    imp = os.path.join(app.state.config.data_dir, "fw", "import")
    with open(os.path.join(imp, "nanoV3-v1.2.4-gas.7.bin"), "wb") as f:
        f.write(b"\xe9" + b"manual" * 10)
    with open(os.path.join(imp, "nanoV3-v1.2.4-gas.8.bin"), "wb") as f:
        f.write(b"no esp image")
    app.state.firmware.poll()
    assert client.post("/", json=update_req()).json()["update"]["version"] == "v1.2.4-gas.7"
