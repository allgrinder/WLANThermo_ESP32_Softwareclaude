# Intelligente Regelung („Smart“) für den Gas-Smoker

Branch `feature/gas-smart-control`. Gilt nur für Profile mit gesetztem Schalter „Intelligente
Regelung“ (Profil „Gas Servo“: an). Alle anderen Profile rechnen bitgenau wie upstream.

## Ziel

Einmal tunen, dann bei 110, 120, 130 oder 150 °C ohne Nachdenken: schnell auf Temperatur, ohne
Überschwingen, und nach dem Deckel-Öffnen schnell wieder ruhig.

## Was der Smart-Modus anders macht

| Baustein | Was er tut | Warum |
|---|---|---|
| **Lernende Öffnungs-Tabelle** (`BiasMap`) | Merkt sich pro Temperatur (60–240 °C, alle 10 °C), welche Servo-Öffnung die Temperatur hält. Gelernt wird nach 5 min ruhigem Halten (±1,5 °C). Den ersten Punkt liefert der Autotune, dauerhaft gespeichert. | Wegen der Ventilkennlinie braucht jede Temperatur eine andere Öffnung. Mit der Tabelle weiß der Regler das vorher. |
| **Vorsteuerung** | Bei Kaltstart, Sollwechsel, nach dem Deckel und nach „Flamme aus“ quittiert startet der I-Anteil direkt beim gelernten Wert. Fehlt der Wert, wird er aus den Nachbarpunkten geschätzt. | Der PID muss nur noch den Rest korrigieren statt den ganzen Haltewert langsam aufzubauen. |
| **Integrationszone ±3 °C** | Der I-Anteil arbeitet nur nahe am Sollwert. Ausnahme: Die Temperatur steht 3 min lang außerhalb der Zone, dann reicht der P-Anteil allein nicht. | Verhindert das Aufstauen beim Anheizen und nach dem Deckel. Das war die Hauptursache für bis zu 14 °C Überschwingen. |
| **Anti-Windup am Anschlag** | Kein Integrieren, solange der Ausgang auf 100 % bzw. auf dem Mindestausgang steht und der Fehler weiter in diese Richtung zeigt. | Wie oben. |
| **D-Anteil auf die Messung** | Der D-Anteil reagiert auf die Temperaturänderung statt auf den Fehler. | Kein Gas-Stoß bei Sollwechsel; bremst stattdessen einen schnellen Anstieg. |

**Getestet und verworfen:** Anheizen mit Bremspunkt (Vollgas bis zur vorhergesagten Ankunft).
Die Vorhersage aus Steigung × Trägheit ist bei Totzeit zu ungenau; Sollsprünge überschwangen damit
bis 6,7 °C. Der Code ist nicht enthalten.

**Neuer Autotune** verwirft die Tabelle und setzt den neuen Startpunkt. Meist tunt man nach einer
Mechanik-Änderung neu. Zusätzlich gibt es den Knopf „Gelerntes zurücksetzen“ (Pitmaster-Seite
bzw. `/setautotune` mit `cmd: resetLearned`).

## Messung (Simulator, `test/sim/gas_sim --quality`)

Ablauf je Smoker-Modell (P1 schnell, P2 mittel, P3 träge):

1. Einmal Autotune bei 130 °C.
2. Mit diesen Werten je ein Grillabend bei 110/120/130/150 °C:
   - Kaltstart bei 20 °C
   - nach 60 min Sollwert +15 °C
   - nach weiteren 40 min zurück
   - 10 min halten
   - Deckel 60 s offen
   - 30 min beobachten

Das Deckel-Modell ist bewusst hart: −70 °C in 60 s, ohne Wärmespeicher.

„Nach einigen Grillabenden“: je ein vorheriger Grillabend bei 130, 110, 150 und 120 °C hat die
Tabelle gefüllt.

### Vorher: Regler v1.2.4-gas.1

