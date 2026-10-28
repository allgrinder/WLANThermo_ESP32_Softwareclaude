# Copyright (C) 2026 WLANThermo gas fork contributors - GPL-3.0-or-later
"""SQLite storage: request log, cook sessions, telemetry and cloud samples."""
import csv
import io
import json
import os
import sqlite3
import threading
import time
from typing import Iterable, List, Optional

TELEMETRY_COLS = ["pm", "type", "raw", "temp", "set", "out", "servo_us", "p", "i", "d", "esum",
                  "flags", "at_state", "at_phase", "at_cycle"]

SCHEMA = """
CREATE TABLE IF NOT EXISTS requests (
  id INTEGER PRIMARY KEY, ts REAL, client TEXT, sn TEXT, device TEXT, hw TEXT, sw TEXT,
  kind TEXT, status INTEGER, summary TEXT);
CREATE TABLE IF NOT EXISTS sessions (
  id INTEGER PRIMARY KEY, sn TEXT, start_ts REAL, end_ts REAL, samples INTEGER DEFAULT 0, note TEXT);
CREATE TABLE IF NOT EXISTS telemetry (
  session_id INTEGER, sn TEXT, ts REAL, pm INTEGER, type INTEGER, raw REAL, temp REAL, "set" REAL,
  out REAL, servo_us INTEGER, p REAL, i REAL, d REAL, esum REAL, flags INTEGER, at_state INTEGER,
  at_phase INTEGER, at_cycle INTEGER);
CREATE INDEX IF NOT EXISTS telemetry_session ON telemetry(session_id, ts);
CREATE TABLE IF NOT EXISTS cloud (
  session_id INTEGER, sn TEXT, ts REAL, source TEXT, channels TEXT, pitmaster TEXT);
CREATE INDEX IF NOT EXISTS cloud_session ON cloud(session_id, ts);
CREATE TABLE IF NOT EXISTS batches (
  id INTEGER PRIMARY KEY, ts REAL, sn TEXT, fw TEXT, heap INTEGER, loop_us_max INTEGER,
  dropped INTEGER, failed INTEGER, rows INTEGER);
"""


