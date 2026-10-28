# Installation: WLANThermo Nano V3 mit Gas-Servo-Firmware

Dein Netz: FritzBox `192.168.178.1`, Unraid `192.168.178.77`.

Die Container bekommen eigene Adressen:

| Container | Adresse | Aufgabe |
|---|---|---|
| `wlt-update` | `192.168.178.250` | Update-Server und Telemetrie-Logger |
| `wlt-dns` | `192.168.178.251` | DNS-Umleitung |

Ablauf in Kurzform:

1. **Erst-Update:** Die Original-Firmware kann nur über ihren Update-Server aktualisiert werden.
   Dafür lenkst du `api.wlanthermo.de` kurz auf `wlt-update` um (DNS-Umleitung).
2. **Danach:** Updates spielst du direkt in der WebUI ein (Datei hochladen). Die DNS-Umleitung
   kommt wieder raus und `wlt-dns` kann aus. `wlt-update` brauchst du nur noch als Logger für
   die Telemetrie (optional).

> **Sicherheit:** Das Gerät steuert einen Gasbrenner ohne Zündsicherung. Spiele Updates nur bei
> ausgeschaltetem Pitmaster ein. Lass den Smoker beim ersten Autotune nicht unbeaufsichtigt.

---

## 1. Container auf Unraid starten

**Vorher prüfen:**

- Die Adressen `.250` und `.251` liegen außerhalb des DHCP-Bereichs der FritzBox. Den Bereich
  findest du unter *Heimnetz → Netzwerk → Netzwerkeinstellungen → IP-Adressen →
  IPv4-Einstellungen* (Werkseinstellung `.20`–`.200`). Liegen die Adressen darin, nimm andere
  und passe sie unten überall an.
- Unraid: *Settings → Docker* auf „Advanced View“ stellen. Das eigene Netzwerk `br0` muss
  aktiv sein. Bei laufenden Containern geht das nur, wenn Docker kurz gestoppt wird.

**Variante A: Unraid-Vorlagen** (empfohlen, ohne Plugin):

1. Im Unraid-Terminal (*>_* oben rechts) die beiden Vorlagen holen:

```bash
cd /boot/config/plugins/dockerMan/templates-user && wget -q https://raw.githubusercontent.com/allgrinder/WLANThermo_ESP32_Softwareclaude/feature/gas-smart-control/update-server/unraid/my-wlt-update.xml https://raw.githubusercontent.com/allgrinder/WLANThermo_ESP32_Softwareclaude/feature/gas-smart-control/update-server/unraid/my-wlt-dns.xml && ls my-wlt-*
```

2. *Docker → Add Container*, oben bei *Template* unter „User templates“ `wlt-update` wählen.
   Alles ist vorausgefüllt: Netzwerk `br0`, feste IP `192.168.178.250`, Datenordner
   `/mnt/user/appdata/wlt-update`. *Apply*.
3. Dasselbe mit `wlt-dns` (feste IP `192.168.178.251`). *Apply*.

Nimmst du andere Adressen, musst du sie an drei Stellen ändern: die IP von `wlt-update`, dessen
`PUBLIC_BASE_URL` und `OVERRIDE_IP` bei `wlt-dns`.

**Variante B: Compose** (Plugin „Docker Compose Manager“ oder Terminal):

```bash
mkdir -p /mnt/user/appdata/wlt && cd /mnt/user/appdata/wlt
```

```bash
git clone --depth 1 -b feature/gas-smart-control https://github.com/allgrinder/WLANThermo_ESP32_Softwareclaude.git src
```

```bash
cd src/update-server && cp .env.example .env
```

Die `.env` passt so für dein Netz. Danach starten:

```bash
docker compose up -d
```

Die Images kommen fertig gebaut aus `ghcr.io/allgrinder/…` und sind öffentlich. Ohne Internet
auf Unraid lassen sie sich auch lokal bauen:

```bash
docker compose build && docker compose up -d
```

**Prüfen** von einem PC im Heimnetz. Der Unraid-Host selbst erreicht `br0`-Container oft nicht,
das ist normal.

```bash
curl http://192.168.178.250/health
```

In der Antwort muss bei `latest` die neueste Gas-Version stehen (z. B. `v1.2.4-gas.1`), und
`files` muss `nanoV3-v1.2.4.bin` für den Rollback enthalten. `last_error` sollte leer sein.

## 2. FritzBox umstellen (nur für das Erst-Update)

