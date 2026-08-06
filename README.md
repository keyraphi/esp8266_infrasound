# Infraschall Sensor
TODO referenz zu Stefan Holzheu

## Hardware

### Bauteile
- Sensirion SDP600-25Pa Differenzdrucksensor
- D1 Mini (ESP8266, Micro-USB)
- D1 Mini Pro (ESP8266, Micro-USB, ext. WiFI-Antenne)
- SD-Card Kartenmodul für Arduino
- edi-tronic ABS Leergehäuse IP66 oder Bopla ET-215
- sonstiges: Jumper Wire, SD-Karte, Kabelverschraubung M12+M16, Stiftleiste zweireihig

### Verdrahtung
SDP600-25 | D1 Mini 
--------- | -------- 
Data    | D2   
Gnd     | Gnd
VDD 3V3 | 3V3   
Clk     | D1

D1 Mini | D1 Mini pro
------- | -------- 
Gnd   | Gnd   
5V    | 5V
D5    | D2/Tx   
D6    | D1/Rx

D1 Mini pro | SD Card Reader
----------- | -------- 
Gnd     | Gnd   
3V3     | 3V3
D5/SCLK | CLK
D6/MISO | MISO
D7/MOSI | MOSI
D8/CS   | CS   

## Software
Der D1 Mini ist eine kleine Platine mit einem ESP8266 Microcontroller.
Sie enthält alles was nötig ist um unabhängig (nur mit Stromversorgung) ein Programm auszuführen.
Wir haben zwei dieser Platinen.
Eine davon ist dafür zuständig in streng regelmäßigen Abständen (50 Hz) den Sensor abzutasten.
Diese Messwerte werden dann über eine Serielle Schnittstelle an die andere Platine geschickt.
Das Programm der zweiten Platine empfängt diese Messwerte und dient also
WebServer. Die Messwerte werden regelmäßig auf die SD-Karte geschrieben.
Außerdem stellt dieser Webserver eine Webseite bereit, auf der die Messwerte
grafisch aufbereitet dargestellt werden.
Die Webseite besteht aus HTML, CSS, JavaScript und WebAssembly Dateien, die also statische Dateien auf der SD Karte gespeichert sind.

### Setup
Nachdem der Sensor verdrahtet ist müssen die beiden Controller programmiert werden. Dazu muss erst die richtige Software installiert werden.

