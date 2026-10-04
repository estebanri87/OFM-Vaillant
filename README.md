# OFM-Vaillant

OpenKNX Modul zur Anbindung von Vaillant-Wärmepumpen und -Heizungen über die
**myVAILLANT-Cloud** (sensoCOMFORT VRC720 mit Gateway VR921, „tli"-API).

Das Modul liest Temperaturen, Zustände, Störungen und Energiedaten der Anlage und kann
Betriebsarten, Sollwerte, Warmwasser und die Abwesenheit schalten — dieselben Funktionen, die
auch die myVAILLANT-App nutzt.

## Features

- Objekte der Anlage (einzeln zuschaltbar):
  - Anlage online, Außentemperatur und 24-h-Mittel (DPT 9.001), Anlagendruck (DPT 9.006)
  - Energiemanager-Zustand, Störung und Wartung (DPT 1.005), Fehlercode als Text
  - Abwesenheit schalten und Status, aktuelle Leistung (DPT 14.056)
  - Diagnose: Cloud-Verbindung und Meldungstext
- Kanäle, je Kanal wahlweise:
  - **Heizzone** – Raumtemperatur und -feuchte, wirksame Solltemperatur, Heizzustand,
    Betriebsart (DPT 5.010), Quick-Veto, Solltemperatur Manuell, Absenktemperatur,
    Vorlauftemperatur Ist/Soll, Heizkreiszustand, Heizkurve
  - **Warmwasser** – Ist-Temperatur, Solltemperatur, Betriebsart (DPT 5.010), Boost
  - **Energiedaten** – Stromverbrauch, Umweltenergie, erzeugte Wärme und Arbeitszahl des
    laufenden Tages (DPT 13.010), je Gerät und Betriebsart
- Konfigurierbare Abfrageintervalle für Anlagenzustand und Energiedaten
- Konsolenbefehle `vai`, `vai poll`, `vai login`, `vai raw on`

## Voraussetzungen

- myVAILLANT-Konto (E-Mail und Passwort der App) mit registrierter Anlage
- Internetzugang des KNX-Geräts
- Gerät mit **ESP32 und PSRAM** (z. B. REG1-LAN-TP-Base). Die Anmeldung braucht einen
  ALTCHA-Rechennachweis (PBKDF2-SHA256) und TLS zur Vaillant-Cloud, dafür werden
  Hardware-SHA und PSRAM für die TLS-Puffer benötigt. Auf RP2040-Geräten ist das Modul nicht
  enthalten.

Die myVAILLANT-API ist nicht offiziell dokumentiert; Vaillant kann sie jederzeit ändern.
Vorbild sind [myPyllant](https://github.com/signalkraft/myPyllant) und
[mypyllant-component](https://github.com/signalkraft/mypyllant-component) für Home Assistant.

## Dokumentation

- [Applikationsbeschreibung](doc/Applikationsbeschreibung-Vaillant.md) – zugleich Quelle der
  ETS-Hilfetexte (`src/Baggages/Help_de`, erzeugt vom VS-Code-Task
  *OpenKNXproducer Documentation*)
- [CHANGELOG](CHANGELOG.md)

## Lizenz

GPL-3.0 – siehe [LICENSE](LICENSE)