1. *Internet → Zugangsdaten → DNS-Server*:
   - „Andere DNSv4-Server verwenden“ wählen
   - Bevorzugt: `192.168.178.251`
   - Alternativ: `1.1.1.1`
   - *Übernehmen*

   Fällt Unraid aus, nutzt die FritzBox den alternativen Server. Das Internet läuft dann weiter,
   nur die Umleitung fehlt. Die FritzBox fragt allerdings mitunter auch den alternativen Server.
   Dann landet das Gerät beim echten Server und meldet kein Gas-Update. In dem Fall einfach noch
   einmal prüfen lassen (Schritt 4).
2. *Heimnetz → Netzwerk → Netzwerkeinstellungen*, darunter „weitere Einstellungen“, Bereich
   **DNS-Rebind-Schutz**:
   - Als Ausnahme `api.wlanthermo.de` eintragen
   - *Übernehmen*

   Ohne die Ausnahme verwirft die FritzBox die Antwort mit der lokalen Adresse.

## 3. Umleitung prüfen

Von einem PC im Heimnetz. Der PC muss die FritzBox als DNS nutzen, ggf. vorher den DNS-Cache leeren.

```bash
nslookup api.wlanthermo.de
```

Die Antwort muss `192.168.178.250` sein. Danach eine Update-Anfrage so senden, wie das Gerät es tut:

```bash
curl -X POST http://api.wlanthermo.de/ -H 'Content-Type: application/json' -d '{"device":{"device":"nano","serial":"test","hw_version":"v3","sw_version":"v1.2.4"},"update":{"prerelease":false},"url":{}}'
```

Erwartet wird:

```json
{"update":{"available":true,"version":"v1.2.4-gas.1","firmware":{"url":"http://192.168.178.250/fw/nanoV3-v1.2.4-gas.1.bin"}}}
```

## 4. Update aufs Gerät

1. Pitmaster **aus**, Gas zu.
2. WLANThermo neu starten (Stecker ziehen). Das leert seinen DNS-Cache.
3. Die WebUI zeigt jetzt „Update verfügbar“ → installieren. Alternativ im Browser
   `http://<wlanthermo-ip>/update` öffnen und `v1.2.4-gas.1` eingeben.
4. Etwa 1–2 Minuten warten, das Gerät startet neu. Unter *System* steht dann `v1.2.4-gas.1`.
5. Im Log von `wlt-update` (`docker logs wlt-update`) siehst du die Anfrage und den Download.

## 5. Nach dem Erst-Update

1. **Web-Passwort setzen:** `http://<wlanthermo-ip>/setadmin` (Benutzer `admin`). Ohne Passwort
   ist kein Firmware-Upload möglich.
2. **FritzBox zurückstellen:** *Internet → Zugangsdaten → DNS-Server* → „DNS-Server des
   Internetanbieters verwenden“. Die Rebind-Ausnahme kann bleiben oder raus.
3. `wlt-dns` stoppen:

   ```bash
   docker stop wlt-dns
   ```

   `wlt-update` brauchst du nur noch als Telemetrie-Logger (Abschnitt 8).

## 6. Servo kalibrieren und Autotune

1. *Pitmaster* → Profil **„Gas Servo“** wählen → Stift-Symbol (Profil bearbeiten).
2. Servo montieren. Mit dem Duty-Cycle-Test des Profils `SPmin` (= kleinste Stufe) und `SPmax`
   (= große Flamme) einstellen. Der Servo darf an beiden Enden nicht gegen den Anschlag drücken.
3. **Mindestausgang** setzen. Bei 110 °C steht der Regler sehr nahe an der kleinsten Stufe, deshalb
   zuerst 0–3 % wählen und nur bei Wind erhöhen. Liegt er zu hoch, ist der Sollwert nicht
   erreichbar.
4. „Flammenausfall-Erkennung“ ist im Gas-Profil an (15 °C in 300 s). „Deckelüberwachung“ an lassen.
5. Pitmaster auf **auto**, Kanal des Garraumfühlers, Soll **130 °C** → *Speichern*.
6. *Autotune starten*. Die Karte zeigt die Phase (Anfahren, Relay), den Zyklus x/8 und die
   Laufzeit. Erfahrungsgemäß dauert es 20–60 min. Deckel geschlossen halten, **dabeibleiben**.
7. Danach stehen Kp/Ki/Kd im Profil und der Pitmaster regelt weiter. Bei jedem Abbruch
   (Übertemperatur, Fühler weg, Stopp) fährt der Servo auf den Mindestausgang und der Pitmaster
   geht aus.
8. **Den Autotune machst du einmal**, nicht bei jedem Grillabend. Wiederholen nur nach einer
   Änderung an Servo, Gestänge oder Gasregler. Mit „Intelligente Regelung“ (im Gas-Profil an)
   lernt das Gerät danach bei jedem Grillabend selbst, welche Öffnung es für welche Temperatur
   braucht. Die Anzahl gelernter Punkte steht auf der Pitmaster-Seite. Details und Messwerte:
   [smart-control.md](smart-control.md).