class Store:
    def __init__(self, data_dir: str, session_gap_s: int = 1800):
        os.makedirs(data_dir, exist_ok=True)
        self.db = sqlite3.connect(os.path.join(data_dir, "wlt.sqlite"), check_same_thread=False)
        self.db.executescript(SCHEMA)
        self.lock = threading.Lock()
        self.gap = session_gap_s

    # ---- request log ----
    def log_request(self, client: str, body: dict, kind: str, status: int, summary: str):
        dev = body.get("device", {}) if isinstance(body, dict) else {}
        with self.lock:
            self.db.execute(
                "INSERT INTO requests(ts, client, sn, device, hw, sw, kind, status, summary) VALUES (?,?,?,?,?,?,?,?,?)",
                (time.time(), client, str(dev.get("serial", "")), str(dev.get("device", "")),
                 str(dev.get("hw_version", "")), str(dev.get("sw_version", "")), kind, status, summary[:500]))
            self.db.commit()

    def recent_requests(self, limit: int = 100) -> List[dict]:
        with self.lock:
            cur = self.db.execute("SELECT ts, client, sn, device, hw, sw, kind, status, summary FROM requests "
                                  "ORDER BY id DESC LIMIT ?", (limit,))
            cols = [c[0] for c in cur.description]
            return [dict(zip(cols, r)) for r in cur.fetchall()]

    # ---- sessions ----
    def _session_for(self, sn: str, ts: float) -> int:
        cur = self.db.execute("SELECT id, end_ts FROM sessions WHERE sn=? ORDER BY end_ts DESC LIMIT 1", (sn,))
        row = cur.fetchone()
        if row and ts - row[1] <= self.gap and ts >= row[1] - self.gap:
            if ts > row[1]:
                self.db.execute("UPDATE sessions SET end_ts=? WHERE id=?", (ts, row[0]))
            return row[0]
        cur = self.db.execute("INSERT INTO sessions(sn, start_ts, end_ts) VALUES (?,?,?)", (sn, ts, ts))
        return cur.lastrowid

    # ---- telemetry ----
    def add_telemetry(self, batch: dict) -> int:
        """batch format: see src/Telemetry.cpp"""
        sn = str(batch.get("sn", "unknown"))
        cols = batch.get("cols", [])
        rows = batch.get("rows", [])
        server_now = time.time()
        dev_now = float(batch.get("now", 0) or 0)
        now_ms = float(batch.get("now_ms", 0) or 0)
        # device clock once it is synced (HTTP Date header), else server time
        base = dev_now if dev_now > 1600000000 else server_now
        stored = 0
        with self.lock:
            for row in rows:
                rec = dict(zip(cols, row))
                ts = base - (now_ms - float(rec.get("ms", now_ms))) / 1000.0
                if not self._active(rec):
                    continue
                sid = self._session_for(sn, ts)
                self.db.execute(
                    'INSERT INTO telemetry(session_id, sn, ts, pm, type, raw, temp, "set", out, servo_us, p, i, d, esum, '
                    "flags, at_state, at_phase, at_cycle) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (sid, sn, ts) + tuple(rec.get(c) for c in TELEMETRY_COLS))
                self.db.execute("UPDATE sessions SET samples=samples+1 WHERE id=?", (sid,))
                stored += 1
            self.db.execute(
                "INSERT INTO batches(ts, sn, fw, heap, loop_us_max, dropped, failed, rows) VALUES (?,?,?,?,?,?,?,?)",
                (server_now, sn, str(batch.get("fw", "")), batch.get("heap"), batch.get("loop_us_max"),
                 batch.get("dropped"), batch.get("failed"), len(rows)))
            self.db.commit()
        return stored

    @staticmethod
    def _active(rec: dict) -> bool:
        """a sample belongs to a cook when the pitmaster runs or the pit is hot"""
        temp = rec.get("raw")
        return (rec.get("type") not in (0, None)) or (temp is not None and 40 <= float(temp) < 900)

    # ---- cloud / custom cloud data passing through ----
    def add_cloud(self, sn: str, source: str, channels: list, pitmaster: Optional[list], ts: Optional[float] = None):
        ts = ts or time.time()
        temps = [c.get("temp") for c in channels if isinstance(c, dict)]
        hot = any(t is not None and 40 <= float(t) < 900 for t in temps)
        running = any(isinstance(p, dict) and p.get("typ") not in (None, "off") for p in (pitmaster or []))
        if not (hot or running):
            return False
        with self.lock:
            sid = self._session_for(sn, ts)
            self.db.execute("INSERT INTO cloud(session_id, sn, ts, source, channels, pitmaster) VALUES (?,?,?,?,?,?)",
                            (sid, sn, ts, source, json.dumps(channels), json.dumps(pitmaster or [])))
            self.db.commit()
        return True

    # ---- export ----
    def sessions(self) -> List[dict]:
        with self.lock:
            cur = self.db.execute(
                "SELECT s.id, s.sn, s.start_ts, s.end_ts, s.samples, "
                "(SELECT COUNT(*) FROM cloud c WHERE c.session_id = s.id) FROM sessions s ORDER BY s.id")
            out = []
            for sid, sn, start, end, samples, cloud in cur.fetchall():
                out.append({"id": sid, "sn": sn, "start": _iso(start), "end": _iso(end),
                            "duration_min": round((end - start) / 60.0, 1), "telemetry_samples": samples,
                            "cloud_samples": cloud, "csv": f"/sessions/{sid}.csv",
                            "cloud_csv": f"/sessions/{sid}/cloud.csv"})
            return out

    def telemetry_csv(self, session_id: int) -> Optional[str]:
        with self.lock:
            if not self.db.execute("SELECT 1 FROM sessions WHERE id=?", (session_id,)).fetchone():
                return None
            cur = self.db.execute('SELECT ts, pm, type, raw, temp, "set", out, servo_us, p, i, d, esum, flags, '
                                  "at_state, at_phase, at_cycle FROM telemetry WHERE session_id=? ORDER BY ts",
                                  (session_id,))
            rows = cur.fetchall()
        buf = io.StringIO()
        w = csv.writer(buf, lineterminator="\n")
        w.writerow(["ts", "iso"] + TELEMETRY_COLS)
        for r in rows:
            w.writerow([f"{r[0]:.1f}", _iso(r[0])] + list(r[1:]))
        return buf.getvalue()

    def cloud_csv(self, session_id: int) -> Optional[str]:
        with self.lock:
            if not self.db.execute("SELECT 1 FROM sessions WHERE id=?", (session_id,)).fetchone():
                return None
            rows = self.db.execute("SELECT ts, source, channels, pitmaster FROM cloud WHERE session_id=? ORDER BY ts",
                                   (session_id,)).fetchall()
        buf = io.StringIO()
        w = csv.writer(buf, lineterminator="\n")
        nch = 0
        parsed = []
        for ts, source, ch, pm in rows:
            ch = json.loads(ch)
            pm = json.loads(pm)
            nch = max(nch, len(ch))
            parsed.append((ts, source, ch, pm))
        w.writerow(["ts", "iso", "source"] + [f"ch{i + 1}" for i in range(nch)] +
                   ["pm_channel", "pm_typ", "pm_set", "pm_value"])
        for ts, source, ch, pm in parsed:
            temps = []
            for c in ch:
                t = c.get("temp")
                temps.append("" if t is None or float(t) >= 900 else t)
            temps += [""] * (nch - len(temps))
            p = pm[0] if pm else {}
            w.writerow([f"{ts:.1f}", _iso(ts), source] + temps +
                       [p.get("channel", ""), p.get("typ", ""), p.get("set", ""), p.get("value", "")])
        return buf.getvalue()

    def batch_stats(self, limit: int = 20) -> List[dict]:
        with self.lock:
            cur = self.db.execute("SELECT ts, sn, fw, heap, loop_us_max, dropped, failed, rows FROM batches "
                                  "ORDER BY id DESC LIMIT ?", (limit,))
            cols = [c[0] for c in cur.description]
            return [dict(zip(cols, r)) for r in cur.fetchall()]


def _iso(ts: float) -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ts))


def iter_rows(text: str) -> Iterable[dict]:
    return csv.DictReader(io.StringIO(text))
