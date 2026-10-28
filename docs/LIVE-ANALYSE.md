# Live-Analyse eines Grillabends (Prompt für Claude Code)

> Diesen ganzen Block während oder nach einem Grillabend in Claude Code einfügen. Claude Code muss
> **im Heimnetz** laufen (lokal auf einem Rechner zu Hause) oder per **Tailscale** ins Heimnetz
> kommen. Eine Cloud-Sitzung erreicht Gerät und Logger nicht.
>
> Vor dem ersten Einsatz die zwei Platzhalter unten ersetzen: `WLANTHERMO_IP` und das Datum.

---

## Auftrag

Analysiere den Grillabend vom **<DATUM>** meines Gas-Smokers und schlage höchstens drei
Verbesserungen vor. Arbeite nur lesend. Ich entscheide über jede Änderung und bin dabei am Smoker.

## Kontext

- **Repo:** https://github.com/allgrinder/WLANThermo_ESP32_Softwareclaude, Branch
  `feature/gas-servo-autotune` (lokal klonen, falls nicht vorhanden).
- **Gerät:** WLANThermo Nano V3, Firmware `v1.2.4-gas.N`, WebUI/API `http://WLANTHERMO_IP`.
- **Logger:** Container `wlt-update` auf Unraid, `http://192.168.178.250`. Sessions:
  `/sessions`, Telemetrie `/sessions/<id>.csv` (1 s), Cloud-Daten `/sessions/<id>/cloud.csv` (30 s).
- **Setup:**
  - El Fuego Gas-Smoker, ein Brenner, Servo am Drehregler (Pitmaster-Aktor SERVO, Profil
    „Gas Servo“).
  - **Keine Zündsicherung**: Geht die Flamme aus, strömt weiter Gas.
  - Servo-Weg: `spmin` = kleinste Stufe, `spmax` = große Flamme; „AUS“ erreicht der Servo nicht.
  - Typischer Betrieb 100–160 °C.
- **Dokumente im Repo, zuerst lesen:**
  - `docs/autotune-simulation.md`: Streckenmodell, Simulator, Abnahme, Hinweise zum Gas-Regler
  - `docs/INSTALL-GAS.md`: Aufbau, Telemetrie, Updates
  - `docs/SECURITY-REVIEW.md`: Risiken, offene Maßnahmen
  - Code: `src/pitmaster/` (Pitmaster, PidCore, AutoTuneRelay, FlameOutDetector), `test/sim/`
    (Simulator), `tools/` (Analyse)

## Regeln (strikt)

1. **Nur lesen.** Erlaubt sind `GET /data` und `GET /settings` am Gerät sowie GET auf den Logger
   (`/sessions`, CSV, `/health`, `/requests`).
2. **Verboten:**
   - jeder schreibende Aufruf ans Gerät (`/set…`, `/setautotune`, `/setDC`, `/update`, `/restart`,
     `/stop`, `/fwupload`, `/settelemetry`, …)
   - MQTT-`/set/…`-Topics
   - Port-Freigaben oder sonstiges Freigeben ins Internet
3. Änderungen nur als **Vorschlag**. Parameter nenne ich dir, Code kommt auf einen **neuen Branch**
   mit Tests. Release und Update erst nach meinem ausdrücklichen Okay.
4. Wenn etwas gefährlich aussieht (Flamme aus, Ausgang dauerhaft 100 %, Fühler weg): **sofort
   melden** und mir sagen, was ich am Smoker prüfen soll. Selbst nichts am Gerät tun.

## Ablauf

### 1. Erreichbarkeit prüfen

```bash
curl -s -m 5 http://WLANTHERMO_IP/data | head -c 300
```

```bash
curl -s -m 5 http://192.168.178.250/health
```

Wenn etwas nicht erreichbar ist, nenne die wahrscheinliche Ursache, statt weiterzumachen:
- Claude Code läuft nicht im Heimnetz bzw. Tailscale ist aus
- Gerät aus oder IP geändert (FritzBox → Heimnetz → Netzwerk)
- Container gestoppt (`docker ps` auf Unraid)
- Zugriff kommt nicht aus `ALLOWED_NETS` (Antwort 403)

### 2. Session holen und auswerten

```bash
pip install -r tools/requirements.txt
```

```bash
curl -s http://192.168.178.250/sessions
```

Die passende Session des Abends wählen (Datum, Dauer, `telemetry_samples` > 0).

```bash
python3 tools/analyze_cook.py --logger http://192.168.178.250 --session <id> --out docs/cooks/<DATUM> --title "Grillabend <DATUM>"
```

Danach `docs/cooks/<DATUM>/report.md` und `cook.png` ansehen.

### 3. Läuft der Cook noch? Dann beobachten

```bash
python3 tools/live_watch.py --device http://WLANTHERMO_IP
```