**Flamme aus?** Die WebUI zeigt ein rotes Banner, der Buzzer piept und es kommt eine Push-Nachricht.
Der Push erscheint als „Unterschreitung“ des Pitmaster-Kanals mit dem Sollwert als Grenze. Der
Servo bleibt auf dem Mindestausgang, **das Gas strömt weiter**. Also Gas zu, prüfen, neu zünden,
dann *Quittieren*.

## 7. Spätere Updates (ohne Docker)

1. Neues Release öffnen:
   https://github.com/allgrinder/WLANThermo_ESP32_Softwareclaude/releases
2. `nanoV3.bin` herunterladen. Die Prüfsumme steht in `nanoV3.bin.sha256`.
3. Pitmaster **aus**.
4. WebUI → *System* → „Firmware-Datei hochladen“ (öffnet `/fwupload`) → mit `admin` und deinem
   Passwort anmelden → Datei wählen → *Hochladen*. Das Gerät startet neu.

Der Upload wird abgelehnt, solange kein Web-Passwort gesetzt ist oder der Pitmaster läuft.

## 8. Telemetrie und eigene Cloud auf den Logger

Voraussetzung: `wlt-update` läuft. Ist er aus, gehen die Daten verloren, die Regelung läuft unverändert weiter.

**Debug-Telemetrie** (jede Sekunde ein Datensatz, alle 10 s als Paket):

```bash
curl -u admin:<passwort> -X POST http://<wlanthermo-ip>/settelemetry -H 'Content-Type: application/json' -d '{"enabled":true,"url":"http://192.168.178.250/telemetry","interval":1}'
```

Abschalten mit `"enabled":false`. Den Status siehst du in `/settings` unter `telemetry`
(`sent`, `dropped`). In jedem Paket stehen außerdem `loop_us_max` (längste Laufzeit des
Regel-Tasks) und `heap`.

**Eigener Cloud-Server** (Kanaltemperaturen alle 30 s): WebUI → *IoT* → „Benutzerdefinierten Cloud
Service aktivieren“, URL `http://192.168.178.250/custom`, Intervall 30 s.

**Daten abholen:**

- Liste der Grillabende: `http://192.168.178.250/sessions`. Nach 30 min ohne Pitmaster und ohne
  heißen Fühler beginnt eine neue Session.
- Telemetrie als CSV: `http://192.168.178.250/sessions/<id>.csv`
- Cloud-Daten als CSV: `http://192.168.178.250/sessions/<id>/cloud.csv`

Auswertung auf einem Rechner im Heimnetz:

```bash
pip install -r tools/requirements.txt
```

```bash
python3 tools/analyze_cook.py --logger http://192.168.178.250 --session <id> --out docs/cooks/<datum>
```

```bash
python3 tools/fit_plant.py --logger http://192.168.178.250 --session <id> --name elfuego --out test/sim/plants --simulate
```

## 9. Rollback auf die offizielle Firmware

- **Ohne Docker:** Unter
  https://github.com/WLANThermo-nano/WLANThermo_ESP32_Software/releases/tag/v1.2.4
  `nano_v3_esp32_firmware_v1.2.4.bin` laden und über „Firmware-Datei hochladen“ einspielen.
  Achtung: Die offizielle Firmware hat diesen Upload nicht mehr. Ein späterer Weg zurück zur
  Gas-Firmware geht dann wieder nur über die DNS-Umleitung (Abschnitte 2–4) oder USB.
- **Mit Docker:** DNS-Umleitung aktiv (Abschnitt 2), dann `http://<wlanthermo-ip>/update` →
  `v1.2.4`. `wlt-update` hält die offizielle Datei bereit.
- Anschließend die FritzBox wieder auf „DNS-Server des Internetanbieters verwenden“ stellen.

## 10. Notfall: Flashen per USB

Gerät per USB an einen PC mit PlatformIO hängen:

```bash
git clone -b feature/gas-servo-autotune https://github.com/allgrinder/WLANThermo_ESP32_Softwareclaude.git
```

```bash
cd WLANThermo_ESP32_Softwareclaude && pio run -e nanoV3 -t upload
```

Alternativ eine fertige `.bin` mit esptool an die App-Adresse schreiben. Zuerst `otadata` löschen,
sonst bootet ein Gerät nach OTA-Updates weiter aus der zweiten App-Partition:

```bash
esptool.py --chip esp32 erase_region 0xe000 0x2000
```

```bash
esptool.py --chip esp32 --baud 460800 write_flash 0x10000 nanoV3.bin
```

Einstellungen (WLAN, Profile) liegen im NVS und bleiben erhalten.