* Zuerst muss die [Arduino IDE](https://www.arduino.cc/en/software) installiert werden.
* Die Arduino IDE kennt den ESP8266 Controller standardmäßig nicht. Das kann
allerdings nachinstalliert werden indem die Schritte under "Installing with
Board Manager" hier: [Arduino core for ESP WiFichip](https://github.com/esp8266/Arduino) befolgt werden.
* Für Windows muss ggf. noch der Treiber "CP210x VCP Windows" installiert von [https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers?tab=downloads](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers?tab=downloads) werden.
* Unsere Programme verwenden einige Bibliotheken, die standardmäßig nicht zur
Verfügung stehen und erst in der Arduino IDE installiert werden müssen. Das
geht bequem in der Arudino IDE mit dem Library Manager (v1: im Menü under
Tools, v2: rechts in der Leiste). Folgende Bibliotheken müssen installiert
werden:
    - NTPClient
    - ArduinoJson
    - ArduinoUniqueId
    - AsyncTCP
    - ESP8266TimerInterrupt
    - ESPAsyncTCP
    - ESPAsyncWebserver (ACHTUNG: von lacamera, getestet mit version 3.1.0)
    - TimerInterrupt

**Achtung:** Die beiden Platinen sprechen über eine Kabelverbindung miteinander, und das Format
dieser Verbindung hat sich geändert (Geschwindigkeit und Aufbau der Datenpakete). Eine Platine mit
alter Software kann deshalb nicht mehr mit einer Platine mit neuer Software kommunizieren. Wenn Sie
den Sensor auf eine neue Version aktualisieren, müssen **immer beide Platinen** neu programmiert
werden – nicht nur eine.

**Achtung, zweiter Teil:** Beim Aktualisieren reicht es nicht, nur die beiden Platinen neu zu
programmieren. Die Webseite liegt nicht im Programm, sondern im Order `static` auf der SD-Karte
(siehe [Statische Dateien](#statische-dateien)). Kopieren Sie deshalb bei jedem Update **auch den
`static` Order neu auf die SD-Karte**, bevor Sie den Sensor wieder einschalten. Wird das
vergessen, läuft neue Firmware mit einer alten Webseite: Die Seite sieht dann kaputt aus, in der
Liste der Messungen steht `[object Object]` statt der Dateinamen, und jeder Klick auf „Download"
oder „Analyse" meldet, dass es die Messung nicht gibt.

Konkret läuft diese Verbindung jetzt mit **38400 Baud** statt vorher 9600 Baud. Diese
Geschwindigkeit ist bewusst gewählt und nicht beliebig: Schneller sollte es nicht sein, weil die
Webserver-Platine, die die Daten empfängt, gleichzeitig WLAN und den Webserver betreibt. Die
verwendete Bibliothek für die serielle Verbindung (`EspSoftwareSerial`) nennt 115200 Baud als
Obergrenze und warnt, dass es dabei bei viel gleichzeitigem Datenverkehr gelegentlich zu
Bitfehlern kommen kann, weil das Timing von Interrupts auf einem ESP nie ganz exakt ist. Bei
38400 Baud bleibt etwa dreimal so viel zeitlicher Spielraum wie an dieser Obergrenze. Langsamer
sollte es aber auch nicht sein: Bei den früheren 9600 Baud war die Sensor-Platine in jedem
Messintervall von 20 ms rund 10,4 ms lang allein mit dem Versenden der Daten beschäftigt; bei
38400 Baud sind es nur noch etwa 2,9 ms, was Zeit für die eigentliche Sensor-Messung und ein
etwas größeres Datenpaket lässt. Wichtig dabei: Die höhere Geschwindigkeit macht die Verbindung
für sich genommen nicht robuster gegen Aussetzer – der Empfangspuffer der Webserver-Platine fasst
eine bestimmte Anzahl an Datenpaketen, und Pakete kommen unabhängig von der
Übertragungsgeschwindigkeit mit 50 pro Sekunde an. Robuster gegen Aussetzer wurde die Verbindung
durch größere Puffer, nicht durch die höhere Geschwindigkeit.

**Die Geschwindigkeit ändern:** Der Wert steht an genau einer Stelle, in
`esp8266_infrasound_webserver/infrasound_frame.h` als `LINK_BAUD`. Ändern Sie ihn dort, führen Sie
danach `python tools/sync_shared.py` aus – das kopiert die Datei unverändert in den Sensor-Order,
damit beide Platinen denselben Wert benutzen – und programmieren Sie anschließend **beide**
Platinen neu. Stimmen die beiden Werte nicht überein, sprechen die Platinen gar nicht mehr
miteinander; die Verbindung wird dann nicht etwa langsamer, sondern kommt überhaupt nicht zustande.

Ob es auf der Verbindung Übertragungsfehler gibt, sehen Sie am Zähler `crc` in der `DIAG`-Zeile,
die der Webserver einmal pro Minute auf die USB-Konsole schreibt. Er zählt Datenpakete, die wegen
einer falschen Prüfsumme verworfen wurden – solche Pakete sind verlorene Messwerte. Steigt dieser
Zähler im laufenden Betrieb spürbar an, kann eine langsamere Verbindung (z. B. 19200 Baud) helfen,
weil dabei mehr zeitlicher Spielraum bleibt.

### Sensor
**Aktualisieren Sie nur den Sensor?** Lesen Sie zuerst den Achtung-Hinweis weiter oben im Abschnitt
„Setup" – in der Regel müssen dabei **beide** Platinen neu programmiert werden, nicht nur diese.

Bevor die D1 Mini Platine programmiert werden kann muss ein Arduino Projekt
angelegt werden. In Arduino-Slang spricht man von einem "Sketch". Alle Arduino
sketches leben in einem festgelegten Order:

 * Windows: `C:\Users\{username}\Documents\Arduino`
 * macOS: `/Users/{username}/Documents/Arduino`
 * Linux: `/home/{username}/Arduino`

Kopieren Sie den `arduino_infrasound_sensor` Order dort hin. Under Windows und
Mac müssen Sie evtl. die Datei `arduino_infrasound_sensor.ino.cpp` umbenennen
zu `arduino_infrasound_sensor.ino`, und den existierenden Link damit ersetzen.
Ein Arduino Sketch braucht immer die Endung `.ino`.

Jetzt kann die Platine programmiert werden:
- `arduino_infrasound_sensor.ino` Sketch in der Arduino IDE öffnen.
- USB Kabel mit dem D1 Mini verbinden, der an den Sensor gelötet ist.
- _Generic ESP8266 Module_ also Board auswählen (v1: Tools->Board, v2:
SelectBoard->Boards) und USB ausgang wählen (v1: Tools->Port, v2:
SelectBoard->Ports).
- Board Programmieren. Dies geht mit dem Upload Button in der IDE (Pfeil nach Rechts).

Fertig.

### Webserver
**Aktualisieren Sie nur den Webserver?** Auch hier gilt der Achtung-Hinweis weiter oben im
Abschnitt „Setup" – in der Regel müssen **beide** Platinen neu programmiert werden, nicht nur diese.

Beim Webserver funktioniert es genau gleich.
Kopieren Sie erst den `esp8266_infrasound_webserver` Order in ihren Sketch Order und benennen Sie ggf. `esp8266_infrasound_webserver.ino.cpp` zu `esp8266_infrasound_webserver.ino` um.

Programmieren Sie den zweiten D1 Mini:
- `esp8266_infrasound_webserver.ino` Sketch in der Arduino IDE öffnen.
- USB kabel mit dem Board verbinden.
- _Generic ESP8266 Module_ also Board auswählen und Port setzen
- Wählen Sie unter Tools → Flash Size eine Aufteilung mit mindestens 2 MB Dateisystem (auf dem D1
  Mini Pro z. B. „16MB (FS:4MB OTA:~1019KB)"). Das Dateisystem nimmt die Webseiten-Dateien auf, die
  weiter unten unter [Statische Dateien](#statische-dateien) beschrieben sind. **Achtung:** Wird
  diese Aufteilung später geändert, wird das Dateisystem gelöscht und muss neu befüllt werden (dazu
  reicht ein Neustart mit SD-Karte, siehe unten).
- Board programmieren durch den Upload button.

Fertig.

### Statische Dateien
Die Software auf den Microcontroller ist recht überschaubar. Im Endeffekt werden nur die Messwerte auf die SD Karte geschrieben und under Umständen durch einen Websocket an die Clients geschickt.
Alles weitere passiert dann auf der Webseite und wird direkt beim Nutzer im Browser berechnet und dargestellt.
Dieses Web-Programm befindet sich im Order `static`. Dieser Order muss vollständig auf die SD-Karte geschrieben werden:
- Erstellen Sie einen Order `www` direkt auf der SD Karte.
- Kopieren sie den `static` Order in diesen neuen `www` Order.

Fertig

Beim Start kopiert der Webserver diese Dateien automatisch von der SD-Karte in seinen eigenen
internen Speicher (denselben, in dem auch das Programm selbst liegt – siehe den Schritt zur
Flash-Aufteilung weiter oben). Das passiert bei jedem Neustart, aber es werden nur Dateien kopiert,
die sich seit dem letzten Mal geändert haben; ein normaler Neustart ohne Änderungen ist deshalb
sehr schnell. Sobald die Dateien einmal erfolgreich kopiert wurden, funktioniert die Webseite auch
dann noch, wenn die SD-Karte danach entfernt wird oder gerade nicht lesbar ist. Die SD-Karte bleibt
trotzdem wichtig: Dort werden die Messwerte gespeichert, und wenn Sie eine Datei in `static`
ändern, kopieren Sie sie wie gewohnt auf die Karte in den `www` Order – beim nächsten Neustart wird
die Änderung automatisch übernommen.

Optional: Wenn Sie neben einer Datei in `static` zusätzlich eine vorkomprimierte Version mit der
Endung `.gz` ablegen (z. B. `app.js.gz` neben `app.js`), liefert der Webserver automatisch die
komprimierte Version aus, sofern vorhanden. Das ist nicht notwendig und rein optional.

## Sensor betreiben
Der Sensor hat drei Betriebsmodi:
1. Messmodus: Für Langzeitmessungen draußen.
2. Live-view-Modus: Für Live Demonstrationen - stabile Internetverbindung ist Notwendig!
3. Analysemodus: Zur Analyse von zuvor aufgenommenen Messungen - Internetverbindung ist Notwendig!

### 1. Messmodus
Sobald der Sensor mit strom versorgt ist (die Webserver Platine per USB-Kabel Strom bekommt) beginnt der Sensor nach 10 sekunden mit einer Messung.
Die Messwerte werden binär fortlaufend in eine Datei im Order `measurements/` geschrieben.
Wenn der Sensor Internet hat, enthält der Name der Datei den Zeitpunkt der Aktivierung; ohne
Internet heißt die Datei `unbekannt`. Alle 24 Stunden beginnt automatisch eine neue Datei, damit
keine einzelne Datei zu groß wird; dabei wird nie eine bestehende Datei überschrieben. Details zum
genauen Dateiformat, zur Dateibenennung und zum CSV-Export finden Sie im Abschnitt „Messdateien und
CSV-Export" weiter unten.

Der Messmodus läuft solange bis der Sensor ausgesteckt wird oder ein neues WLAN eingestellt wird oder in den Analysemodus gewechselt wird.

### 2. Live-View-Modus
Der Live-View-Modus ist genau gleich wie der Messmodus, nur, dass sich jemand übers W-LAN mit dem Sensor verbunden hat.
In diesem Modus überträgt der Sensor in Echtzeit sämtliche Messwerte an das verbundene Gerät, wo sie dann grafisch aufbereitet dargestellt werden.

Um sich mit dem Sensor zu verbinden ist eine __stabile__ WiFi Verbindung notwendig, welche wie folgt eingerichtet werden kann. Die SSID sollte keine Großbuchstaben enthalten.
1. Entweder über SD-Karte:
    1. Lege eine Datei `wifi_ssid_pw.txt` im root-Verzeichnis an.
    2. Inhalt: 1. Zeile: SSID, 2. Zeile: Passwort, 3. Zeile: `307`
2. Oder über AduinoIDE:
    1. In der AduinoIDE den SerialMonitor öffnen(TODO ... das muss ich im code ändern ... die aktuelle lösung ist blöd.)
    2. Sensor mit Computer verbinden.
    3. Sobald im Serial Monitor die Nachricht "SSID Eingeben" erscheint kann oben die SSID (der Name) ihres WLAN Netzes eingegeben werden und mit senden bestätigen. Wird 10 sekunden lange nichts eingegeben beginnt die offline-messung im Messmodus.
    4. Sobald im Serial Monitor die Nachricht "Passwort Eingeben" erscheint kann das WLAN passwort eingegeben werden und absenden. Hierfür bleiben 60 Sekunden Zeit. Danach beginnt der Sensor auch automatisch im offline modus mit messungen.
    5. Der Sensor versucht sich zu verbinden. Falls es nicht funktioniert startet der Sensor neu und es beginnt wieder bei Schritt 3.
    6. Der Sensor Bestätigt die Verbindung zum WLAN und zeigt seine IP-Adresse an. Diese kann bei einem Beliebigen Gerät im selben WLAN-Netzwerk in den Browser getippt werden um sich mit dem Sensor zu verbinden.
    7. Das Browserfenster zeigt die Messungen in Echtzeit an.

ACHTUNG: Der Live-View-Modus funktioniert gleichzeitig bei mehreren Geräten oder Browser Fenstern. Allerdings wird die Messung für alle unterbrochen wenn eines dieser Geräte in den Analysemodus wechselt.

Im Live-View-Modus können die Messdaten in vier unterschiedlichen Modalitäten beobachtet werden:
1. Zeitserie: Der Sensor misst den Druckunterschied zwischen außerhalb der Box und innerhalb der Box mit einer Frequenz von 50 Hz. Diese Messungen werden in der Zeitserie direkt dargestellt.
2. Spektrum: Das Spektrum stellt einen Ausschnitt der letzten Messungen im Frequenzraum dar. Mit der Messfrequenz von 50 Hz kann Infraschall bis zu 25 Hz aufgezeichnet werden.
Im Spektrum ist das Signal in einzelne Frequenzen zwischen 0 Hz und 25 Hz
zerlegt. Für jede Frequenz wird die Lautstärke des Sinus-Tons angegeben, mit
der diese Frequenz in dem gemessenen Signal vorkommt. Würden alle diese
Sinus-Töne gleichzeitig in der angegebenen Lautstärke abgespielt käme genau
dasselbe Signal zustande, das in der Zeitserie dargestellt ist. Die Einheit der
y-Achse des Spektrums ist abhängig vom gewählten Darstellungsmodus. Die rohen
Messungen vom Sensor geben den Druckunterschied in Pascal (Pa) an. Das
Menschliche Ohr nimmt Lautstärke allerdings nicht linear sonder logarithmisch
wahr. Daher können die Amplituden auch also Schalldruckpegel in dB(SPL)
angezeigt werden. SPL steht für Sound Preassure Level. Die dB(SPL) werte haben
aber auch noch keinen Bezug zur tatsächlichen Wahrnehmung von Infraschall.
Deshalb kann das Spektrum also [G-Bewerteter
Schalldruckpegel](https://pub.dega-akustik.de/DAGA_2021/data/articles/000511.pdf)
dargestellt werden mit der Einheit dB(G). Die typische Wahrnehmungsschwelle für
G-Bewerteten Schalldruckpegel liegt bei 95 db(G) - 100 db(G) mit einer
Standardabweichung von etwa 5 dB(G). Es sind keine Fälle bekannt wo Menschen
Schalldruckpegel unterhalb von 85 dB(G) wahrnehmen konnten. Für details der Standardisierung siehe [ISO-7196-1995.pdf](ISO-7196-1995.pdf).
3. Dauerschallpegel: Ganz oben wird der Dauerschallpegel, sprich die Gesammtlautstärke des Signals über den Analysezeitraum angezeigt - je nach option in Pa (RMS), dB(SPL) oder db(G).  
4. Spektrogram: Das Spektrogram zeigt eine Farblich kodierte Historie über alle Spektren an, die auf die Breite der Seite passen. Die neusten Spektren werden rechts eingeführt und wandern langsam nach links. Wenn dB(G) als Darstellungsmodus gewählt wurde ist der Wertebereich für die Farbkodierung fixiert zwischen 0 dB(G) und 100 dB(G).

### 3. Analysemodus
Vom Live-View-Modus kann in den Analysemodus gewechselt werden indem im Menü zu Analyse gewechselt wird.
Dadurch wird die aktuelle Messreihe auf dem Sensor **gestoppt**, damit er die gespeicherten Daten in der vollen Geschwindigkeit zur Verfügung stellen kann ohne immer wieder neue Messungen aufzeichnen zu müssen.
Hier wird eine Liste aller Messdateien dargestellt, die auf dem Sensor gespeichert sind.
Jede Messung kann entweder als CSV-Datei heruntergeladen werden (z.B. zur Weiterverarbeitung in Excel oder Python) oder direkt im Browser analysiert werden.
Für die Analyse stehen zwei Darstellungen bereit.
1. Der G-Bewerteter Dauerschallpegel über die Zeit dargestellt. Hier lässt sich
   untersuchen wie die Lautstärke des Signals sich während der Messung entwickelt hat. In dem selben Diagramm sind 95 dB(G) als Menschliche Wahrnehmungsschwelle fest eingezeichnet. 
2. Das Spektrogram der gesamten Messung. Je länger die Messung ging, desto komprimierter wird es dargestellt. Hier die Lautstärke der einzelnen Frequenzen über den Verlauf der Messung farblich kodiert dargestellt. Die Auflösung der Frequenzen kann mit einem Regler ausgewählt werden.

## Status-LED
Die kleine LED direkt auf der Webserver-Platine zeigt den Betriebszustand des Sensors an, auch
ohne dass ein Computer angeschlossen ist:

| Muster | Bedeutung |
|---|---|
| dauerhaft an | Keine SD-Karte gefunden |
| doppeltes Blinken | Läuft, schreibt aber nicht auf die SD-Karte |
| dreifaches Blinken | Läuft, aber es kommen keine Daten vom Sensor |
| schnelles Blinken | Läuft, keine Uhrzeit (kein WLAN/NTP) |
| langsames, gleichmäßiges Blinken (1 s an, 1 s aus) | Messung bewusst gestoppt (Analysemodus) |
| kurzes Blitzen alle 2 s | Misst gerade, Uhrzeit gesetzt |

Das dreifache Blinken zeigt an, dass zwar eine Messung laufen soll, aber seit über zwei Sekunden
keine Daten vom Sensor-Board mehr angekommen sind. Das ist wichtig, weil ein abgestürztes oder
abgeklemmtes Sensor-Board sonst unsichtbar bliebe: Die Messung gilt intern weiterhin als aktiv, und
die Logdatei bleibt ohne Schreibfehler geöffnet, sodass ohne dieses Muster der normale Herzschlag
weiterlaufen würde, obwohl nichts aufgezeichnet wird. Das langsame, gleichmäßige Blinken bedeutet
dagegen, dass die Messung bewusst gestoppt wurde (Analysemodus) – das ist normal und kein Fehler.
Das doppelte Blinken ist ausschließlich für echte Störungen reserviert (SD-Karte voll oder
Schreibfehler) und bedeutet immer, dass etwas nicht in Ordnung ist.

Das kurze Blitzen alle 2 Sekunden bedeutet „Gerät läuft, hat eine Uhrzeit und misst gerade" – anders
als früher ist der Herzschlag jetzt kein bloßes Versprechen, dass auch aufgezeichnet wird: Fehlende
Daten und ein bewusster Stopp haben inzwischen jeweils ihr eigenes, unterscheidbares Muster.

## Diagnose-Zeile (DIAG)
Neben der Status-LED schreibt der Webserver einmal pro Minute eine zweite, genauere
Diagnose-Zeile auf seine USB-Konsole. Sie sieht zum Beispiel so aus:

```
DIAG ovf=1 crc=9 frm=50 qfull=0 sdfail=0 maxloop=179 heap=9216 ntpfail=0 reboots=0
```

Sieben der neun Zahlen zählen Probleme und sollten im Idealfall bei 0 bleiben. Zwei davon,
`maxloop` und `heap`, sind keine Fehler, sondern reine Beobachtungswerte.

Die sieben Fehler-Zähler laufen seit dem Einschalten des Geräts mit und werden nie zurückgesetzt.
Das ist der wichtigste Punkt beim Lesen dieser Zeile: Bleibt eine Zahl über mehrere Minuten
konstant, ist das zugehörige Problem einmal aufgetreten und tritt gerade nicht mehr auf. Steigt
eine Zahl dagegen von Minute zu Minute weiter, passiert das Problem noch immer.

| Feld | Bedeutung |
|---|---|
| `ovf` | Der Empfangspuffer für die Sensor-Verbindung ist übergelaufen, Bytes gingen verloren. Passiert, wenn die Hauptschleife länger blockiert als der Puffer überbrücken kann, etwa 370 ms. |
| `crc` | Ein Datenpaket kam beschädigt an und wurde wegen falscher Prüfsumme verworfen. Jedes davon ist ein verlorener Messwert. |
| `frm` | Ein Paket war intakt, trug aber einen unplausiblen Zeitstempel, mehr als eine Sekunde vom vorherigen entfernt. Das folgt normalerweise auf einen Verlust: Der Empfänger verliert kurz den Anschluss an den Datenstrom und braucht etwa eine Sekunde, um sich neu zu synchronisieren – bei 50 Messungen pro Sekunde kostet das rund 50 Pakete. |
| `qfull` | Messwerte kamen schneller an, als die Hauptschleife sie verarbeiten konnte; die interne Warteschlange lief voll. |
| `sdfail` | Schreiben auf die SD-Karte ist fehlgeschlagen. |
| `maxloop` | Der längste einzelne Durchlauf der Hauptschleife in der letzten Minute, in Millisekunden. **Kein Fehler.** Wird jede Minute zurückgesetzt und beschreibt also die gerade vergangene Minute. |
| `heap` | Die kleinste Menge an freiem Speicher, die seit dem Einschalten gemessen wurde, in Byte. **Kein Fehler.** Wird nie zurückgesetzt, ist also der bisher schlechteste Wert. |
| `ntpfail` | Ein Versuch, die Uhrzeit aus dem Internet zu holen, ist fehlgeschlagen. |
| `reboots` | Die Uhr der Sensor-Platine ist zurückgesprungen, was normalerweise bedeutet, dass diese Platine neu gestartet ist. Die laufende Aufzeichnung wird beendet und in einer neuen Datei fortgesetzt. |

Was tun bei den einzelnen Feldern:
- **`ovf`**: Schauen Sie in derselben Zeile nach `maxloop`. Ist der Wert hoch, blockiert etwas die
  Hauptschleife. Ein einzelner Zähler kurz nach dem Einschalten, der danach nicht mehr steigt, ist
  harmlos.
- **`crc`**: Siehe den Absatz zu `crc` im Abschnitt [Setup](#setup) weiter oben – eine langsamere
  Verbindung (19200 Baud) lässt mehr zeitlichen Spielraum. Prüfen Sie außerdem, ob die Kabel
  zwischen den beiden Platinen kurz sind und nicht zusammen mit der SD-Karten-Verkabelung verlegt
  wurden.
- **`frm`**: Steigt meist zusammen mit `ovf` oder `crc` und braucht dann keine eigene Maßnahme.
  Steigt er dagegen alleine, deutet das auf einen Neustart der Sensor-Platine hin.
- **`qfull`**: Wie bei `ovf` – schauen Sie nach `maxloop`.
- **`sdfail`**: Die LED zeigt in diesem Fall zusätzlich das doppelte Blinken. Prüfen Sie, ob die
  SD-Karte voll, richtig eingesteckt und nicht defekt ist. Solange dieser Fehler auftritt, werden
  keine Messwerte gespeichert.
- **`maxloop`**: Keine Maßnahme nötig, solange der Wert nicht in die Nähe von 370 kommt – darüber
  hinaus kann der Empfangspuffer die Lücke nicht mehr überbrücken und `ovf` beginnt zu zählen.
- **`heap`**: Keine Maßnahme nötig, solange der Wert deutlich über etwa 8000 bleibt. Darunter kann
  die Weboberfläche instabil werden, besonders wenn mehrere Browser gleichzeitig verbunden sind.
- **`ntpfail`**: Nur relevant, wenn Sie absolute Zeitstempel möchten. Der Sensor misst trotzdem
  weiter und speichert Zeiten relativ zum Beginn der Aufzeichnung; die LED blinkt in diesem Fall
  schnell, um anzuzeigen, dass keine Uhrzeit vorliegt.
- **`reboots`**: Prüfen Sie Stromversorgung und Verkabelung der Sensor-Platine. Ein Vorkommen etwa
  alle 50 Tage ist zu erwarten und harmlos – dann läuft die interne Uhr des Sensors über, was von
  außen genauso aussieht.

## Messdateien und CSV-Export
Diese Angaben sind nur relevant, wenn Sie die Messdaten direkt (ohne die Weboberfläche) auswerten
möchten, z.B. mit einem eigenen Auswerteskript.

Jede Messdatei im Order `measurements/` beginnt mit einem 32 Byte großen Kopfbereich (u.a.
Formatversion, Abtastrate und `epoch0_ms` – der absoluten Startzeit der Datei in Millisekunden
seit dem 1.1.1970 (Unix-Zeit), oder 0, wenn beim Aufzeichnen keine Internet-Uhrzeit vorlag),
gefolgt von einer fortlaufenden Reihe von 8-Byte-Datensätzen: 4 Byte Zeitstempel in Millisekunden
relativ zu `epoch0_ms` (Ganzzahl) und 4 Byte Druck in Pascal (Fließkommazahl), jeweils
little-endian. Um aus einem Datensatz den absoluten Zeitpunkt zu berechnen, muss `epoch0_ms` aus
dem Kopfbereich zum Zeitstempel des Datensatzes addiert werden. Ältere Aufnahmen im alten Format
(nur Fließkommazahlen ohne Kopfbereich) kann der Sensor weiterhin lesen und zum Download anbieten.

Der Grund für diese Umstellung: Im alten Format standen nur die reinen Druckwerte hintereinander,
ganz ohne Zeitangabe. Ging dabei eine Messung verloren, stand davon nichts in der Datei – sie
wurde einfach etwas kürzer, und alle folgenden Werte rückten unbemerkt in der Zeit nach vorne. Weil
jeder Datensatz jetzt seinen eigenen Zeitstempel trägt, wird eine solche Lücke stattdessen in den
Messwerten und später auch in den Diagrammen sichtbar, statt die Zeitachse still zu verfälschen.
Das ist auch der Grund, warum ein Datensatz jetzt 8 statt 4 Byte braucht. Außerdem tragen die
Datenpakete auf der seriellen Verbindung zwischen den beiden Platinen (siehe „Setup" oben) jetzt
eine Prüfsumme; ein auf dem Weg beschädigtes Paket wird dadurch verworfen, statt als scheinbar
plausibler Messwert gespeichert zu werden.

Damit keine einzelne Datei zu groß wird, beginnt automatisch alle 24 Stunden eine neue Datei. Bei
50 Messungen pro Sekunde und 8 Byte pro Datensatz sind das etwa 34,6 MB pro Tag. Der Dateiname hat
die Form `<Basisname>_r<NNNN>_p<NN>`: `r` ist eine Laufnummer, die bei jedem Einschalten
hochgezählt wird und auch einen Stromausfall übersteht, damit nie eine bestehende Datei
überschrieben wird; `p` zählt die Teile eines Tages durch (auch ein Neustart der Platine oder ein
Wechsel in den Analysemodus beginnt einen neuen Teil).

Beim Herunterladen als CSV-Datei (im Analysemodus) enthält die Datei zwei Spalten: `time_ms` und
`pressure_pa`. Wenn der Sensor beim Aufzeichnen Internet hatte, ist `time_ms` die absolute Uhrzeit
in Millisekunden seit dem 1.1.1970 (Unix-Zeit); ohne Internet sind es Millisekunden seit Beginn
der Aufzeichnung.

Dabei hat jede Zeile der CSV-Datei exakt dieselbe Anzahl Zeichen. Das ist kein Zufall, sondern
notwendig: Der Sensor überträgt große Dateien in mehreren Teilen (siehe Hinweis zu großen Dateien
unten), und nur weil jede Zeile gleich lang ist, kann er nach einem Teil genau dort weitermachen,
wo der nächste beginnt. Aus demselben Grund lässt sich die Datei auch ab einer beliebigen Stelle
lesen, ohne sie von vorne an durchsuchen zu müssen.

Achtung bei großen Dateien: Eine volle Tagesaufzeichnung hat bei 50 Messungen pro Sekunde etwa 4,3
Millionen Zeilen und damit mehr Zeilen, als Excel darstellen kann (Excel-Grenze: ca. 1.048.576
Zeilen). Um eine Tagesaufzeichnung trotzdem in Excel zu öffnen, öffnen Sie die Datei auf der
Analyse-Seite über den Button „Analyse" (nicht „Download"), wählen Sie dort mit den beiden Reglern
unterhalb des Spektrogramms den gewünschten Zeitausschnitt aus und laden Sie nur diesen Ausschnitt
über den Button „Ausgewählten bereich als CSV herunterladen" herunter, statt die ganze Datei auf
einmal über den Download-Button in der Liste.

## Updates
Es gibt noch viele Möglichkeiten die Fähigkeiten des Sensors auszubauen.
Wir werden weiter an diesem Sensor basteln. Wenn es Verbesserungen gibt werden sie im Release-Tab hier auf GitHub eingespielt und hier beschrieben.

# Disclaimer
Dieser Sensor ist ein Bastelprojekt, basierend auf einem Kostengünstigen Differentialdrucksensor und kann kein professionelles Infraschallmikrofon ersetzen.
Die Software wird wie sie ist zur Verfügung gestellt ohne irgendwelche Garantien oder Haftungsübernamen.

# Credits
Das Projekt basiert auf den Arbeiten von [Stephan Holzheu](https://www.bayceer.uni-bayreuth.de/infraschall/), der sich als einer der ersten intensiv mit Infraschall von Windkraftanlagen beschäftigt hat.  Seine Untersuchungen konnten die Fehler der vielbeachteten Infraschallstudie des BGR (Bundesamt für Geologie und Rohstoffe) nachwiesen.  Er hatte auch die Idee zu einem DIY-Sensorbau mit einem SDP-600-25.

## Aliasing
Der Sensor tastet alle 20 ms den Druckunterschied zwischen in der Box und außerhalb der Box ab und kann so Infraschall Signale bis zu 25 Hz recht genau messen. Allerdings zeichnet er dabei auch Druckschwingungen (Schall) auf, mit weit höheren Frequenzen als 25 Hz auf.  
Dieser hörbare Schall wird von dem Sensor also auch als Infraschall erfasst und analysiert. So wird Schall im hörbaren Bereich auch als tieffrequenter Infraschall wahrgenommen. Z.B. wird ein 30Hz Ton als 20Hz Infraschall aufgenommen.
Dieses Phänomen nennt sich [Aliasing-Effekt](https://de.wikipedia.org/wiki/Alias-Effekt). Mit unserem Sensor lässt sich das nicht verhindern, was auch der Grund ist, weshalb professionelle Infraschallmikrofone sehr viel aufwändiger und teurer sind.
Allerdings wird unser Sensor den Infraschall niemals unterschätzen sondern immer __überschätzen__.