| Strecke | Soll | Anheizen bis Soll−2 [min] | Überschwingen Start [°C] | eingeschwungen ±2 [min] | +15: Überschw. [°C] | +15: eingeschw. [min] | −15: Unterschw. [°C] | −15: eingeschw. [min] | Halten max [°C] | Deckel: Überschw. [°C] | Deckel: erholt [min] |
|---|---|---|---|---|---|---|---|---|---|---|---|
| P1 | 110 | 4.0 | 3.80 | 12.5 | 0.35 | 2.9 | 0.32 | 2.9 | 0.32 | 6.06 | 15.4 |
| P1 | 120 | 4.7 | 2.99 | 12.1 | 0.34 | 3.4 | 0.31 | 3.0 | 0.39 | 7.23 | 17.1 |
| P1 | 130 | 5.6 | 1.79 | 5.6 | 0.19 | 4.0 | 0.24 | 4.4 | 0.27 | 8.51 | 19.6 |
| P1 | 150 | 8.5 | 0.32 | 8.5 | 0.11 | 6.0 | 0.33 | 6.4 | 0.28 | 6.96 | 20.1 |
| P2 | 110 | 5.9 | 6.44 | 24.4 | 0.43 | 4.4 | 0.74 | 3.2 | 0.31 | 5.96 | 23.5 |
| P2 | 120 | 6.7 | 5.47 | 25.2 | 0.38 | 4.9 | 0.31 | 4.8 | 0.35 | 7.15 | nie |
| P2 | 130 | 7.7 | 4.50 | 23.5 | 0.22 | 5.7 | 0.16 | 5.1 | 0.30 | 8.12 | nie |
| P2 | 150 | 10.2 | 2.13 | 17.5 | 0.09 | 7.7 | 0.08 | 7.8 | 0.34 | 10.35 | nie |
| P3 | 110 | 6.8 | 13.64 | 44.4 | 0.81 | 5.1 | 1.17 | 5.0 | 0.64 | 5.64 | nie |
| P3 | 120 | 8.2 | 8.74 | 39.4 | 0.54 | 6.0 | 0.34 | 4.5 | 0.29 | 6.67 | nie |
| P3 | 130 | 9.3 | 7.39 | 38.6 | 0.42 | 6.7 | 0.14 | 6.8 | 0.44 | 7.94 | nie |
| P3 | 150 | 11.9 | 4.94 | 38.2 | 0.16 | 8.7 | 0.02 | 9.0 | 0.39 | 9.83 | nie |

### Smart, erster Grillabend (nur Autotune-Punkt bekannt)

| Strecke | Soll | Anheizen bis Soll−2 [min] | Überschwingen Start [°C] | eingeschwungen ±2 [min] | +15: Überschw. [°C] | +15: eingeschw. [min] | −15: Unterschw. [°C] | −15: eingeschw. [min] | Halten max [°C] | Deckel: Überschw. [°C] | Deckel: erholt [min] |
|---|---|---|---|---|---|---|---|---|---|---|---|
| P1 | 110 | 4.3 | 1.77 | 4.3 | 1.30 | 2.4 | 0.70 | 2.7 | 0.37 | 0.54 | 4.8 |
| P1 | 120 | 5.0 | 1.26 | 5.0 | 1.30 | 2.5 | 0.54 | 2.6 | 0.33 | 0.80 | 5.2 |
| P1 | 130 | 5.8 | 0.43 | 5.9 | 0.17 | 3.8 | 0.53 | 2.5 | 0.32 | 0.62 | 5.8 |
| P1 | 150 | 12.5 | 0.19 | 13.1 | 0.65 | 3.3 | 1.08 | 1.9 | 0.30 | 0.55 | 6.8 |
| P2 | 110 | 6.4 | 2.42 | 13.4 | 2.13 | 10.0 | 0.75 | 3.2 | 0.41 | 0.52 | 6.8 |
| P2 | 120 | 7.3 | 1.91 | 7.3 | 2.46 | 13.8 | 0.54 | 2.9 | 0.40 | 0.58 | 7.4 |
| P2 | 130 | 8.4 | 1.37 | 8.4 | 0.21 | 5.5 | 0.59 | 2.8 | 0.34 | 0.52 | 8.2 |
| P2 | 150 | 11.8 | 0.20 | 11.9 | 0.16 | 6.3 | 0.59 | 2.3 | 0.34 | 0.53 | 9.7 |
| P3 | 110 | 8.7 | 1.68 | 8.7 | 1.65 | 4.4 | 1.54 | 5.0 | 0.41 | 0.55 | 8.5 |
| P3 | 120 | 9.9 | 1.16 | 10.1 | 1.40 | 5.1 | 0.47 | 4.5 | 0.33 | 0.50 | 9.2 |
| P3 | 130 | 11.7 | 0.56 | 11.7 | 0.26 | 6.9 | 0.55 | 4.0 | 0.39 | 0.53 | 10.4 |
| P3 | 150 | 21.1 | -0.01 | 23.2 | 0.38 | 7.8 | 0.80 | 3.5 | 0.41 | 0.44 | 12.4 |

### Smart, nach einigen Grillabenden (Tabelle gelernt)

