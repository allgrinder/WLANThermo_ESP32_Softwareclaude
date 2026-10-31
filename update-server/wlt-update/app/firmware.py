# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
"""Firmware cache: fork release assets (polled from GitHub) and the official
firmware for rollback. Files: <data>/fw/<env>-<version>.bin, index.json."""
import hashlib
import json
import logging
import os
import re
import threading
import time
from typing import Dict, List, Optional

import httpx

from .config import Config

log = logging.getLogger("wlt.firmware")

_SAFE = re.compile(r"^[A-Za-z0-9._-]+$")


def version_key(version: str):
    """Order: numeric parts of x.y.z, then the gas build number.
    v1.2.4 < v1.2.4-gas.1 < v1.2.4-gas.2 < v1.2.5"""
    m = re.match(r"^v(\d+)\.(\d+)\.(\d+)(?:-gas\.(\d+))?", version)
    if not m:
        return (0, 0, 0, -1, version)
    gas = int(m.group(4)) if m.group(4) is not None else -1
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), gas, version)


class FirmwareStore:
    def __init__(self, config: Config, client: Optional[httpx.Client] = None):
        self.config = config
        self.dir = os.path.join(config.data_dir, "fw")
        os.makedirs(self.dir, exist_ok=True)
        self.index_path = os.path.join(self.dir, "index.json")
        self.lock = threading.Lock()
        self.client = client
        self.last_poll = 0.0
        self.last_error = ""
        self.index: Dict[str, dict] = self._load_index()

    # ---- index ----
    def _load_index(self) -> Dict[str, dict]:
        if os.path.exists(self.index_path):
            with open(self.index_path) as f:
                return json.load(f)
        return {}

    def _save_index(self):
        tmp = self.index_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.index, f, indent=1, sort_keys=True)
        os.replace(tmp, self.index_path)

    @staticmethod
    def file_name(env: str, version: str) -> str:
        return f"{env}-{version}.bin"

    def add_file(self, env: str, version: str, data: bytes, source: str, published: str = ""):
        if not (_SAFE.match(env) and _SAFE.match(version)):
            raise ValueError("unsafe name")
        name = self.file_name(env, version)
        path = os.path.join(self.dir, name)
        tmp = path + ".tmp"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
        with self.lock:
            self.index[name] = {
                "env": env,
                "version": version,
                "source": source,
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "published": published,
                "added": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            }
            self._save_index()
        log.info("firmware stored: %s (%s, %d bytes)", name, source, len(data))

    def versions(self, env: str, source: Optional[str] = None) -> List[dict]:
        with self.lock:
            items = [v for v in self.index.values() if v["env"] == env and (source is None or v["source"] == source)]
        return sorted(items, key=lambda v: version_key(v["version"]))

    def latest(self, env: str) -> Optional[dict]:
        """newest fork release (official rollback files are never offered automatically)"""
        fork = self.versions(env, source="fork")
        return fork[-1] if fork else None

    def find(self, env: str, version: str) -> Optional[dict]:
        with self.lock:
            return self.index.get(self.file_name(env, version))

    def path_for(self, file_name: str) -> Optional[str]:
        if not _SAFE.match(file_name) or file_name not in self.index:
            return None
        path = os.path.join(self.dir, file_name)
        return path if os.path.exists(path) else None

    # ---- GitHub ----
    def _http(self) -> httpx.Client:
        if self.client is None:
            self.client = httpx.Client(timeout=60.0, follow_redirects=True)
        return self.client

    def _headers(self, token: bool) -> dict:
        h = {"Accept": "application/vnd.github+json", "User-Agent": "wlt-update"}
        if token and self.config.github_token:
            h["Authorization"] = "Bearer " + self.config.github_token
        return h

    def _releases(self, repo: str, token: bool) -> List[dict]:
        r = self._http().get(f"{self.config.github_api}/repos/{repo}/releases?per_page=30", headers=self._headers(token))
        r.raise_for_status()
        return r.json()

    def _download_asset(self, asset: dict, token: bool) -> bytes:
        # API asset endpoint works for private repos too (token), redirects to storage
        headers = self._headers(token)
        headers["Accept"] = "application/octet-stream"
        r = self._http().get(asset["url"], headers=headers)
        r.raise_for_status()
        return r.content

    def import_dir(self):
        """manual fallback: files <env>-<version>.bin in <data>/fw/import are taken over"""
        path = os.path.join(self.dir, "import")
        os.makedirs(path, exist_ok=True)
        for name in sorted(os.listdir(path)):
            m = re.match(r"^([A-Za-z0-9]+)-(v[0-9A-Za-z.\-]+)\.bin$", name)
            if not m or self.find(m.group(1), m.group(2)):
                continue
            with open(os.path.join(path, name), "rb") as f:
                data = f.read()
            if data[:1] != b"\xe9":
                log.warning("import %s: not an ESP32 image", name)
                continue
            source = "fork" if "-gas" in m.group(2) else "upstream"
            self.add_file(m.group(1), m.group(2), data, source)

    def poll(self):
        errors = []
        try:
            self.import_dir()
        except Exception as e:  # noqa: BLE001
            errors.append(f"import: {e}")
        for env, asset_name in self.config.fork_asset.items():
            try:
                self._poll_fork(env, asset_name)
            except Exception as e:  # noqa: BLE001
                errors.append(f"fork {env}: {e}")
        for env, pattern in self.config.upstream_asset.items():
            for tag in self.config.rollback_tags:
                if self.find(env, tag):
                    continue
                try:
                    self._fetch_upstream(env, pattern, tag)
                except Exception as e:  # noqa: BLE001
                    errors.append(f"upstream {env} {tag}: {e}")
        self.last_poll = time.time()
        self.last_error = "; ".join(errors)
        if errors:
            log.warning("poll errors: %s", self.last_error)

    def _poll_fork(self, env: str, asset_name: str):
        for rel in self._releases(self.config.github_repo, token=True):
            tag = rel.get("tag_name", "")
            if rel.get("draft") or not tag.startswith("v") or self.find(env, tag):
                continue
            assets = {a["name"]: a for a in rel.get("assets", [])}
            if asset_name not in assets:
                continue
            data = self._download_asset(assets[asset_name], token=True)
            sha_asset = assets.get(asset_name + ".sha256")
            if sha_asset:
                expected = self._download_asset(sha_asset, token=True).decode().split()[0]
                if hashlib.sha256(data).hexdigest() != expected:
                    raise ValueError(f"sha256 mismatch for {tag}")
            if data[:1] != b"\xe9":
                raise ValueError(f"{tag}: not an ESP32 image")
            self.add_file(env, tag, data, "fork", rel.get("published_at", ""))

    def _fetch_upstream(self, env: str, pattern: str, tag: str):
        r = self._http().get(f"{self.config.github_api}/repos/{self.config.upstream_repo}/releases/tags/{tag}",
                             headers=self._headers(False))
        r.raise_for_status()
        rel = r.json()
        name = pattern.format(tag=tag)
        assets = {a["name"]: a for a in rel.get("assets", [])}
        if name not in assets:
            raise ValueError(f"asset {name} not found")
        data = self._download_asset(assets[name], token=False)
        if data[:1] != b"\xe9":
            raise ValueError(f"{tag}: not an ESP32 image")
        self.add_file(env, tag, data, "upstream", rel.get("published_at", ""))

    def start_polling(self):
        def loop():
            while True:
                try:
                    self.poll()
                except Exception as e:  # noqa: BLE001
                    self.last_error = str(e)
                    log.exception("poll failed")
                time.sleep(max(60, self.config.poll_interval_s))

        threading.Thread(target=loop, name="fw-poll", daemon=True).start()
