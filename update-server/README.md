# Update-Server-Emulator und DNS-Umleitung

Zwei Container für Unraid, Anleitung: [docs/INSTALL-GAS.md](../docs/INSTALL-GAS.md).
Unraid-Vorlagen: [`unraid/my-wlt-update.xml`](unraid/my-wlt-update.xml) und [`unraid/my-wlt-dns.xml`](unraid/my-wlt-dns.xml).

| Container | Aufgabe |
|---|---|
| `wlt-update` | Beantwortet Update-Anfragen des Geräts mit den Releases dieses Forks. Leitet alle anderen API-Anfragen 1:1 an das echte `api.wlanthermo.de` weiter (aufgelöst über 1.1.1.1/9.9.9.9). Liefert die Firmware per HTTP aus und speichert Telemetrie und Cloud-Daten in SQLite (`/sessions`). |
| `wlt-dns` | dnsmasq: `api.wlanthermo.de` → IP von `wlt-update` (TTL 60 s). Alles andere geht an 1.1.1.1 und 9.9.9.9. |

## Endpunkte von `wlt-update`

Nur aus dem Heimnetz erreichbar (`ALLOWED_NETS`).

| Methode | Pfad | Zweck |
|---|---|---|
| POST | `/` | Geräte-API (Update hier beantwortet, alles andere weitergeleitet) |
| GET | `/fw/<env>-<version>.bin` | Firmware-Datei |
| GET | `/firmware`, POST `/admin/poll` | Firmware-Cache anzeigen, sofort neu abfragen |
| POST | `/telemetry` | Telemetrie des Geräts (Firmware-Einstellung) |
| POST | `/custom` | Ziel für „Eigener Cloud-Server“ des Geräts |
| GET | `/sessions`, `/sessions/<id>.csv`, `/sessions/<id>/cloud.csv` | Grillabende als CSV |
| GET | `/requests`, `/health` | Request-Log, Status |

## Firmware-Quellen

- Releases des Forks (Asset `nanoV3.bin`, optional mit `.sha256` geprüft), Abfrage alle `POLL_INTERVAL_S` Sekunden.
- Offizielle Firmware `v1.2.4` aus den Upstream-Releases für den Rollback (`ROLLBACK_TAGS`).
- Manuell: Dateien `nanoV3-<version>.bin` nach `<DATA_PATH>/fw/import/` legen.

## Tests

```bash
cd wlt-update && pip install -r requirements-dev.txt && python -m pytest -q tests
cd ../test && ./run_integration.sh     # Docker, ohne Internet
```
