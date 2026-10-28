# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
"""wlt-update: update server emulator and API proxy for WLANThermo devices.

The device sends every API request as plain HTTP POST to http://api.wlanthermo.de/
(src/Cloud.cpp). With a DNS override this container receives them:
  - requests with an "update" object are answered here (own firmware releases)
  - everything else is forwarded 1:1 to the real api.wlanthermo.de
Never sends a "url" object (it would rewrite the hosts in the device for good).
"""
import email.utils
import ipaddress
import json
import logging
import os
import time
from typing import Optional

import dns.resolver
import httpx
from fastapi import FastAPI, Request
from fastapi.responses import FileResponse, JSONResponse, PlainTextResponse, Response

from .config import Config
from .firmware import FirmwareStore
from .store import Store

logging.basicConfig(level=os.environ.get("LOG_LEVEL", "INFO"),
                    format="%(asctime)s %(levelname)s %(name)s %(message)s")
log = logging.getLogger("wlt.update")


class Upstream:
    """Forwards requests to the real API server. The host is resolved with fixed
    public resolvers, not with the home network DNS (that would point to us)."""

    def __init__(self, config: Config, transport: Optional[httpx.BaseTransport] = None):
        self.config = config
        self.transport = transport
        self._ip = None
        self._ip_until = 0.0

    def resolve(self) -> str:
        if self._ip and time.time() < self._ip_until:
            return self._ip
        resolver = dns.resolver.Resolver(configure=False)
        resolver.nameservers = self.config.upstream_dns
        resolver.lifetime = 5.0
        answer = resolver.resolve(self.config.upstream_api_host, "A")
        ip = answer[0].to_text()
        own = self.config.public_base_url.split("//")[-1].split(":")[0].split("/")[0]
        if ip == own:
            raise RuntimeError("upstream resolves to this server (DNS loop)")
        self._ip = ip
        self._ip_until = time.time() + max(60, min(answer.rrset.ttl, 3600))
        return ip

    def post(self, body: bytes, headers: dict) -> httpx.Response:
        if self.transport is not None:
            url = f"http://{self.config.upstream_api_host}/"
        else:
            url = f"http://{self.resolve()}/"
        fwd = {"Host": self.config.upstream_api_host, "Content-Type": "application/json", "Connection": "close"}
        lower = {k.lower(): v for k, v in headers.items()}
        for name in ("SN", "User-Agent"):
            if name.lower() in lower:
                fwd[name] = lower[name.lower()]
        with httpx.Client(timeout=self.config.upstream_timeout_s, transport=self.transport) as client:
            return client.post(url, content=body, headers=fwd)


def request_kind(body: dict) -> str:
    for key, kind in (("update", "update"), ("cloud", "cloud"), ("notification_v2", "notification"),
                      ("crash_report", "crash")):
        if key in body:
            return kind
    return "time" if set(body.keys()) <= {"device"} else "other"


