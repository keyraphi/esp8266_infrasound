# Infraschall Sensor

## Hardware

### Bauteile
- D1 Mini
- D1 Mini Pro
- SD-Card-Reader
- SDP ...

### Verdrahtung

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
geht bequem in der Arudino IDE mit dem Library Manager (v1: im Menü unter
Tools, v2: rechts in der Leiste). Folgende Bibliotheken müssen installiert
werden:
    - NPTClient 
    - ArduinoJson 
    - ArduinoUniqueId 
    - AsyncTCP
    - ESP8266TimerInterrupt
    - ESP8266TimerInterrupt
    - ESPAsyncTCP
    - ESPAsyncWebserver
    - TimerInterrupt

### Sensor
Bevor die D1 Mini Platine programmiert werden kann muss ein Arduino Projekt
angelegt werden. In Arduino-Slang spricht man von einem "Sketch". Alle Arduino
sketches leben in einem festgelegten Ordner:

 * Windows: `C:\Users\{username}\Documents\Arduino`
 * macOS: `/Users/{username}/Documents/Arduino`
 * Linux: `/home/{username}/Arduino`

Kopieren Sie den `arduino_infrasound_sensor` Ordner dort hin. Unter Windows und
Mac müssen Sie evtl. die Datei `arduino_infrasound_sensor.ino.cpp` umbenennen
zu `arduino_infrasound_sensor.ino`, und den existierenden Link damit ersetzen.
Ein Arduino Sketch braucht immer die Endung `.ino`.

Jetzt kann die Platine programmiert werden:
- `arduino_infrasound_sensor.ino` Sketch in der Arduino IDE öffnen.
- USB Kabel mit dem D1 Mini verbinden, der an den Sensor gelötet ist.
- _Generic ESP8266 Module_ als Board auswählen (v1: Tools->Board, v2:
SelectBoard->Boards) und USB ausgang wählen (v1: Tools->Port, v2:
SelectBoard->Ports).
- Board Programmieren. Dies geht mit dem Upload Button in der IDE (Pfeil nach Rechts).

Fertig.

### Webserver
Beim Webserver funktioniert es genau gleich. 
Kopieren Sie erst den `esp8266_infrasound_webserver` Ordner in ihren Sketch Ordner und benennen Sie ggf. `esp8266_infrasound_webserver.ino.cpp` zu `esp8266_infrasound_webserver.ino` um. 

Programmieren Sie den zweiten D1 Mini:
- `esp8266_infrasound_webserver.ino` Sketch in der Arduino IDE öffnen.
- USB kabel mit dem Board verbinden.
- _Generic ESP8266 Module_ als Board auswählen und Port setzen
- Board programmieren durch den Upload button.

Fertig.

### Statische Dateien
Die Software auf den Microcontroller ist recht überschaubar. Im Endeffekt werden nur die Messwerte auf die SD Karte geschrieben und unter Umständen durch einen Websocket an die Clients geschickt.
Alles weitere passiert dann auf der Webseite und wird direkt beim nutzer im Browser berechnet und dargestellt.
Dieses Web-Programm befindet sich im Ordner `static`. Dieser Ordner muss vollständig auf die SD-Karte geschrieben werden:
- Erstellen Sie einen Ordner `www` direkt auf der SD Karte.
- Kopieren sie den `static` Ordner in diesen neuen `www` Ordner.

Fertig

## Sensor betreiben
Der Sensor hat drei Betriebsmodi:
1. Mess Modus: Für Langzeitmessungen draußen.
2. Live view Modus: Für Live Demonstrationen - stabile Internetverbindung ist Notwendig!
3. Analyse Modus: Zur Analyse von zuvor aufgenommenen Messungen - Internetverbindung ist Notwendig!

### 1. Mess Modus
Sobald der Sensor mit strom versorgt ist (die Webserver Platine per USB-Kabel Strom bekommt) beginnt der Sensor sofort mit einer Messung. Die Messwerte werden in eine Datei `messurements/` TODO


## Updates

