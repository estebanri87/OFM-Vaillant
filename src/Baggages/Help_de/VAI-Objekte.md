### Objekte

Jedes Objekt der Anlage lässt sich einzeln zuschalten. Nicht benötigte Objekte abwählen hält
die Objektliste kurz. Für „Anlage online", „Störung, Wartung und Fehlercode" und „Aktuelle
Leistung" entfällt dann auch die zugehörige Cloud-Abfrage.

Anlage:

* **Anlage online**: das Internet-Gateway ist mit der Cloud verbunden
* **Außentemperatur** und **Außentemperatur 24-h-Mittel** (DPT 9.001)
* **Anlagendruck** in Pa (DPT 9.006; 1 bar = 100000 Pa)
* **Energiemanager-Zustand**: Text der Anlage, z. B. „HEATING", „STANDBY"
* **Störung**, **Wartung** (DPT 1.005) und **Fehlercode** als Text
* **Abwesenheit** schalten und **Status Abwesenheit**: schaltet den Abwesenheitsmodus der App
  für ein Jahr ein bzw. beendet ihn
* **Aktuelle Leistung** (DPT 14.056), nur bei Anlagen, die das melden

Diagnose:

* **Cloud-Verbindung**: 1 = angemeldet und die letzte Abfrage war erfolgreich
* **Diagnose-Meldungstext**: kurzer Text, z. B. „verbunden", „Anmeldung...", „Login Fehler",
  „Netzwerkfehler", „Befehl Fehler"