def create_app(config: Optional[Config] = None, upstream_transport: Optional[httpx.BaseTransport] = None,
               github_client: Optional[httpx.Client] = None) -> FastAPI:
    config = config or Config.from_env()
    store = Store(config.data_dir, config.session_gap_s)
    firmware = FirmwareStore(config, github_client)
    upstream = Upstream(config, upstream_transport)
    app = FastAPI(title="wlt-update", docs_url=None, redoc_url=None, openapi_url=None)
    app.state.config = config
    app.state.store = store
    app.state.firmware = firmware

    if config.fetch_on_start:
        firmware.start_polling()

    @app.middleware("http")
    async def only_home_network(request: Request, call_next):
        client = request.client.host if request.client else ""
        try:
            addr = ipaddress.ip_address(client)
            allowed = any(addr in net for net in config.allowed_nets)
        except ValueError:
            allowed = client == "testclient"  # FastAPI TestClient
        if not allowed:
            log.warning("rejected client %s %s", client, request.url.path)
            return PlainTextResponse("forbidden", status_code=403)
        return await call_next(request)

    # ------------------------------------------------------------------
    # update answer
    # ------------------------------------------------------------------
    def update_answer(body: dict) -> dict:
        device = body.get("device", {}) or {}
        update = body.get("update", {}) or {}
        key = f"{device.get('device', '')}:{device.get('hw_version', '')}"
        env = config.device_map.get(key)
        sw = str(device.get("sw_version", ""))

        if env is None:
            return {"update": {"available": False}}, f"unknown device {key}"

        # display files (Nextion) are not provided. The Nano V3 has an OLED and
        # never requests one (DisplayBase::getUpdateName() is empty there).
        if update.get("file"):
            return {"update": {"available": False}}, f"file {update.get('file')} not provided"

        requested = update.get("version")
        if requested and requested != "false":
            fw = firmware.find(env, str(requested))
            if fw is None:
                return {"update": {"available": False}}, f"requested {requested} not available"
        else:
            fw = firmware.latest(env)
            if fw is None:
                return {"update": {"available": False}}, "no firmware cached"
            if fw["version"] == sw:
                return {"update": {"available": False}}, f"up to date ({sw})"

        answer = {"update": {
            "available": True,
            "version": fw["version"],
            "firmware": {"url": f"{config.public_base_url}/fw/{firmware.file_name(env, fw['version'])}"},
        }}
        # the device starts the update when the key exists, whatever the value
        if update.get("force"):
            answer["update"]["force"] = True
        return answer, f"offer {fw['version']} (device {sw}, force={bool(update.get('force'))})"

    @app.middleware("http")
    async def date_header(request: Request, call_next):
        # the device sets its clock from the Date header of the API answer
        response = await call_next(request)
        if "date" not in response.headers:
            response.headers["Date"] = email.utils.formatdate(usegmt=True)
        return response

    @app.post("/")
    async def api(request: Request):
        raw = await request.body()
        client = request.client.host if request.client else ""
        try:
            body = json.loads(raw or b"{}")
            if not isinstance(body, dict):
                raise ValueError("not an object")
        except ValueError:
            store.log_request(client, {}, "invalid", 400, raw[:200].decode(errors="replace"))
            return JSONResponse({}, status_code=400)

        kind = request_kind(body)
        dev = body.get("device", {})
        if kind == "update":
            answer, summary = update_answer(body)
            store.log_request(client, body, kind, 200, summary)
            log.info("update %s %s %s: %s", dev.get("serial"), dev.get("device"), dev.get("sw_version"), summary)
            return JSONResponse(answer)

        # cloud data passing through is also recorded for the analysis
        if kind == "cloud":
            try:
                for entry in body["cloud"].get("data", []):
                    store.add_cloud(str(dev.get("serial", "unknown")), "cloud", entry.get("channel", []),
                                    entry.get("pitmaster", []))
            except (AttributeError, TypeError, ValueError):
                log.warning("cloud data not recorded")

        try:
            resp = upstream.post(raw, dict(request.headers))
        except Exception as e:  # noqa: BLE001
            store.log_request(client, body, kind, 502, f"upstream error: {e}")
            log.warning("upstream error for %s: %s", kind, e)
            if kind == "time":
                # the device takes the time from the HTTP Date header of any answer
                return JSONResponse({})
            return JSONResponse({}, status_code=502)

        content = resp.content
        try:
            answer = resp.json()
            if isinstance(answer, dict):
                # never let a proxied answer rewrite hosts or trigger updates
                removed = [k for k in ("url", "update") if k in answer]
                for k in removed:
                    answer.pop(k)
                if removed:
                    log.info("removed %s from upstream answer", removed)
                content = json.dumps(answer).encode()
        except ValueError:
            pass

        store.log_request(client, body, kind, resp.status_code, content[:200].decode(errors="replace"))
        log.info("proxy %s %s -> %d", kind, dev.get("serial"), resp.status_code)
        headers = {}
        if "date" in resp.headers:
            headers["Date"] = resp.headers["date"]
        return Response(content=content, status_code=resp.status_code, media_type="application/json", headers=headers)

    # ------------------------------------------------------------------
    # firmware files (plain http, the device has no TLS)
    # ------------------------------------------------------------------
    @app.get("/fw/{name}")
    def fw(name: str, request: Request):
        path = firmware.path_for(name)
        if path is None:
            return PlainTextResponse("not found", status_code=404)
        log.info("firmware download %s by %s", name, request.client.host if request.client else "")
        return FileResponse(path, media_type="application/octet-stream", filename=name)

    @app.get("/firmware")
    def firmware_list():
        return {"index": firmware.index, "last_poll": firmware.last_poll, "last_error": firmware.last_error}

    @app.post("/admin/poll")
    def admin_poll():
        firmware.poll()
        return {"last_error": firmware.last_error, "index": sorted(firmware.index)}

    # ------------------------------------------------------------------
    # telemetry and custom cloud
    # ------------------------------------------------------------------
    @app.post("/telemetry")
    async def telemetry(request: Request):
        try:
            batch = json.loads(await request.body())
        except ValueError:
            return JSONResponse({}, status_code=400)
        stored = store.add_telemetry(batch)
        log.debug("telemetry %s: %d rows", batch.get("sn"), stored)
        # empty object on purpose: the device must not act on telemetry answers
        return JSONResponse({})

    @app.post("/custom")
    async def custom(request: Request):
        """target for the device setting 'own cloud server' (customUrl).
        The device parses this answer like an API answer, so it stays empty."""
        try:
            body = json.loads(await request.body())
            sn = request.headers.get("SN") or request.query_params.get("sn") or "custom"
            store.add_cloud(sn, "custom", body.get("channel", []), None)
        except (ValueError, AttributeError):
            pass
        return JSONResponse({})

    @app.get("/sessions")
    def sessions():
        return store.sessions()

    @app.get("/sessions/{session_id}.csv")
    def session_csv(session_id: int):
        text = store.telemetry_csv(session_id)
        if text is None:
            return PlainTextResponse("not found", status_code=404)
        return PlainTextResponse(text, media_type="text/csv")

    @app.get("/sessions/{session_id}/cloud.csv")
    def session_cloud_csv(session_id: int):
        text = store.cloud_csv(session_id)
        if text is None:
            return PlainTextResponse("not found", status_code=404)
        return PlainTextResponse(text, media_type="text/csv")

    @app.get("/requests")
    def requests_log(limit: int = 100):
        return store.recent_requests(limit)

    @app.get("/health")
    def health():
        env_latest = {env: (firmware.latest(env) or {}).get("version") for env in config.fork_asset}
        return {"ok": True, "latest": env_latest, "files": sorted(firmware.index), "last_poll": firmware.last_poll,
                "last_error": firmware.last_error, "batches": store.batch_stats(3)}

    return app


app = create_app() if os.environ.get("WLT_NO_AUTOAPP") != "1" else None
