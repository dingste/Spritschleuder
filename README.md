# Spritschleuder 2

Cocktailautomat mit ESP32-C3, 128×128-ST7735-Display, Weboberfläche und einem separaten Arduino für vier Pumpen.

## Schnellstart für die Entwicklung

Voraussetzung ist eine eingerichtete ESP-IDF-Umgebung (aktuell getestet mit ESP-IDF 6.0.2).

```sh
. /opt/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Beim ersten Build lädt der ESP-IDF Component Manager die offiziellen Komponenten `espressif/qrcode` und `espressif/mdns`. Die erzeugte ESP32-C3-Firmware liegt anschließend unter `build/cocktailbot.bin`.

## Installation und Verdrahtung

Der Aufbau besteht aus dem ESP32-C3, einem separaten Arduino als Pumpencontroller und einem ULN-Treiber für die vier Pumpen. Vor dem Anschließen die Versorgung ausschalten. Arduino, ESP32 und ULN-Treiber benötigen eine gemeinsame Masse (GND). Die Pumpenversorgung muss für die Pumpen und den Treiber geeignet sein; nicht die Pumpen über einen GPIO versorgen.

Die vier bisherigen Pumpenstufen werden nach bisherigem Aufbau über den ULN-Treiber an Arduino D2 bis D5 gesteuert. Die Zuordnung ist Pumpe 1 bis 4 zu D2, D3, D4 und D5. Prüfe vor dem Einschalten, ob das zur tatsächlichen Platinenverdrahtung passt.

Das vieradrige Kabel am ESP32 ist wie folgt belegt. Die Bezeichnungen IO6 und IO7 sind die ESP32-C3-GPIOs; TX1/RX0 bezeichnen die Arduino-Pins laut bisheriger Verdrahtungsnotiz:

| Leitung | ESP32-C3 | Arduino |
| --- | --- | --- |
| Rot | 5 V | 5 V |
| Schwarz | GND | GND |
| Weiß | IO7 (TX) | TX1 (Arduino RX) |
| Gelb | IO6 (RX) | RX0 (Arduino TX) |

UART wird gekreuzt angeschlossen: ESP32-TX geht zum Arduino-RX, ESP32-RX zum Arduino-TX. Prüfe anhand der Beschriftung und des Arduino-Modells, dass TX1 und RX0 tatsächlich die verwendeten UART-Pins sind. Beide Geräte müssen dieselbe Logik-GND haben. Die UART-Signale dürfen nur mit passenden Logikpegeln verbunden werden; ein Arduino mit 5-V-TX benötigt gegebenenfalls einen Pegelwandler zum 3,3-V-RX des ESP32-C3.

Die 5-V-Versorgung des ESP32 und Arduino ist mit einer 5-V-Quelle verbunden. Ob der Arduino zusätzlich per USB versorgt wird, ist nicht mehr sicher bekannt; ebenso ist ein zuvor verwendeter Buck/Boost-Wandler nicht bestätigt. Prüfe die reale Versorgung vor dem Einschalten und vermeide, zwei 5-V-Ausgänge (zum Beispiel USB und externe Versorgung) gegeneinander zu schalten. Falls ein Buck/Boost-Wandler verbaut ist, Ausgangsspannung und Polarität vor dem Anschluss messen.

```text
                         gemeinsame Masse (GND)
  5-V-Quelle  ───────┬──────────── ESP32-C3 5V
                     └──────────── Arduino 5V
  GND         ───────┬──────────── ESP32-C3 GND
                     ├──────────── Arduino GND
                     └──────────── ULN-Treiber GND

  ESP32-C3 IO7 / TX  ───────────── Arduino TX1 / RX
  ESP32-C3 IO6 / RX  ───────────── Arduino RX0 / TX

  Arduino D2 ── ULN-Eingang 1       ULN-Ausgang 1 ── Pumpe 1
  Arduino D3 ── ULN-Eingang 2       ULN-Ausgang 2 ── Pumpe 2
  Arduino D4 ── ULN-Eingang 3       ULN-Ausgang 3 ── Pumpe 3
  Arduino D5 ── ULN-Eingang 4       ULN-Ausgang 4 ── Pumpe 4
                                            Pumpenversorgung nach Treiberaufbau
