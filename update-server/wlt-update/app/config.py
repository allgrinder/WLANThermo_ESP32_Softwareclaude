# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
"""Configuration from environment variables (see update-server/.env.example)."""
import ipaddress
import os
from dataclasses import dataclass, field
from typing import Dict, List


def _list(value: str) -> List[str]:
    return [v.strip() for v in value.split(",") if v.strip()]


def _secret(name: str) -> str:
    """Value from NAME_FILE (docker secret) or NAME."""
    path = os.environ.get(name + "_FILE")
    if path and os.path.exists(path):
        with open(path) as f:
            return f.read().strip()
    return os.environ.get(name, "").strip()


@dataclass
class Config:
    data_dir: str = "/data"
    # own address, used in firmware URLs (plain http, the device has no TLS)
    public_base_url: str = "http://192.168.178.250"
    # fork releases
    github_repo: str = "allgrinder/WLANThermo_ESP32_Softwareclaude"
    github_token: str = ""
    github_api: str = "https://api.github.com"
    poll_interval_s: int = 1800
    # official firmware for rollback
    upstream_repo: str = "WLANThermo-nano/WLANThermo_ESP32_Software"
    rollback_tags: List[str] = field(default_factory=lambda: ["v1.2.4"])
    # device.device:device.hw_version -> environment name
    device_map: Dict[str, str] = field(default_factory=lambda: {"nano:v3": "nanoV3"})
    # asset names: fork release asset per environment, upstream asset pattern
    fork_asset: Dict[str, str] = field(default_factory=lambda: {"nanoV3": "nanoV3.bin"})
    upstream_asset: Dict[str, str] = field(default_factory=lambda: {"nanoV3": "nano_v3_esp32_firmware_{tag}.bin"})
    # real API server, resolved via fixed public resolvers (never the home DNS)
    upstream_api_host: str = "api.wlanthermo.de"
    upstream_dns: List[str] = field(default_factory=lambda: ["1.1.1.1", "9.9.9.9"])
    upstream_timeout_s: float = 10.0
    # only clients from these networks are served
    allowed_nets: List[ipaddress._BaseNetwork] = field(default_factory=list)
    session_gap_s: int = 1800
    fetch_on_start: bool = True

    @staticmethod
    def from_env() -> "Config":
        c = Config()
        c.data_dir = os.environ.get("DATA_DIR", c.data_dir)
        c.public_base_url = os.environ.get("PUBLIC_BASE_URL", c.public_base_url).rstrip("/")
        c.github_repo = os.environ.get("GITHUB_REPO", c.github_repo)
        c.github_token = _secret("GITHUB_TOKEN")
        c.github_api = os.environ.get("GITHUB_API", c.github_api).rstrip("/")
        c.poll_interval_s = int(os.environ.get("POLL_INTERVAL_S", c.poll_interval_s))
        c.upstream_repo = os.environ.get("UPSTREAM_REPO", c.upstream_repo)
        if "ROLLBACK_TAGS" in os.environ:
            c.rollback_tags = _list(os.environ["ROLLBACK_TAGS"])
        if "DEVICE_MAP" in os.environ:
            # nano:v3=nanoV3,link:v1=linkV1
            c.device_map = dict(item.split("=", 1) for item in _list(os.environ["DEVICE_MAP"]))
        c.upstream_api_host = os.environ.get("UPSTREAM_API_HOST", c.upstream_api_host)
        if "UPSTREAM_DNS" in os.environ:
            c.upstream_dns = _list(os.environ["UPSTREAM_DNS"])
        c.upstream_timeout_s = float(os.environ.get("UPSTREAM_TIMEOUT_S", c.upstream_timeout_s))
        nets = os.environ.get("ALLOWED_NETS", "192.168.178.0/24,127.0.0.0/8")
        c.allowed_nets = [ipaddress.ip_network(n, strict=False) for n in _list(nets)]
        c.session_gap_s = int(os.environ.get("SESSION_GAP_S", c.session_gap_s))
        c.fetch_on_start = os.environ.get("FETCH_ON_START", "1") not in ("0", "false", "no")
        return c