Das liest alle 60 s `/data` und meldet nur Auffälligkeiten:
- Abweichung > 5 °C länger als 5 min
- Schwingen
- Servo dauerhaft am Anschlag
- Flamme-aus-Verdacht
- Fühler weg

Gib mir nur diese Meldungen weiter, keine Routineausgaben. Beenden mit Strg+C, wenn ich es sage
oder der Cook vorbei ist.

### 4. Streckenmodell anpassen und vergleichen

```bash
python3 tools/fit_plant.py --logger http://192.168.178.250 --session <id> --name elfuego-<DATUM> --out test/sim/plants --simulate
```

Vergleiche die angepassten Parameter (Totzeit, Zeitkonstante, tMin, tMax, Ventil-Exponent,
Fühlerverzögerung) mit P1–P3 aus `docs/autotune-simulation.md` und mit früheren Fits in
`test/sim/plants/`.

Hinweis zur Aussagekraft:
- Totzeit und Fühlerverzögerung lassen sich gegeneinander tauschen, belastbar ist ihre Summe.
- Die Ventilkennlinie ist nur gut bestimmt, wenn die Session Anheizen oder Autotune enthält.

### 5. Höchstens drei Vorschläge

Für jeden Vorschlag (Parameter oder Code):
1. Problem mit Zahlen aus Bericht und Fit belegen.
2. Im Simulator mit dem angepassten Modell prüfen (`test/sim`, Szenario mit `--plant`). Vorher und
   nachher als Tabelle zeigen: Überschwingen, Einschwingzeit, Abweichung, Servo-Weg, Erkennungszeit
   Flamme aus. Alle bestehenden Prüfungen (`make -C test/sim test`) müssen grün bleiben.
3. Bei Parametern: konkrete Werte (Kp/Ki/Kd, Mindestausgang, d/h/Regel, Flammenausfall-Fenster)
   nennen. Eintragen tue ich sie selbst in der WebUI.
4. Bei Code: neuer Branch `analysis/<DATUM>-<thema>`, kleine Commits, Tests ergänzen, CI grün.
   **Kein** Tag, kein Release, kein Update ohne mein Okay.

Kandidaten aus der Simulation, nur aufgreifen, wenn die Daten sie stützen:
- Überschwingen nach langem Deckel-Öffnen: Anti-Windup („bedingte Integration bei gesättigtem
  Ausgang“, nur Gas-Profil)
- Mindestausgang zu hoch bzw. zu niedrig
- Flammenausfall-Schwellen

### 6. Bericht

`docs/cooks/<DATUM>.md` anlegen mit:
- Eckdaten: Dauer, Sollwerte, Wetter, falls ich es nenne
- Kennzahlen aus `report.md`
- Plots (`docs/cooks/<DATUM>/cook.png`, Fit-Plot)
- Bewertung der Flamme-aus-Alarme (echt oder Fehlalarm)
- Fit-Ergebnis
- die Vorschläge mit Vorher/Nachher

Auf dem Analyse-Branch committen und pushen. Mir am Ende eine kurze Zusammenfassung mit Link geben.

---

## Selbsttest des Ablaufs (bereits durchgeführt)

Der Ablauf wurde mit einer **Simulator-Session** getestet:
- `test/sim/gas_sim --cook` mit verdeckter Strecke L 45 s, τ 500 s, tMin 85 °C, tMax 240 °C,
  Exponent 0,6
- über `tools/sim_to_logger.py` in den Logger eingespielt, genau wie die Firmware-Telemetrie

Ergebnisse:
- `analyze_cook.py`: Sprung 130 → 150 °C mit 0,3 °C Überschwingen, eingeschwungen nach 6,8 min;
  2 Deckel-Ereignisse; Flamme-aus-Alarm als „echt“ bewertet; P/I/D-Anteile 15/79/6 %.
- `fit_plant.py`: τ 515 s, tMin 83 °C, tMax 239 °C, Exponent 0,58 (wahr: 500/85/240/0,6);
  Totzeit plus Fühler 91 s (wahr 70 s); RMSE 0,58 °C. Der Simulator mit dem Fit besteht alle
  Prüfungen.
- `live_watch.py --replay`: meldet nur Autotune-Ende, Flamme aus und die Abweichung danach; keine
  Fehlmeldung bei Deckel oder Sollsprung. Gegen ein Mock-Gerät gab es keinen einzigen
  schreibenden Aufruf.

Wiederholen:

```bash
make -C test/sim && test/sim/gas_sim --cook /tmp/cook.csv 45 500 85 240 25 0.6 1
```

```bash
python3 tools/sim_to_logger.py /tmp/cook.csv http://192.168.178.250 --sn simulator0001
```

Danach die Schritte 2 und 4 mit der neuen Session-ID. Die Simulator-Session ist im Logger an der
Seriennummer `simulator0001` erkennbar.
