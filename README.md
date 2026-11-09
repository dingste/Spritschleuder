# Spritschleuder 2

Cocktailautomat mit ESP32-C3, 128×128-ST7735-Display, Weboberfläche und einem separaten Arduino für vier Pumpen.

## Schnellstart für die Entwicklung

Voraussetzung ist eine eingerichtete ESP-IDF-Umgebung (aktuell getestet mit ESP-IDF 6.0.2).

```sh
. /opt/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Beim ersten Build lädt der ESP-IDF Component Manager die offizielle Komponente `espressif/qrcode`. Die erzeugte ESP32-C3-Firmware liegt anschließend unter `build/cocktailbot.bin`.

Der Pumpencontroller ist ein eigenes Ziel: `main/arduino_main.cpp` und `main/pump_controller.cpp` müssen zusätzlich mit der Arduino-IDE oder PlatformIO auf den angeschlossenen Arduino geladen werden. Nach jedem ESP32-Reset fordert der ESP32 den LED-Test beim Arduino an: D2 bis D5 nacheinander je eine Sekunde, danach alle vier gemeinsam eine Sekunde. Der Arduino quittiert den Testbefehl erst nach Ende der Sequenz; anschließend verarbeitet er normale UART-Befehle.

Die globale Durchflusskalibrierung wird in den Einstellungen der Weboberfläche als Laufzeit für 2 ml eingestellt und im ESP32-NVS gespeichert. Der ESP32 übermittelt den Faktor beim Start und bei jeder Änderung an den Arduino. Der Startwert von 0,9 Sekunden für 2 ml entspricht der bisherigen Annahme von 450 ms/ml; zur Kalibrierung kann der Betreiber eine bekannte Zeit laufen lassen, das Volumen messen und diesen einen globalen Richtwert anpassen.

## Bedienung

- Ohne gespeichertes WLAN startet das Gerät den Access Point `Spritschleuder-Setup` mit Passwort `cocktail`. Die Weboberfläche ist dann unter `http://192.168.4.1` erreichbar.
- Wenn das gespeicherte WLAN nach acht Wiederholungen nicht erreichbar ist, werden die Zugangsdaten gelöscht, der Setup-Access-Point gestartet und die Infoansicht mit QR-Code automatisch angezeigt.
- K1 (GPIO 8) stoppt alle Pumpen unmittelbar. Nach ungefähr drei Sekunden kehrt das Display selbstständig zur Hauptansicht zurück; es wird nicht auf ein Arduino-ACK gewartet.
- K2 (GPIO 10) schaltet zwischen Haupt- und Infoansicht um. Die Infoansicht zeigt IP-Adresse und QR-Code zur Geräte-URL.
- Neue Mixturen werden in der Weboberfläche als relative ml-Anteile je Pumpe angelegt und dauerhaft im NVS gespeichert. Maximal sind 20 Mixturen vorgesehen.
- Die Weboberfläche zeigt Mixturauswahl, Portionsgröße und einen geschätzten Mischfortschritt. Kalibrierung und WLAN-Konfiguration liegen unter „Einstellungen“.

## Aufbau

- `main/main.cpp`: Initialisierung und Startreihenfolge
- `main/cocktail_server.cpp`: eingebettete Weboberfläche und HTTP-API
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
- `POST /api/order` – Mixtur mit `cocktail_id` und `quantity_ml` starten
- `POST /api/abort` – laufende Mischung sofort abbrechen
- `GET/POST /api/calibration` – globalen Richtwert „Sekunden für 2 ml“ abfragen oder speichern
- `POST /api/wifi` und `POST /api/wifi/reset` – WLAN konfigurieren beziehungsweise löschen

## Hinweise für Änderungen

Nach Änderungen immer mindestens `idf.py build` ausführen. Bei Arbeiten an Pumpensteuerung oder UART müssen beide Firmwares aktualisiert und gemeinsam auf echter Hardware geprüft werden. Pinänderungen gehören zentral nach `main/config.h`; das Board-Schaltbild liegt als `ESP32-C3-1.44inch.pdf` im Projektordner.
