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
* Unsere Programme verwenden einige Bibliotheken, die standardmäßig nicht zur
Verfügung stehen und erst in der Arduino IDE installiert werden müssen. Das
geht bequem in der Arudino IDE mit dem Library Manager (v1: im Menü under
Tools, v2: rechts in der Leiste). Folgende Bibliotheken müssen installiert
werden:
    - NPTClient
    - ArduinoJson
    - ArduinoUniqueId
    - AsyncTCP
    - ESP8266TimerInterrupt
    - ESPAsyncTCP
    - ESPAsyncWebserver (ACHTUNG: von lacamera, getestet mit version 3.1.0)
    - TimerInterrupt

### Sensor
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
Beim Webserver funktioniert es genau gleich.
Kopieren Sie erst den `esp8266_infrasound_webserver` Order in ihren Sketch Order und benennen Sie ggf. `esp8266_infrasound_webserver.ino.cpp` zu `esp8266_infrasound_webserver.ino` um.

Programmieren Sie den zweiten D1 Mini:
- `esp8266_infrasound_webserver.ino` Sketch in der Arduino IDE öffnen.
- USB kabel mit dem Board verbinden.
- _Generic ESP8266 Module_ also Board auswählen und Port setzen
- Board programmieren durch den Upload button.

Fertig.

### Statische Dateien
Die Software auf den Microcontroller ist recht überschaubar. Im Endeffekt werden nur die Messwerte auf die SD Karte geschrieben und under Umständen durch einen Websocket an die Clients geschickt.
Alles weitere passiert dann auf der Webseite und wird direkt beim Nutzer im Browser berechnet und dargestellt.
Dieses Web-Programm befindet sich im Order `static`. Dieser Order muss vollständig auf die SD-Karte geschrieben werden:
- Erstellen Sie einen Order `www` direkt auf der SD Karte.
- Kopieren sie den `static` Order in diesen neuen `www` Order.

Fertig

## Sensor betreiben
Der Sensor hat drei Betriebsmodi:
1. Messmodus: Für Langzeitmessungen draußen.
2. Live-view-Modus: Für Live Demonstrationen - stabile Internetverbindung ist Notwendig!
3. Analysemodus: Zur Analyse von zuvor aufgenommenen Messungen - Internetverbindung ist Notwendig!

### 1. Messmodus
Sobald der Sensor mit strom versorgt ist (die Webserver Platine per USB-Kabel Strom bekommt) beginnt der Sensor sofort mit einer Messung.
Die Messwerte werden binär also 32-Bit float in eine Datei geschrieben.
Diese Datei wird in den Order `messurements/` geschrieben.
Wenn der Sensor Internet hat ist der Name der Datei der Zeitpunkt der Aktivierung im format `YYYY-MM-DD hh:mm:ss-ms`.
Ohne Internet wird die Datei `unbekannt` benannt.
Falls eine datei bereits existiert wird eine aufsteigende Zahl angehängt. Z.B.
wenn `messurements/unbekannt` bereits existiert wird die Datei
`messurements/unbekannt_0` genannt.

Der Messmodus läuft solange bis der Sensor ausgesteckt wird oder ein neues WLAN eingestellt wird oder in den Analysemodus gewechselt wird.

### 2. Live-View-Modus
Der Live-View-Modus ist genau gleich wie der Messmodus, nur, dass sich jemand übers W-LAN mit dem Sensor verbunden hat.
In diesem Modus überträgt der Sensor in Echtzeit sämtliche Messwerte an das verbundene Gerät, wo sie dann grafisch aufbereitet dargestellt werden.

Um sich mit dem Sensor zu verbinden ist eine __stabile__ WiFi Verbindung notwendig, welche wie folgt eingerichtet werden kann:
1. In der AduinoIDE den SerialMonitor öffnen(TODO ... das muss ich im code ändern ... die aktuelle lösung ist blöd.)
2. Sensor mit Computer verbinden.
3. Sobald im Serial Monitor die Nachricht "SSID Eingeben" erscheint kann oben die SSID (der Name) ihres WLAN Netzes eingegeben werden und mit senden bestätigen.
4. Sobald im Serial Monitor die Nachricht "Passwort Eingeben" erscheint kann das WLAN passwort eingegeben werden und absenden.
5. Der Sensor started sich neu und versucht sich in das WLAN einzuwählen. Falls es nicht funktioniert geht es zurück zu Schritt 3.
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
Dadurch wird die aktuelle Messreihe auf dem Sensor gestoppt, damit er die gespeicherten Daten in der vollen Geschwindigkeit zur Verfügung stellen kann ohne immer wieder neue Messungen aufzeichnen zu müssen.
Hier wird eine Liste aller Messdateien dargestellt, die auf dem Sensor gespeichert sind.
Jede Messung kann entweder heruntergeladen werden (als .raw Datei) oder direkt im Browser analysiert werden.
Für die Analyse stehen zwei Darstellungen bereit.
1. Der G-Bewerteter Dauerschallpegel über die Zeit dargestellt. Hier lässt sich
   untersuchen wie die Lautstärke des Signals sich während der Messung entwickelt hat. In dem selben Diagramm sind 95 dB(G) als Menschliche Wahrnehmungsschwelle fest eingezeichnet. 
2. Das Spektrogram der gesamten Messung. Je länger die Messung ging, desto komprimierter wird es dargestellt. Hier die Lautstärke der einzelnen Frequenzen über den Verlauf der Messung farblich kodiert dargestellt. Die Auflösung der Frequenzen kann mit einem Regler ausgewählt werden.

## Updates
Es gibt noch viele Möglichkeiten die Fähigkeiten des Sensors auszubauen.
Wir werden weiter an diesem Sensor basteln. Wenn es Verbesserungen gibt werden sie im Release-Tab hier auf GitHub eingespielt und hier beschrieben.

# Disclaimer
Dieser Sensor ist ein Bastelprojekt, basierend auf einem Kostengünstigen Differentialdrucksensor und kann kein professionelles Infraschallmikrofon ersetzen.
Die Software wird wie sie ist zur Verfügung gestellt ohne irgendwelche Garantien oder Haftungsübernamen.

## Aliasing
Der Sensor tastet alle 20 ms den Druckunterschied zwischen in der Box und außerhalb der Box ab und kann so Infraschall Signale bis zu 25 Hz recht genau messen. Allerdings zeichnet er dabei auch Druckschwingungen (Schall) auf, mit weit höheren Frequenzen als 25 Hz auf.  
Dieser hörbare Schall wird von dem Sensor also auch als Infraschall erfasst und analysiert. So wird Schall im hörbaren Bereich auch als tieffrequenter Infraschall wahrgenommen. Z.B. wird ein 30Hz Ton als 20Hz Infraschall aufgenommen.
Dieses Phänomen nennt sich [Aliasing-Effekt](https://de.wikipedia.org/wiki/Alias-Effekt). Mit unserem Sensor lässt sich das nicht verhindern, was auch der Grund ist, weshalb professionelle Infraschallmikrofone sehr viel aufwändiger und teurer sind.
Allerdings wird unser Sensor den Infraschall niemals unterschätzen sondern immer __überschätzen__.

