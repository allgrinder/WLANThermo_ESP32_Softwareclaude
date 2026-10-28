# Sicherheitsprüfung des Gas-Forks

Stand: Branch `feature/gas-servo-autotune`, Firmware-Basis v1.2.4. Geprüft wurden Firmware
(`src/`), WebUI, `update-server/` und das Verhalten im Fehlerfall, wo möglich im Simulator.

**Status: nur Bericht.** Maßnahmen setze ich erst nach deinem Okay um (STOP-Punkt 3). Bereits
umgesetzt sind lediglich die Sicherheitsfunktionen, die Phase B ausdrücklich verlangt hat; sie sind
unten als „bereits umgesetzt“ markiert.

**Kontext:** Das Gerät dreht den Gasregler eines Brenners **ohne Zündsicherung**. Jeder, der die
Firmware oder den Pitmaster kontrolliert, kann den Brenner auf große Flamme stellen. Geht die Flamme
aus, strömt weiter Gas.

## Übersicht

| # | Befund | Risiko | Empfehlung (Kurz) |
|---|---|---|---|
| F-1 | OTA-Update über HTTP ohne Signatur. Jeder Server, der auf `api.wlanthermo.de` antwortet, kann mit `force` Firmware aufspielen. | **kritisch** | Signaturprüfung (ECDSA P-256) vor dem Boot-Partitionswechsel |
| F-2 | Ohne gesetztes Web-Passwort sind alle Steuer-Endpunkte offen. Einige sind auch **mit** Passwort offen (`/setDC`, `/setnetwork`, `/restart`, `/checkupdate`, `/configreset` …). | **hoch** | Passwortpflicht für alles, was Brenner, Update oder Netz betrifft |
| F-3 | Antworten jedes Servers (auch der Custom-Cloud) laufen durch denselben Parser: `url` überschreibt die Hosts dauerhaft, `update.force` startet ein Update. | **hoch** | `url` nur akzeptieren, wenn ausdrücklich gewollt; `update` nur aus der Update-Anfrage |
| F-4 | MQTT `/set/pitmaster`, `/set/pid`, `/set/system` … ohne eigene Prüfung, Klartext-MQTT | mittel–hoch | Schalter „MQTT darf steuern“ (Default aus), Broker-ACL |
| F-5 | Geheimnisse ohne Anmeldung lesbar (`/settings`: MQTT-Passwort, Cloud-Token; `/getpush`: Telegram-/Pushover-Token) | mittel | Nur mit Passwort ausliefern oder maskieren |
| F-6 | Neustart (Watchdog, Absturz, Stromspitze) mit gespeichertem Modus „aus“ lässt den Servo stehen, wo er war (Simulator: 80 % bleiben 80 %). Im Recovery-Modus läuft gar keine Regelung. | **hoch** | Beim Booten Servo aktiv auf `minOutput` fahren, dann abkoppeln; Task-Watchdog für die Regelung |
| F-7 | Recovery-Modus: offener Firmware-Upload über AP mit festem Passwort `12345678`. Ohne Web-Passwort ist er per `GET /recovery` aus dem WLAN auslösbar. | hoch | `/recovery` nur mit Passwort; Recovery-AP-Passwort gerätespezifisch |
| F-8 | Basic-Auth über unverschlüsseltes HTTP, im WLAN mitlesbar | mittel | Akzeptieren (kein TLS auf dem Gerät); WLAN mit WPA2/3, Gerät nicht ins Gästenetz |
| F-9 | Container: Update-Verteilung und Logger ohne Anmeldung (nur Heimnetz-Filter) | niedrig–mittel | Admin- und Schreib-Endpunkte mit Token; Ports nie nach außen |
| F-10 | Wer Schreibrechte am GitHub-Fork hat, kann ein Release bauen, das `wlt-update` anbietet | mittel | 2FA, Branch-Schutz; mit F-1 nur signierte Releases ausliefern |
| F-11 | Fehlerfälle: WLAN-Verlust, Neustart im Autotune, Abbruch, Netzteil-Wegfall | – | Bewertung unten; überwiegend bereits abgesichert |
| F-12 | Gesamtsystem: keine Zündsicherung | **kritisch (physisch)** | Thermoelement-Zündsicherung nachrüsten; Software ersetzt sie nicht |

---

## F-1 Unsignierte OTA-Updates über HTTP

**Befund:**

