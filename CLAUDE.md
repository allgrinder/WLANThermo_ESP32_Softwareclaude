# WLANThermo Nano V3 – Gas-Fork (El-Fuego-Gas-Smoker)

Fork von `WLANThermo-nano/WLANThermo_ESP32_Software` (Basis FW v1.2.4). Der Servo sitzt am
Gasregler eines El-Fuego-Gas-Smokers. **Der Brenner hat keine Zündsicherung.**
Sprache mit dem Nutzer: Deutsch. Code-Kommentare: Englisch, wie im Upstream.

## Sicherheitsregeln (vom Nutzer, gelten immer)

- **Gerätezugriff grundsätzlich nur lesend** (`/data`, `/settings`, Telemetrie). Schreibende
  Aufrufe (Soll, PID, Pitmaster, Servo, Update, Restart, `/settelemetry` …) nur nach
  ausdrücklichem Okay des Nutzers im Einzelfall und nur, wenn er am Smoker ist.
- STOP-Punkte, an denen gefragt wird:
  1. bevor ein Release-Tag gesetzt wird (mit Zusammenfassung der Simulation),
  2. bevor irgendetwas Richtung Gerät ausgelöst wird (flashen, starten),
  3. bevor Maßnahmen aus `docs/SECURITY-REVIEW.md` umgesetzt werden.
- Den Wert `APISERVER` in der Firmware **nicht** ändern.
- **Niemals ein `url`-Objekt** in einer Server-Antwort an das Gerät senden (schreibt die Hosts im
  Gerät dauerhaft um). `wlt-update` entfernt `url`/`update` aus weitergeleiteten Antworten.
- Keine neuen Libraries in `platformio.ini`, außer es geht nicht anders (dann begründen).
  Rückwärtskompatibel für alle anderen Aktoren und Profile.
- Kleine, thematische Commits. Nur in diesen Fork pushen, **kein PR an Upstream**.
- GPL-Lizenz-Header in neuen Dateien, Upstream-Autoren nicht entfernen.
- GitHub-Token (falls nötig) nur read-only und als Secret. Kein Port nach außen; Telemetrie- und
  Admin-Endpunkte nur aus dem Heimnetz.

## Netz des Nutzers

| | IP |
|---|---|
| FritzBox | 192.168.178.1 |
| Unraid | 192.168.178.77 |
| `wlt-update` (Container, br0) | 192.168.178.250 |
| `wlt-dns` (Container, br0, nur Erst-Update) | 192.168.178.251 |
| WLANThermo | noch nicht genannt (in `docs/LIVE-ANALYSE.md` eintragen, sobald bekannt) |

## Stand

- Branch **`feature/gas-smart-control`** ist der aktuelle Stand (baut auf
  `feature/gas-servo-autotune` auf). `master` ist unverändert Upstream.
- Release **`v1.2.4-gas.2`** (Vorabversion, Commit `69ede86`) ist auf dem Gerät installiert
  (Erst-Update über `wlt-update`/`wlt-dns`). Die DNS-Umleitung ist zurückgenommen.
  Ob `wlt-dns` gestoppt ist und ob das Web-Passwort gesetzt ist, beim Nutzer nachfragen.
- Inhalt: Relay-Autotune (Åström-Hägglund, Tyreus-Luyben), Mindestausgang, sicheres Parken,
  Boot-Parken, Task-Watchdog, Flammenausfall-Erkennung, Server-Antwort-Policy, Telemetrie,
  `/fwupload`, intelligente Regelung (lernende Vorsteuerung `BiasMap`, Anti-Windup,
  D auf Messung).
- Servo-Kalibrierung und erster Autotune am echten Smoker stehen noch aus
  (`docs/INSTALL-GAS.md` Abschnitt 6, nur mit dem Nutzer am Smoker).

## Bauen und testen

- Firmware: PlatformIO, nur env `nanoV3` (`pio run -e nanoV3`). CI: `.github/workflows/gas-ci.yml`.
- Host-Simulation (kompiliert die echte `src/pitmaster/*` gegen Arduino-Stubs):
  `make -C test/sim test` (alter Modus, Smart-Modus und Unit-Tests). Muss vor jedem Release grün sein.
- Update-Server: `update-server/wlt-update` (pytest in `tests/`),
  Integrationstest `update-server/test/run_integration.sh` (Docker).
- WebUI: `webui/` (Vue). Nach Änderungen `webui/dist/nano/index.html.gz` neu bauen und mit committen.
- Release: Version in `RELEASE_VERSION` auf dem Feature-Branch ändern und pushen
  (`.github/workflows/release.yml` baut, testet und legt Tag + Release an). Schema
  `v1.2.4-gas.N`. Nur nach Okay des Nutzers (STOP-Punkt 1).
- Container-Images: `ghcr.io/allgrinder/wlt-update` und `wlt-dns` (öffentlich), gebaut von
  `.github/workflows/containers.yml`. Unraid-Vorlagen in `update-server/unraid/`.

## Dokumentation

- `docs/INSTALL-GAS.md` – Installation, Erst-Update, Servo/Autotune, spätere Updates, Telemetrie
- `docs/autotune-simulation.md` – Modell, Autotune, Simulationsergebnisse
- `docs/smart-control.md` – intelligente Regelung, Vorher/Nachher
- `docs/SECURITY-REVIEW.md` – Sicherheitsprüfung (S2, S4–S6 vom Nutzer als nicht nötig abgelehnt)
- `docs/LIVE-ANALYSE.md` – lesende Live-Auswertung (`tools/live_watch.py`)

## Offene Punkte

1. Erster echter Grillabend mit Telemetrie → `tools/analyze_cook.py`, dann
   `tools/fit_plant.py` (Modell an den El Fuego anpassen) und die Simulation damit neu rechnen.
   Die Simulationsparameter (P1–P3) sind bisher Annahmen, keine Messung.
2. Danach neu bewerten: Ventil-Linearisierung (offen), Bremspunkt beim Anheizen (in der
   Simulation verworfen, schlechter bei Sollsprüngen).
3. Geräte-IP in `docs/LIVE-ANALYSE.md` eintragen, sobald der Nutzer sie nennt.
