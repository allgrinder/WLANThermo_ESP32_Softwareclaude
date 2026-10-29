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

In der Antwort muss bei `latest` die neueste Gas-Version stehen (z. B. `v1.2.4-gas.2`), und
`files` muss `nanoV3-v1.2.4.bin` für den Rollback enthalten. `last_error` sollte leer sein.

## 2. FritzBox umstellen (nur für das Erst-Update)

1. *Internet → Zugangsdaten → DNS-Server*:
   - „Andere DNSv4-Server verwenden“ wählen
   - Bevorzugt: `192.168.178.251`
   - Alternativ: `1.1.1.1`
   - *Übernehmen*
2. *Internet → Zugangsdaten → Reiter IPv6*: Haken bei „IPv6-Unterstützung aktiv“ **raus** →
   *Übernehmen*. Das Internet ist kurz weg.

   Warum: Mit aktivem IPv6 fragt die FritzBox auch die IPv6-DNS-Server des Anbieters und liefert
   dann die echte Adresse, `wlt-dns` wird übergangen. Das Abschalten leert außerdem ihren
   DNS-Speicher. Nur für die paar Minuten des Updates.
3. *Heimnetz → Netzwerk → Netzwerkeinstellungen*, darunter „weitere Einstellungen“, Bereich
   **DNS-Rebind-Schutz**: als Ausnahme `api.wlanthermo.de` eintragen → *Übernehmen*.
   Ohne die Ausnahme kann die FritzBox die Antwort mit der lokalen Adresse verwerfen.

## 3. Umleitung prüfen

Von einem Windows-PC im Heimnetz (Eingabeaufforderung). `nslookup` fragt den angegebenen Server
direkt, ein Leeren des DNS-Caches ist nicht nötig.

1. `wlt-dns` selbst:

```bash
nslookup api.wlanthermo.de 192.168.178.251
```

2. Die FritzBox über IPv4, also genau so, wie der WLANThermo fragt:

```bash
nslookup api.wlanthermo.de 192.168.178.1
```

Beide Male muss `192.168.178.250` herauskommen. Die zusätzliche Zeile `::` ist gewollt (leere
IPv6-Antwort), `Server: UnKnown` ebenfalls harmlos. Kommt beim zweiten Test `85.214.113.210`
(der echte Server), ist IPv6 noch an oder die FritzBox hat die alte Antwort noch im Speicher:
Schritt 2 prüfen, 1 min warten, erneut testen.

Ein `nslookup` ohne Serverangabe ist kein guter Test: Windows fragt die FritzBox dann oft über
IPv6, das Ergebnis sagt über den WLANThermo (nur IPv4) wenig.

## 4. Update aufs Gerät

1. Pitmaster **aus**, Gas zu.
2. WLANThermo neu starten (Stecker ziehen). Das leert seinen DNS-Cache.
3. Die WebUI zeigt jetzt „Update verfügbar“ → installieren. Alternativ im Browser
   `http://<wlanthermo-ip>/update` öffnen und die neueste Gas-Version (z. B. `v1.2.4-gas.2`) eingeben.
4. Etwa 1–2 Minuten warten, das Gerät startet neu. Nicht vom Strom trennen. Unter *System*
   steht dann die neue Version.
5. Im Log von `wlt-update` (Unraid → Docker → `wlt-update` → *Logs*) siehst du die Anfrage
   (`offer v1.2.4-gas.…`) und den Download (`/fw/nanoV3-…bin`).

## 5. Nach dem Erst-Update

1. **Web-Passwort setzen:** `http://<wlanthermo-ip>/setadmin` (Benutzer `admin`). Ohne Passwort
   ist kein Firmware-Upload möglich.
2. **FritzBox zurückstellen:** *Internet → Zugangsdaten → Reiter IPv6* → „IPv6-Unterstützung
   aktiv“ wieder anhaken. Dann *DNS-Server* → „DNS-Server des Internetanbieters verwenden“.
   Die Rebind-Ausnahme kann bleiben oder raus.
3. `wlt-dns` stoppen:

   ```bash
   docker stop wlt-dns
   ```

   `wlt-update` brauchst du nur noch als Telemetrie-Logger (Abschnitt 8).

## 6. Servo einrichten und Autotune

Servo und Gestänge entscheiden darüber, wie ruhig die Temperatur später läuft. Die Regelung kann nur
so fein sein wie die Mechanik.

**Vorab wissen**

- Der Servo wird über den **Handbetrieb** des Pitmasters bewegt (Pitmaster → Typ *manuell*,
  Wert in %). Der Wert läuft von `SPmin` (0 %) bis `SPmax` (100 %). Im Profil sind das
  zunächst 0 und 100, also der volle Servoweg von 550 bis 2250 µs.
- `SPmin`/`SPmax` kannst du in % des Servowegs (0–100) oder direkt in µs (550–2250)
  eintragen.
- Ist `SPmin` größer als `SPmax`, dreht der Servo andersherum. So passt die Richtung zum Ventil,
  egal wie er eingebaut ist.