- `Cloud.cpp` schickt alle API-Requests per Klartext-HTTP an `http://api.wlanthermo.de/`.
- `OtaUpdate::doFirmwareUpdate()` lädt das Image mit `WiFiClient` (ohne TLS) über `httpUpdate`.
  `Update.end()` schaltet die Boot-Partition um, sobald die ESP32-Image-Prüfsumme stimmt. Eine
  Herkunftsprüfung gibt es nicht.
- Enthält die Antwort `update.force` (der Wert ist egal, es reicht der Schlüssel), startet das
  Update **ohne Nutzeraktion** (`WebHandler.cpp`, `setServerAPI`).
- Bei aktivem Auto-Update fragt das Gerät nach jedem Start.

Angreifbar ist das für jeden, der DNS oder Pfad kontrolliert: das eigene WLAN, eine manipulierte
FritzBox, jemand auf dem Weg zum Server. Genau diese Lücke nutzen wir mit `wlt-update` selbst.

**Risiko: kritisch.** Fremde Firmware kann den Brenner beliebig ansteuern und die
Sicherheitslogik (minOutput, Flamme aus, Parken) abschalten.

**Empfehlung: Signaturprüfung im Fork.**

| Punkt | Bewertung |
|---|---|
| Machbarkeit | Der gepatchte Core (tuniii/arduino-esp32 `WifiFix`, IDF 3.3) enthält mbedTLS mit `CONFIG_MBEDTLS_ECDSA_C`, `SECP256R1`, `pk`, `sha256`, `rsa`. Die Arduino-`Update`-Klasse 1.0.x hat **keine** eingebaute Signaturprüfung, also selbst umsetzen. |
| Ablauf | (1) Image in die freie OTA-Partition streamen (eigene Schleife statt `httpUpdate`, wie schon in `/fwupload`), dabei SHA-256 mitrechnen. (2) Signatur laden: `nanoV3.bin.sig`, 64 Byte raw ECDSA P-256 oder DER, vom Update-Server bzw. als Release-Asset. (3) `mbedtls_pk_verify` mit dem öffentlichen Schlüssel aus der Firmware. (4) Nur bei Erfolg `Update.end(true)` und damit `esp_ota_set_boot_partition`, sonst `Update.abort()`. |
| Schlüssel | Privater Schlüssel als GitHub-Actions-Secret; `release.yml` signiert und hängt `.sig` an. Alternativ offline auf deinem PC signieren und das `.sig` selbst hochladen (sicherer, mehr Handarbeit). Öffentlicher Schlüssel als Konstante in der Firmware. |
| Kosten | Flash geschätzt +30–50 KB (ECP, bignum, pk, asn1), bei 1,53 MB von 6 MB belegt unkritisch. RAM während der Prüfung einige KB, Dauer unter 1 s. |
| Wirkung | Gilt für Server-Updates **und** `/fwupload`. Auch `wlt-update` könnte dann nur noch signierte Fork-Releases einspielen. |
| Rollback auf offizielle Firmware | Die ist unsigniert und würde abgelehnt. Vorschlag: unsigniertes Image nur über `/fwupload` **mit** Web-Passwort **und** Tastendruck am Gerät innerhalb von 30 s (physische Anwesenheit), oder nur per USB. |
| Nicht empfohlen | ESP32 Secure Boot und Flash Encryption (eFuses): unumkehrbar, Rückweg zur offiziellen Firmware wäre dauerhaft verbaut. |

## F-2 Endpunkte ohne Anmeldung

Werkszustand: **kein Passwort** (`WServer::requireAuth()` ist false). Dann ist alles offen. Mit
Passwort schützt die Tabelle `nanoWebHandlerList` nur Einträge mit gesetztem `authRequestMethod`.

| Endpunkt | Wirkung | ohne PW | mit PW |
|---|---|---|---|
| `/setpitmaster`, `/setpid`, `/setautotune` (neu), `/settelemetry` (neu), `/setsystem`, `/setchannels`, `/setIoT`, `/setpush`, `/setapi`, `/setbluetooth` | Brenner, Profile, Server-URLs | offen | geschützt |
| **`/setDC`** | Duty-Cycle-Test: Servo 10 s auf beliebigen Wert, z. B. 100 % | offen | **offen** |
| **`/setnetwork`** | WLAN ändern | offen | **offen** |
| **`/restart`**, `/stop` | Neustart, Pitmaster aus | offen | **offen** |
| **`/checkupdate`** | fragt Update-Server; mit F-1/F-3 Update ohne Klick | offen | **offen** |
| `/update` (POST) | Update starten | offen | geschützt |
| **`/configreset`, `/clearwifi`, `/stopwifi`** | Konfiguration und WLAN löschen | offen | **offen** |
| `/recovery` | Recovery-Modus (F-7) | offen | geschützt |
| **`/setbattmin`, `/settestmode`, `/newtoken`** | Akku-Grenze, Cloud-Intervall, Token | offen | **offen** |
| `/fwupload` (neu) | Firmware-Upload | **gesperrt** | geschützt + Pitmaster muss aus sein |