```

Die UART-Pinbelegung muss zu `main/config.h` passen. Pumpe und Treiber müssen für die vorhandene Pumpenspannung dimensioniert sein. Beim ULN-Treiber die Anschlüsse für induktive Lasten/Freilaufbeschaltung gemäß verwendetem Treibermodul und Pumpenversorgung anschließen.

### Firmware aufspielen

1. ESP-IDF einrichten und die ESP32-Firmware bauen. Beim erstmaligen Umstellen auf die geänderte Partitionstabelle zuerst die Hinweise unter [Schnellstart](#schnellstart-für-die-entwicklung) beachten.
2. ESP32-C3-Firmware und bei der Ersteinrichtung das SPIFFS-Abbild flashen.
3. Die Arduino-Firmware aus `main/arduino_main.cpp` und `main/pump_controller.cpp` mit Arduino-IDE oder PlatformIO auf den Pumpencontroller laden. Dazu die UART-Leitungen wie im Abschnitt [Arduino flashen](#arduino-flashen) vorübergehend trennen.
4. UART wieder verbinden, die Versorgung einschalten und den LED-Selbsttest abwarten. D2 bis D5 sollten nacheinander und danach gemeinsam schalten. Zunächst ohne angeschlossene Pumpen prüfen.
5. Setup-Access-Point `Spritschleuder-Setup` verbinden (Passwort `cocktail`), `http://192.168.4.1` öffnen und WLAN einrichten. Anschließend Durchfluss in den Einstellungen kalibrieren.

Der Schaltplan nennt einen Winbond W25Q128. Dieser Baustein hat 128 Mbit beziehungsweise 16 MiB Flash. Die Partitionstabelle lässt NVS und PHY an ihren bisherigen Adressen, vergrößert die App-Partition auf 2 MiB und weist den restlichen Speicher ab `0x210000` (knapp 14 MiB) SPIFFS zu. Die erste NVS-Partition bleibt bei `0x9000` und 24 KiB, damit WLAN und Mixturen erhalten bleiben. Vor dem ersten Flashen der neuen Partitionstabelle die reale Flash-ID des Boards mit `esptool.py -p /dev/ttyACM0 flash_id` gegenprüfen. Beim Umstellen nicht `erase-flash` verwenden.

Die Weboberfläche wird als SPIFFS-Datei gebaut und im lokalen Netz unverschlüsselt per HTTP bereitgestellt. Die Firmware ist unter `http://spritschleuder.local` erreichbar; im Setup-Access-Point auch unter `http://192.168.4.1`.

Das SPIFFS-Abbild wird bei jedem Build als `build/webdata.bin` aktualisiert, aber absichtlich nicht bei jedem normalen Firmware-Flash geschrieben. Beim erstmaligen Einrichten der geänderten Partitionstabelle müssen Partitionstabelle und Firmware geflasht sowie das SPIFFS-Abbild einmalig ab `0x210000` geschrieben werden, zum Beispiel mit `esptool.py --chip esp32c3 --port /dev/ttyACM0 write_flash 0x210000 build/webdata.bin`. Ein späteres Aktualisieren der Weboberfläche erfordert nur das erneute Schreiben dieses Abbilds; dabei gehen Bilder in diesem Dateisystem verloren, daher vorher sichern.

Der Pumpencontroller ist ein eigenes Ziel: `main/arduino_main.cpp` und `main/pump_controller.cpp` müssen zusätzlich mit der Arduino-IDE oder PlatformIO auf den angeschlossenen Arduino geladen werden. Nach jedem ESP32-Reset fordert der ESP32 den LED-Test beim Arduino an: D2 bis D5 nacheinander je eine Sekunde, danach alle vier gemeinsam eine Sekunde. Der Arduino quittiert den Testbefehl erst nach Ende der Sequenz; anschließend verarbeitet er normale UART-Befehle.

Die globale Durchflusskalibrierung wird in den Einstellungen der Weboberfläche als Laufzeit für 2 ml eingestellt und im ESP32-NVS gespeichert. Der ESP32 übermittelt den Faktor beim Start und bei jeder Änderung an den Arduino. Der Startwert von 0,9 Sekunden für 2 ml entspricht der bisherigen Annahme von 450 ms/ml; zur Kalibrierung kann der Betreiber eine bekannte Zeit laufen lassen, das Volumen messen und diesen einen globalen Richtwert anpassen.

## Bedienung

