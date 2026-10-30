# Stufentest 2026-10-30 (El Fuego, erster Lauf mit Gas)

Handbetrieb mit festen Servo-Stufen, um die Strecke zu vermessen. Kein Grillgut.

## Aufbau

- Profil 4 „Gas Servo“, SPmin 0, SPmax 45 (100 % ≈ 80° am Ventil, Min→Max ≈ 90°)
- Fühler 1 Mitte (Regelkanal), Fühler 2 unten über der Wasserschale, Fühler 3 ganz oben
- Wasserschale voll zu Beginn, Garage 10–12 °C, Tor offen, Deckel die ganze Zeit zu
- Mitschrieb `GET /data` alle 5 s: [stufentest_5s.csv](stufentest_5s.csv) (Cloud-Format,
  Spalten wie `tools/analyze_cook.py` sie liest, plus `flameOut`, `lid`, `soc`, `rssi`)

## Ablauf

| Uhrzeit | Servo | Bemerkung |
|---|---|---|
| 20:34 | 0 % | Smoker schon gezündet, Fühler 1 bei 65 °C |
| 20:35:29 | 100 % | 20:36:43–20:38:28 keine Daten (Gerät lief am Akku, schwaches WLAN) |
| 20:57:25 | 50 % | |
| 21:42:24 | 25 % | 21:47 Fehlalarm „Flamme aus“ des Mitschreibers (Flamme brannte) |
| ca. 22:05 | 25 % | Wasserschale leer (ab hier „trocken“) |
| 22:17:28 | 0 % | |
| 22:48:20 | – | Gas von Hand zu, danach Abkühlen mit geschlossenem Deckel bis 23:02 |

![Verlauf](cook.png)

## Ergebnisse (Fühler 1)

| Servo | eingeschwungen | Wasser |
|---|---|---|
| 100 % | ~176–178 °C (Spitze 179 °C, als das Wasser zu kochen begann) | ja |
| 50 % | ~175,5 °C | ja |
| 25 % | ~107 °C (Minimum 22:06) | knapp |
| 0 % | ~118,7 °C | leer |
| Gas aus | 118,7 → 51,7 °C in 13 min | leer |

- **Sättigung:** Oberhalb von etwa 50 % Servo kommt kaum noch Wärme dazu (2–3 °C). Ob das am Ventil
  liegt oder an der kochenden Wasserschale, die bei mehr Gas mehr verdampft, trennt dieser Lauf nicht.
- **Zeitkonstante:** nach Gasänderungen ≈ 6 min bis 63 %; Abkühlen ohne Flamme ≈ 12 min.
- **Wasser:** Die volle Schale hält bei 100–180 °C etwa 1,5 h. Ohne Wasser liegt der Garraum bei
  gleichem Gas deutlich höher (0 % trocken 118,7 °C > 25 % nass 107 °C).
- **Verteilung:** Bei viel Gas sind Mitte und oben fast gleich (±4 °C), unten über dem Wasser
  35–40 °C kühler. Bei 25 % liegen alle drei Ebenen innerhalb von 2 °C.
- **Flammenausfall:** Der Schritt 50 → 25 % ließ Fühler 1 in 5 min um 36 °C fallen, genauso schnell
  wie nach echtem Gas-aus. Eine reine Fallgeschwindigkeits-Schwelle unterscheidet das nicht.
- **WLAN:** RSSI −67 bis −87 dBm in der Garage; am Akku antwortete das Gerät zeitweise nicht.

## Folgerungen

- SPmax 45 → 25 (mit dem Nutzer abgestimmt, noch nicht gesetzt), damit der wirksame Bereich den
  ganzen Regelweg nutzt.
- Vor dem Autotune prüfen, ob die Flammenausfall-Erkennung nach Schritten nach unten
  (Relay-Autotune, Sollwert senken) Fehlalarme auslöst.