**Risiko: hoch.** Jeder im WLAN (auch ein kompromittiertes IoT-Gerät) kann den Brenner per
`/setDC` auf große Flamme stellen, sogar mit gesetztem Passwort.

**Empfehlung:**

1. Für alles, was Brenner, Update, Netz oder Neustart betrifft, Auth erzwingen: `/setDC`,
   `/setnetwork`, `/restart`, `/stop`, `/checkupdate`, `/configreset`, `/clearwifi`, `/stopwifi`,
   `/setbattmin`, `/settestmode`, `/newtoken`. Ausnahme: `/setnetwork` im AP-Modus bei der
   Ersteinrichtung.
2. Steuer-Endpunkte (`/setpitmaster`, `/setpid`, `/setautotune`, `/setDC`) **verweigern**, solange
   kein Passwort gesetzt ist. Die WebUI zeigt dann einen Hinweis mit Link auf `/setadmin`.
3. **Offizielle App:** Die App im Repo (`mobile_flutter`) ist eine Hülle um die Geräte-WebUI. Die
   WebUI fängt 401 ab und zeigt einen Anmeldedialog (`show-auth-popup` in `App.vue`), mit Passwort
   funktioniert sie also weiter. Cloud-Ansicht und Push sind nicht betroffen. Ungeprüft sind
   Fremd-Apps und eigene Skripte, die die API ohne Basic-Auth nutzen; sie bräuchten dann
   Zugangsdaten. **Vor dem Umsetzen bitte mit deinem Handy prüfen.**

## F-3 Server-Antworten können Hosts umschreiben und Updates auslösen

**Befund:**

- `Cloud::onReadyStateChange` übergibt **jede** 200-Antwort an `setServerAPI`, auch die der
  Custom-Cloud-URL.
- Ein `url`-Objekt landet dauerhaft in `/url.json` (`Cloud::saveUrl()`). Danach spricht das Gerät
  für immer mit dem neuen Host.
- `update.force` startet ein Update.

**Risiko: hoch.** Eine einzige manipulierte Antwort reicht, um das Gerät dauerhaft umzulenken.
`wlt-update` entfernt `url` und `update` aus weitergeleiteten Antworten; das schützt aber nur,
solange die Umleitung aktiv ist.

**Empfehlung:**

- `update` nur aus Antworten auf eine Update-Anfrage (`APIUPDATE`) auswerten.
- `url` ignorieren oder nur mit Signatur annehmen.
- Antworten der Custom-Cloud gar nicht parsen.

## F-4 MQTT-Steuerung

**Befund:**

- Bei aktivem MQTT ruft `Mqtt::onMqttMessage` für `…/set/channels|system|pitmaster|pid|iot`
  direkt die Web-Handler auf. Eine eigene Prüfung gibt es nicht.
- Die Verbindung ist Klartext (`AsyncMqttClient` ohne TLS).

**Risiko:** mittel bis hoch, je nach Broker. Wer auf den Broker schreiben darf, steuert den Brenner.

**Empfehlung:**

- Einstellung „MQTT darf steuern“, Default **aus**; `/get/*` bleibt erlaubt.
- Broker mit Benutzer und ACL, Schreibrecht auf `…/set/#` nur für vertrauenswürdige Clients.

## F-5 Geheimnisse lesbar

**Befund:**

- `/settings` (ohne Auth) liefert `iot.PMQpass` (MQTT-Passwort) und `CLtoken`.
- `/getpush` liefert Telegram- und Pushover-Tokens.
- `/settings` geht zusätzlich per MQTT raus.

**Risiko:** mittel. Mit den Tokens lassen sich Push-Kanäle missbrauchen, mit dem MQTT-Passwort
lässt sich F-4 ausnutzen.

**Empfehlung:**

- Passwörter in `/settings` maskieren (`"***"`, nur schreiben, nie zurückgeben).
- `/getpush` nur mit Auth.

## F-6 Neustart mit Pitmaster „aus“, Recovery-Modus, Watchdog

**Befund (im Simulator bestätigt, `test/sim`, Ausgabe `info: restart with saved type off`):**

