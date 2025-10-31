# pferdusche
Dusche für Pferde




Was macht das?

Also, die Pferdedusche ist eine Software, die dafür sorgt, dass Pferde sich ihre eigene Dusche anschalten köännen. Für den Sommer also ideal, damit die Vierbeiner selbst entscheiden können, ob sie eine Abkühlung brauchen. Technisch ist es so, dass die Tiere einen Taster drücken, die Software dann ein Magnetventil steuert und dann für eine gewisse Zeit Wasser fließen kann.  In der Software kann man einstellen, wie lange das Magnetventil offen sein soll (pro Zyklus), ob es eine Sperrzeit geben soll (zwischen zwei Auslösungen) und wie lange das Ventil am Tag generell geöffnet werden darf. Das ganze ist über eine Website (Lokal oder im WiFi einsehbar, keine Cloud) einstellbar. Natürlich kann man da auch das Ventil manuell auslösen. Auslösen als auch die Sperre zwischen zwei Auslösungen wird optisch über LED´s angezeigt. Die Helligkeit der LED´s ist per Software einstellbar.


Features:
- "Wasser an" über Grobhandtaster oder per Software
- Reset des Tageswasserverbrauches über Software-Schalter, Taster am Gerät oder Automatisch um 24 Uhr Nachts
- Vorgabe von:
  - Dauer einer Auslösung
  - Zeit zwischen zwei Auslösungen
  - Gesamtauslösezeit
- WiFi
  - AP-Mode (Access Point, lokaler Zugang zum Gerät)
  - Client Mode (Anmelden im lokalen WLAN Netzwerk)
- Update über Webseite möglich
- MQTT Anbidung möglich


Was braucht man?
- ESP32
- Grobhandtaster
- SolidState Realais
- 12V Magnetventil
- 6 Stück WS2812B LED´s
- Kabel
- Spannungsregler, z.B. von 12V auf 5V
- Spannungsversorgung (z.B. PV Modul, Autobatterie, Solarladeregler)
- Steckverbindungen
- Evtl. Gehäuse


Wie geht das ?

1. ESP32 z.B. mit Arduino IDE programmieren
2. Relais u LED´s am ESP anschließen
3. Alles in ein Gehäude bauen
4. Magnetventil u Wasser anschließen
5. fertig.

