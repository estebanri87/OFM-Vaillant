### Abfrage

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