- Nach einem Neustart initialisiert die Firmware den Servo erst, wenn der Pitmaster wieder regelt.
  Steht der gespeicherte Modus auf „aus“, bekommt der Servo keine Pulse mehr und bleibt mechanisch
  dort, wo er war. Das kann große Flamme sein, und der Nutzer sieht „aus“.
- Nach 10 Neustarts in Folge (Reset-Zähler) bzw. per `/recovery` startet das Gerät im
  Recovery-Modus. Dort läuft **keine** Regelung.
- Es gibt keinen Task-Watchdog für die Regelung: Hängt der System-Task, behält die LEDC-Hardware
  den letzten Pulswert bei.

**Risiko: hoch.**

**Empfehlung:**

1. Beim Booten bei `SERVO` immer zuerst aktiv auf `minOutput` fahren, dann je nach gespeichertem
   Modus weiter (aus: parken und abkoppeln). Das ist dieselbe Funktion wie das bestehende sichere
   Parken.
2. Den System-Task beim ESP32-Task-Watchdog anmelden (`esp_task_wdt_add`, z. B. 10 s). Ein Hänger
   führt dann zum Reset, und Punkt 1 greift.
3. Im Recovery-Modus den Servo vor dem Start des AP auf `minOutput` fahren.

**Bereits umgesetzt (Phase B):**

- Neustart **während eines Autotune**: Das Gerät hält nach dem Boot `minOutput` im Modus manuell,
  Status „Neustart während Autotune“ (Simulator: 39 % → 12 %).
- Geplanter Neustart (`/restart`, nach OTA): Servo wird vorher geparkt (`PitmasterGrp::parkAll`).

## F-7 Recovery-Modus

**Befund:**

- AP `WLANThermo-RecoveryMode` mit festem Passwort `12345678`.
- Firmware-Upload ohne Anmeldung (`RecoveryMode.cpp`, `Update.begin/write/end`).
- Auslösbar per Taster beim Start (3 s) oder per `GET /recovery`. Ohne Web-Passwort geht das aus
  dem WLAN.

**Risiko: hoch.** Jemand in Funkreichweite kann nach einem `/recovery` beliebige Firmware aufspielen.

**Empfehlung:**

- `/recovery` nur mit Passwort.
- AP-Passwort gerätespezifisch, z. B. aus der Seriennummer abgeleitet und auf dem Display angezeigt.
- Mit F-1 auch hier nur signierte Images.

## F-8 Basic-Auth über HTTP

Das Gerät kann kein TLS-Webinterface. Das Passwort ist im WLAN mitlesbar, wenn jemand den Verkehr
sieht.

**Empfehlung:** Akzeptieren und dokumentieren. Das Gerät nicht in ein offenes WLAN oder Gästenetz
hängen, kein Port-Forwarding. Fernzugriff nur per VPN oder Tailscale.

## F-9 Container `wlt-update` / `wlt-dns`

| Punkt | Stand |
|---|---|
| Ports nach außen | keine; eigene IPs im Heimnetz (macvlan), kein Port-Forwarding nötig. **Kein** Port-Forwarding in der FritzBox auf `.250` oder `.251` einrichten. |
| Zugriff | Middleware lässt nur `ALLOWED_NETS` (Heimnetz) zu. Innerhalb des Heimnetzes sind aber alle Endpunkte offen, inkl. `/admin/poll`, `/requests`, `/sessions`, `/telemetry`. |
| Schreibend | `/telemetry` und `/custom` nehmen Daten ohne Anmeldung an; Folge wäre nur verfälschte Analyse. Firmware kann man dem Container per HTTP **nicht** unterschieben (nur GitHub-Releases oder Host-Ordner `fw/import`). |
| GitHub-Token | nur nötig, wenn der Fork privat ist. Dann fein granular, nur dieses Repo, „Contents: Read-only“, als `.env`-Variable (nicht im Git) oder per Docker-Secret (`GITHUB_TOKEN_FILE`). |
| Container-Härtung | `wlt-update`: Nicht-Root, `read_only`, `cap_drop: ALL`, `no-new-privileges`. `wlt-dns`: nur Bind- und Setuid-Rechte. |
| DNS-Schleife | `wlt-update` löst den echten Server über 1.1.1.1/9.9.9.9 auf und verweigert, wenn die Antwort die eigene IP ist. `wlt-dns` fragt nie die FritzBox. |
| Antworten | `url` und `update` werden aus weitergeleiteten Antworten entfernt; `force` nur, wenn das Gerät es selbst angefragt hat. |

**Empfehlung:**