- Ohne gespeichertes WLAN startet das Gerät den Access Point `Spritschleuder-Setup` mit Passwort `cocktail`. Die Weboberfläche ist dann unter `http://192.168.4.1` erreichbar.
- Im Heim-WLAN lautet die Geräteadresse `http://spritschleuder.local` (mDNS).
- Wenn das gespeicherte WLAN nach acht Wiederholungen nicht erreichbar ist, werden die Zugangsdaten gelöscht, der Setup-Access-Point gestartet und die Infoansicht mit QR-Code automatisch angezeigt.
- K1 (GPIO 8) stoppt alle Pumpen unmittelbar. Nach ungefähr drei Sekunden kehrt das Display selbstständig zur Hauptansicht zurück; es wird nicht auf ein Arduino-ACK gewartet.
- K2 (GPIO 10) schaltet zwischen Haupt- und Infoansicht um. Die Infoansicht zeigt IP-Adresse und QR-Code zur Geräte-URL.
- Neue Mixturen werden in der Weboberfläche als relative ml-Anteile je Pumpe angelegt und dauerhaft im NVS gespeichert. Maximal sind 20 Mixturen vorgesehen.
- CSV-Mengen sind ebenfalls relative Anteile, keine festen Pumpenlaufzeiten. Beispiel: `Schlabber,40,40,40,40` ergibt bei 200 ml Bestellung 50 ml je Pumpe. Mit dem Standardfaktor von 0,9 Sekunden für 2 ml laufen die Pumpen dann etwa 22,5 Sekunden parallel. Eine lange Laufzeit ist daher nicht automatisch eine UART-Verzögerung.
- Die Weboberfläche zeigt Mixturauswahl, Portionsgröße und einen geschätzten Mischfortschritt. Kalibrierung und WLAN-Konfiguration liegen unter „Einstellungen“.

### Arduino flashen

Vor dem Flashen des Arduino die UART-Leitungen zwischen ESP32 und Arduino trennen (ESP-TX/Arduino-RX und ESP-RX/Arduino-TX). Der USB-Seriell-Adapter und der ESP32 dürfen beim Upload nicht gleichzeitig die Arduino-UART-Pins treiben. Nach dem Upload und einem Reset die UART-Leitungen wieder verbinden.

## Aufbau

- `main/main.cpp`: Initialisierung und Startreihenfolge
- `main/cocktail_server.cpp`: HTTP-Server und HTTP-API
- `data/www/index.html`: Weboberfläche in SPIFFS
- `main/local_discovery.cpp`: mDNS-Gerätename `spritschleuder.local`
- `partitions.csv`: 2-MiB-App und SPIFFS bis zum Ende des 16-MiB-Flashs
- `main/recipes.cpp`: Rezeptverwaltung und NVS-Persistenz
- `main/flow_calibration.cpp`: globaler Durchflussfaktor und NVS-Persistenz
- `main/arduino_comm.cpp`: UART-Protokoll, Befehlswarteschlange und Abbruch
- `main/arduino_main.cpp`: Firmware des separaten Pumpen-Arduinos
- `main/display_manager.cpp`: Displayzustände, Fortschritt und QR-Code
- `main/button_manager.cpp`: entprelltes Polling der beiden Gerätetasten
- `main/config.h`: Pins, WLAN-, UART- und Displaykonfiguration

## UART-Protokoll

Ein Pumpenbefehl besteht aus fünf Bytes:

```text
0xAA  PUMP_ID  AMOUNT_ML_HI  AMOUNT_ML_LO  0x55
```

Die angeforderte Menge ist eine 16-Bit-Zahl in Millilitern. Der Arduino multipliziert sie mit dem globalen ms/ml-Wert und verwaltet je Pumpe eine eigene Abschaltzeit. Mehrere Startbefehle werden direkt hintereinander übertragen und die Pumpen laufen parallel. Eine Menge `0` schaltet die betreffende Pumpe sofort aus. Pumpen-ID `0xFD` setzt den globalen Faktor; der Arduino bestätigt die Übernahme. Beim Abbruch werden AUS-Befehle für alle vier Pumpen gesendet. Pumpen-ID `0xFE` mit Menge `0` startet den LED-Selbsttest; dessen ACK kommt erst nach der vollständigen Sequenz.

## HTTP-API

- `GET /api/status` – WLAN-Status und Mischfortschritt
- `GET /api/recipes` – Liste aller Mixturen
- `POST /api/recipes` – Mixtur mit `name` und `pump0_ml` bis `pump3_ml` speichern
- `GET /api/recipes.csv` – Mixturen als CSV herunterladen (`name,pump0_ml,pump1_ml,pump2_ml,pump3_ml`)
- `POST /api/recipes/import` – CSV importieren; ersetzt die bestehende Mixturliste nach vollständiger Prüfung
- `POST /api/order` – Mixtur mit `cocktail_id` und `quantity_ml` starten
- `POST /api/abort` – laufende Mischung sofort abbrechen
- `GET/POST /api/calibration` – globalen Richtwert „Sekunden für 2 ml“ abfragen oder speichern
- `POST /api/wifi` und `POST /api/wifi/reset` – WLAN konfigurieren beziehungsweise löschen

## Hinweise für Änderungen

Nach Änderungen immer mindestens `idf.py build` ausführen. Bei Arbeiten an Pumpensteuerung oder UART müssen beide Firmwares aktualisiert und gemeinsam auf echter Hardware geprüft werden. Pinänderungen gehören zentral nach `main/config.h`; das Board-Schaltbild liegt als `ESP32-C3-1.44inch.pdf` im Projektordner.