| Strecke | Soll | Anheizen bis Soll−2 [min] | Überschwingen Start [°C] | eingeschwungen ±2 [min] | +15: Überschw. [°C] | +15: eingeschw. [min] | −15: Unterschw. [°C] | −15: eingeschw. [min] | Halten max [°C] | Deckel: Überschw. [°C] | Deckel: erholt [min] |
|---|---|---|---|---|---|---|---|---|---|---|---|
| P1 | 110 | 4.9 | 0.55 | 4.9 | 1.60 | 2.2 | 0.66 | 2.6 | 0.41 | 0.63 | 4.7 |
| P1 | 120 | 5.4 | 0.57 | 5.4 | 1.56 | 2.3 | 0.54 | 2.6 | 0.33 | 0.80 | 5.2 |
| P1 | 130 | 5.5 | 1.09 | 5.5 | 1.11 | 2.7 | 0.52 | 2.5 | 0.32 | 0.62 | 5.8 |
| P1 | 150 | 7.0 | 0.58 | 7.0 | 1.21 | 3.0 | 1.07 | 2.0 | 0.27 | 0.55 | 6.7 |
| P2 | 110 | 7.2 | 0.60 | 7.2 | 1.76 | 3.2 | 0.80 | 3.2 | 0.31 | 0.58 | 6.8 |
| P2 | 120 | 8.2 | 0.60 | 8.2 | 1.58 | 3.7 | 0.50 | 2.9 | 0.34 | 0.53 | 7.4 |
| P2 | 130 | 8.5 | 1.11 | 8.5 | 1.23 | 4.2 | 0.59 | 2.7 | 0.31 | 0.52 | 8.2 |
| P2 | 150 | 10.7 | 0.65 | 10.7 | 0.95 | 4.9 | 0.56 | 2.4 | 0.36 | 0.49 | 9.7 |
| P3 | 110 | 9.5 | 0.78 | 9.5 | 0.91 | 5.1 | 1.51 | 5.0 | 0.46 | 0.64 | 8.5 |
| P3 | 120 | 10.6 | 0.71 | 10.7 | 1.80 | 4.8 | 0.46 | 4.5 | 0.41 | 0.49 | 9.4 |
| P3 | 130 | 11.1 | 1.19 | 11.1 | 1.46 | 5.4 | 0.49 | 4.0 | 0.40 | 0.54 | 10.4 |
| P3 | 150 | 13.9 | 0.59 | 13.9 | 0.93 | 6.7 | 0.59 | 3.7 | 0.40 | 0.51 | 12.4 |


**Ohne Autotune**, nur mit den Profil-Startwerten (Kp 1,5 / Ki 0,002 / Kd 60), Kaltstart, Halten
und +20 °C (`--defaults`):

| | gas.1 | Smart |
|---|---|---|
| Überschwingen Kaltstart | bis 6,9 °C | bis 0,4 °C |
| Halten max | bis 0,6 °C | bis 1,9 °C (P1 bei 150 °C), sonst ≤ 0,6 °C |
| Überschwingen +20 °C | bis 1,5 °C | bis 0,3 °C |

## Zusammenfassung

| | gas.1 | Smart (gelernt) |
|---|---|---|
| Überschwingen beim Kaltstart | 0,3–13,6 °C | **0,5–1,2 °C** |
| Eingeschwungen nach Kaltstart (±2 °C) | 5,6–44 min | **4,9–13,9 min** |
| Überschwingen nach Deckel | 6–10 °C | **0,5–0,8 °C** |
| Erholt nach Deckel | 15 min bis nie | **4,7–12,4 min** |
| Sollsprung ±15 °C | ≤ 1,2 °C | ≤ 1,8 °C (schneller eingeschwungen) |
| Anheizen bis Soll−2 | 4,0–11,9 min | 4,9–13,9 min (etwas langsamer, ohne Überschwingen) |

Abnahme im Smart-Modus (`make -C test/sim test` läuft beide Modi):
- alle 179 Prüfungen der bestehenden Suite
- zusätzlich 30 Qualitätsgrenzen: Kaltstart- und Deckel-Überschwingen < 3 °C, eingeschwungen bzw.
  erholt ≤ 20 min, Halten ±2 °C, +15 °C-Sprung < 5 °C
- Unit-Tests der Tabelle

10 weitere Zufalls-Seeds in beiden Modi ohne Fehler.

## Grenzen

- Die Simulation nutzt die Modelle P1–P3. Wie gut die Tabelle auf deinem El Fuego lernt, zeigt
  der erste echte Grillabend mit Telemetrie. `tools/fit_plant.py` passt dann das Modell an, und die
  Qualitätsmessung lässt sich mit `--plant` wiederholen.
- Wind oder Regen verändern den Haltewert. Die Tabelle lernt das mit (Zeitkonstante 10 min ruhiges
  Halten), springt aber nicht sofort.