- Ein optionales Token (`ADMIN_TOKEN`) für `/admin/*`, `/requests` und die Session-Exporte.
- `wlt-dns` nach dem Erst-Update stoppen (steht in INSTALL-GAS.md).

## F-10 Lieferkette

**Befund:** Wer auf deinen GitHub-Account oder Fork schreiben kann, kann ein Release bauen. `wlt-update`
bietet es an, das Gerät installiert es nach einem Klick, mit `force` sogar ohne.

**Empfehlung:**

- 2FA auf dem GitHub-Account; Tag-Schutz für `v*`.
- Mit F-1 signiert der private Schlüssel. Liegt er nur offline bei dir, reicht ein gekaperter
  GitHub-Account allein nicht mehr.

## F-11 Fehlerfälle (Fail-safe)

| Fall | Verhalten | Nachweis |
|---|---|---|
| WLAN-Verlust | Regelung, Autotune, Flamme-aus-Erkennung und Buzzer laufen im System-Task unabhängig vom WLAN weiter. Nur Push und Telemetrie fallen aus; die Telemetrie verwirft, ohne zu blockieren. | Code: `SystemBase::task` ruft `pitmasters.update()`, das Netzwerk läuft im `ConnectTask`. |
| Abbruch Autotune (Übertemperatur, Fühler weg, Stopp, Profil-, Kanal- oder Sollwechsel, Flamme aus) | Servo fährt auf `minOutput`, nach 1,5 s abgekoppelt, Pitmaster aus | Simulator: alle Pfade ✔ |
| Neustart während Autotune | Nach dem Boot aktiv auf `minOutput`, Modus manuell | Simulator ✔ |
| Neustart sonst, Modus „aus“ | **Servo bleibt stehen** (F-6) | Simulator: 80 % → 80 % |
| Absturz, Watchdog | wie Neustart; ein hängender Task wird heute nicht erkannt (F-6) | Code |
| USB-Versorgung weg | Pitmaster deaktiviert (`PitmasterGrp::enable(false)`), Servo zuerst auf `minOutput`. Ohne Versorgung bewegt sich der Servo aber ohnehin nicht. | Code |
| Flamme aus | erkannt in 2–5,5 min, danach `minOutput` gehalten, Alarm; das Gas strömt bei kleinster Stufe weiter | Simulator ✔ |

## F-12 Physische Sicherheit

Software kann eine fehlende Zündsicherung nicht ersetzen. Die Flamme-aus-Erkennung ist eine
Temperaturheuristik mit Minuten Verzug; in der Zeit strömt Gas. Der Servo kann die Stellung
„AUS“ nicht erreichen.

**Empfehlung (wichtigste Maßnahme überhaupt):**

- Thermoelement-Zündsicherung nachrüsten (handelsübliche Nachrüstsätze für Gasbrenner).
- Gasflasche im Freien; Smoker bei Betrieb in Ruf- und Sichtweite.
- Zusätzlich einen Gas-/CO-Melder in der Nähe, wenn der Smoker unter einem Dach steht.

---

## Vorschlag zur Umsetzung (wartet auf dein Okay)

Reihenfolge nach Nutzen und Aufwand:

| Paket | Inhalt | Aufwand | Folge für dich |
|---|---|---|---|
| S1 | F-6: Servo beim Booten auf `minOutput` + Task-Watchdog + Recovery parkt Servo | klein | keine |
| S2 | F-2 + F-5 + F-7 (teilweise): Auth für alle Brenner-, Netz-, Update- und Neustart-Endpunkte; Steuerung erst mit gesetztem Passwort; Geheimnisse maskieren; `/recovery` mit Auth | mittel | Web-Passwort nötig; App mit Passwort prüfen |
| S3 | F-3: Antwort-Parser einschränken (`url` ignorieren, `update` nur aus Update-Antworten, Custom-Cloud nicht parsen) | klein | keine |
| S4 | F-4: Schalter „MQTT darf steuern“ (Default aus) | klein | MQTT-Steuerung muss bewusst eingeschaltet werden |
| S5 | F-1 + F-10: ECDSA-Signatur für Firmware (CI signiert oder Offline-Schlüssel), Prüfung bei Server-Update und Upload; unsignierte Images nur mit Passwort + Tastendruck oder USB | groß | Schlüssel verwalten; Rollback zur offiziellen Firmware nur noch so |
| S6 | F-9: `ADMIN_TOKEN` im Container | klein | Token für Export und Admin |

Jedes Paket bekäme Simulator-Tests (S1) bzw. pytest/Integrationstests (S6) und ein eigenes Release
`v1.2.4-gas.N+1`.
