<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 OpenKNX -->

<!-- KEINE MARKDOWN-TABELLEN in den DOC-Bloecken: die ETS zeigt sie als rohe Pipe-Zeichen an.
     Aufzaehlungen verwenden. Die Bloecke zwischen DOC und DOCEND werden von
     "openknxproducer baggages" (VS-Code-Task "OpenKNXproducer Documentation") zu den
     Hilfetexten in src/Baggages/Help_de/ verarbeitet. -->

# Applikationsbeschreibung Vaillant

Das Modul bindet eine **Vaillant-Wärmepumpe oder -Heizung** über die **myVAILLANT-Cloud** an
KNX an. Es liest Temperaturen, Zustände, Störungen und Energiedaten und kann Betriebsarten,
Sollwerte, Warmwasser und die Abwesenheit schalten — dieselben Funktionen, die auch die
myVAILLANT-App und die Home-Assistant-Integration *mypyllant* nutzen.

Voraussetzungen:

* Regler **sensoCOMFORT VRC720** mit Internet-Gateway **VR921** (in der App „myVAILLANT")
* ein myVAILLANT-Konto mit registrierter Anlage
* Internetzugang des KNX-Geräts
* ein Gerät mit **ESP32 und PSRAM** (z. B. REG1-LAN-TP-Base). Auf RP2040-Geräten ist das
  Modul nicht enthalten.

Die myVAILLANT-API ist nicht offiziell dokumentiert. Vaillant kann sie jederzeit ändern; dann
funktioniert das Modul erst nach einem Update wieder.

## Wichtige Hinweise

* Diese KNXprod wird nicht von der KNX Association offiziell unterstützt!
* Die Erzeugung der KNXprod geschieht auf eure eigene Verantwortung!

# Allgemein

<!-- DOC -->
## Allgemein

Das Modul ist standardmäßig aktiv. Wird es nicht gebraucht, lässt es sich unter
**OpenKNX → Module** abschalten; dann blendet die ETS seine Seiten aus und die Firmware meldet
sich nicht bei der Cloud an.

Diese Seite enthält die Zugangsdaten, die Abfrageeinstellungen und die Objekte der Anlage. Die
Kanäle werden auf der Seite **Kanalauswahl** angelegt.

Werte, die die ganze Anlage betreffen — Außentemperatur, Anlagendruck, Störungen, Abwesenheit,
aktuelle Leistung — liegen auf Objekten dieser Seite. Heizzonen, Warmwasser und Energiedaten
werden als **Kanäle** angelegt.

Alle Statusobjekte senden nur bei Änderung. Die Werte kommen aus der Cloud, die die Anlage
etwa minütlich aktualisiert; sie eignen sich zur Anzeige und für Logik, nicht für schnelle
Regelungen.

<!-- DOCEND -->

<!-- DOC -->
## Zugangsdaten

**E-Mail-Adresse** und **Passwort** sind die Anmeldedaten der myVAILLANT-App. **Land des
myVAILLANT-Kontos** ist das Land, das bei der Registrierung gewählt wurde; es bestimmt den
Anmeldeserver. Stimmt es nicht, schlägt die Anmeldung fehl.

**Anlage** wählt bei mehreren Anlagen im Konto die gewünschte aus, in der Reihenfolge der App.

Die Anmeldung verlangt einen Rechennachweis (ALTCHA) und dauert beim ersten Mal **bis zu einer
Minute**. Danach merkt sich das Gerät ein Erneuerungs-Token und braucht nach einem Neustart
keine vollständige Anmeldung mehr. Wird die E-Mail-Adresse geändert, meldet es sich neu an.

Nach fehlgeschlagenen Anmeldungen wartet das Modul zunehmend länger (1, 5, 15, 60 Minuten),
damit das Konto nicht gesperrt wird.

Das Passwort steht im Klartext im ETS-Projekt.

<!-- DOCEND -->

<!-- DOC -->
## Abfrage

**Anlagenzustand abfragen alle** legt fest, wie oft Temperaturen, Betriebsarten und Sollwerte
geholt werden (30 bis 3600 Sekunden, Standard 60). Die Anlage selbst meldet ihre Werte nur
etwa jede Minute an die Cloud; kürzere Abstände bringen keine frischeren Werte, erhöhen aber
die Last und das Risiko, von Vaillant gedrosselt zu werden.

**Energiedaten, Störungen und Leistung abfragen alle** betrifft die selteneren Abfragen
(5 bis 240 Minuten, Standard 15).

**Dauer einer Schnellabsenkung/-anhebung (Quick-Veto)** gilt für das Objekt „Quick-Veto" der
Heizzonen (1 bis 24 Stunden, Standard 3).

Nach einem Schreibbefehl wird der Zustand nach etwa 5 Sekunden neu abgefragt, damit die
Statusobjekte die Änderung zeigen.

<!-- DOCEND -->

<!-- DOC -->
## Objekte

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

<!-- DOCEND -->

Auf der Konsole zeigt `vai` den Zustand, `vai poll` fragt sofort ab, `vai login` verwirft das
gespeicherte Token und meldet neu an, `vai raw on` protokolliert jede Anfrage.

# Kanäle

<!-- DOC -->
## Kanaltyp

Ein Kanal bildet entweder eine **Heizzone**, einen **Warmwasserspeicher** oder einen Satz
**Energiedaten** ab. Kanäle werden auf der Seite **Kanalauswahl** aktiviert, indem dort der
Kanaltyp gewählt wird; „Deaktiviert" entfernt den Kanal. Auf der Seite des Kanals lässt sich
der Typ wechseln, aber nicht deaktivieren.

Mehrere Kanäle dürfen auf dieselbe Zone oder denselben Speicher zeigen, etwa für
unterschiedliche Energiedaten.

<!-- DOCEND -->

<!-- DOC -->
## Heizzone

**Zone** wählt die Zone der Anlage, so wie sie im Regler nummeriert ist (Zone 1 = erste Zone).

Objekte:

* **Raumtemperatur Ist**, **Raumfeuchte Ist**: nur mit Raumfühler bzw. Fernbedienung
* **Solltemperatur aktuell wirksam**: was der Regler gerade anstrebt
* **Heizzustand**: Text der Anlage, z. B. „IDLE", „HEATING_UP"
* **Betriebsart** setzen und Status (DPT 5.010): 0 = Aus, 1 = Manuell, 2 = Zeitprogramm
* **Quick-Veto Solltemperatur**: hebt oder senkt die Solltemperatur für die eingestellte
  Dauer; 0 beendet das Quick-Veto. **Quick-Veto aktiv** zeigt, ob eines läuft.
* **Solltemperatur Manuell** setzen und Status: gilt in der Betriebsart Manuell
* **Absenktemperatur** setzen und Status: gilt im Zeitprogramm außerhalb der Zeitfenster
* **Vorlauftemperatur Ist / Soll**, **Heizkreis Zustand**: aus dem Heizkreis der Zone
* **Heizkurve** setzen und Status (DPT 14)

<!-- DOCEND -->

<!-- DOC -->
## Warmwasser

**Warmwasserspeicher** wählt den Speicher, meist gibt es nur einen.

Objekte:

* **Warmwassertemperatur Ist**
* **Warmwasser-Solltemperatur** setzen und Status. Der Regler nimmt nur ganze Grad; der Wert
  wird gerundet und auf den zulässigen Bereich der Anlage begrenzt (meist 35 bis 70 °C).
* **Warmwasser-Betriebsart** setzen und Status (DPT 5.010): 0 = Aus, 1 = Manuell,
  2 = Zeitprogramm
* **Warmwasser-Boost** Ein/Aus und Status: einmalige Speicherladung

<!-- DOCEND -->

<!-- DOC -->
## Energiedaten

Liefert die Tageswerte seit Mitternacht (Ortszeit des Geräts) in Wh.

**Gerät** wählt die Wärmepumpe bzw. den Wärmeerzeuger oder die elektrische Zusatzheizung.
**Betriebsart** wählt Heizen, Warmwasser, Kühlen oder die Summe aller drei.

Objekte:

* **Stromverbrauch heute** (DPT 13.010)
* **Umweltenergie heute**: aus Luft oder Erdreich gewonnene Energie
* **Erzeugte Wärme heute**
* **Arbeitszahl heute**: erzeugte Wärme geteilt durch Stromverbrauch

Welche Werte es gibt, hängt von der Anlage ab; fehlende Werte werden nicht gesendet. Vaillant
aktualisiert die Energiedaten mit Verzögerung, die Werte des laufenden Tages hinken daher
nach.

<!-- DOCEND -->
