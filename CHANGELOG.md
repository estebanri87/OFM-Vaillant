# Changelog OFM-Vaillant

## 0.1.0 - 2026-10-03

Erste Fassung. Anbindung von Vaillant-Anlagen mit sensoCOMFORT VRC720 und VR921 über die
myVAILLANT-Cloud, nach dem Vorbild von myPyllant / mypyllant-component.

### Added
- Anmeldung wie die myVAILLANT-App (Keycloak, PKCE, ALTCHA-Rechennachweis), Erneuerungs-Token
  im NVS, wachsende Wartezeiten nach Fehlern
- Anlage: Außentemperatur, Anlagendruck, Energiemanager-Zustand, Störung/Wartung/Fehlercode,
  Abwesenheit, aktuelle Leistung, Cloud- und Gateway-Status
- Kanäle nach OpenKNX-Kanalauswahl (Typ-Variante): Heizzone, Warmwasser, Energiedaten
- Heizzone: Raumtemperatur/-feuchte, Sollwert, Heizzustand, Betriebsart, Quick-Veto,
  Manuell- und Absenktemperatur, Vorlauf, Heizkurve
- Warmwasser: Temperatur, Sollwert, Betriebsart, Boost
- Energiedaten des Tages: Strom, Umweltenergie, Wärme, Arbeitszahl
- Konsole: `vai`, `vai poll`, `vai login`, `vai raw on|off`

### Changed
- KO-Namen und Objektfunktionen einheitlich nach dem Schema „Vaillant [Kanal]: Eingang/Ausgang, Wert“.