- **Beim Einschalten fährt der Servo sofort auf den Mindestausgang**, also auf `SPmin`. Mit den
  Werkswerten ist das ein Ende des Servowegs. Darum: **Gestänge erst ankuppeln, wenn `SPmin`
  und `SPmax` gespeichert sind.**

**Schritt 1 – Ventil verstehen (ohne Gas, Flasche zu)**

- Von Hand drehen: In welcher Reihenfolge kommen *AUS*, *GROSS* und *KLEIN*? Bei vielen
  Gasventilen geht es AUS → GROSS → KLEIN. Wo liegen die Anschläge, muss man zum Drehen drücken?
- Der Servo arbeitet **nur zwischen GROSS und KLEIN**. Er darf *AUS* nie erreichen. Sonst wäre die
  Flamme aus, und beim Zurückdrehen strömt Gas ohne Flamme (keine Zündsicherung!).

**Schritt 2 – Servo-Positionen finden (Gestänge abgekuppelt)**

1. Profil „Gas Servo“, Pitmaster auf *manuell*. Mit dem Wert in kleinen Schritten den Servo
   bewegen und schauen, welche Hebelstellung zu welchem Wert gehört.
2. Hebel/Kupplung so montieren, dass der Bereich GROSS…KLEIN in der Mitte des Servowegs liegt,
   mit Luft zu beiden Enden. Kupplung **spielfrei** (kein Rutschen, kein Wackeln), Servo fest
   verschraubt. Spiel im Gestänge macht die Kurve später unruhig.
3. Die Werte für *KLEIN* und *GROSS* knapp vor den Anschlägen notieren und als `SPmin` (KLEIN)
   und `SPmax` (GROSS) eintragen → *Speichern*. Danach erst ankuppeln. Prüfen: Wert 0 % =
   KLEIN, 100 % = GROSS, an keinem Ende drückt der Servo gegen den Anschlag (kein Brummen).

**Schritt 3 – Mit Flamme feinjustieren (Deckel offen, dabeibleiben)**

1. Gas auf, von Hand zünden, Pitmaster *manuell* 100 % → große Flamme ok?
2. Langsam auf 0 % zurück. Brennt die Flamme bei 0 % stabil, auch bei einem Windstoß? Wenn nicht,
   `SPmin` etwas Richtung GROSS verschieben, bis sie sicher brennt. Das ist die wichtigste
   Einstellung: Die Regelung geht nie unter diesen Punkt.
3. `Mindestausgang` im Profil auf 0 lassen. `SPmin` ist ja schon die kleinste sichere Flamme.

**Schritt 4 – SPmax begrenzen (Deckel zu, ca. 30 min)**

Mit vollem Ventilweg wird der Smoker weit über 250 °C heiß. Zum Smoken (110–150 °C) ist das zu
viel Weg: Jeder Prozentschritt bewirkt dann zu viel, und die Temperatur schwingt. Ziel ist:
**Bei 100 % erreicht der Smoker etwa 200–220 °C.** Das ist genug Reserve für Anheizen, Wind und
Deckel, und jeder Schritt wird feiner.

1. Pitmaster *manuell* **50 %**, Deckel zu, 25–30 min warten, bis die Temperatur steht.
   - etwa 200–220 °C: Testwert 0,5 passt
   - deutlich über 220 °C: mit 35 % wiederholen, unter 190 °C: mit 65 %
2. Neuer Wert: **SPmax = SPmin + (SPmax − SPmin) × Testwert**
   (Beispiel: SPmin 5, SPmax 60, gut bei 50 % → 5 + 55 × 0,5 = 32,5). Eintragen, *Speichern*.
3. Wird `SPmax` später noch einmal geändert: Autotune wiederholen und auf der Pitmaster-Seite
   „Gelerntes zurücksetzen“, denn Regelwerte und gelernte Ventilstellungen gelten nur für
   diesen Weg.

Ein begrenzter `SPmax` hält den Servo außerdem sicher weg von GROSS und damit von AUS.

**Schritt 5 – Autotune**

1. „Flammenausfall-Erkennung“ ist im Gas-Profil an (15 °C in 300 s). „Deckelüberwachung“ an lassen.
2. Pitmaster auf **auto**, Kanal des Garraumfühlers, Soll **130 °C** → *Speichern*.
3. *Autotune starten*. Die Karte zeigt die Phase (Anfahren, Relay), den Zyklus x/8 und die
   Laufzeit. Erfahrungsgemäß dauert es 20–60 min. Deckel geschlossen halten, **dabeibleiben**. Ohne Grillgut, aber
   mit Wasserschale, falls du sie sonst auch benutzt.
4. Danach stehen Kp/Ki/Kd im Profil und der Pitmaster regelt weiter. Bei jedem Abbruch
   (Übertemperatur, Fühler weg, Stopp) fährt der Servo auf den Mindestausgang und der Pitmaster
   geht aus.
5. **Den Autotune machst du einmal**, nicht bei jedem Grillabend. Wiederholen nur nach einer
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
