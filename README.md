# PartSplice 3D

PartSplice 3D ist eine Windows-Anwendung zum Aufteilen von STL-, 3MF- und STEP-Modellen und zum Erzeugen passender 3D-Druck-Verbinder entlang gerader oder mehrpunktiger Schnittlinien.

## Funktionen

- STL-, 3MF-, STEP- und STP-Modelle laden und prüfen
- STEP-CAD-Körper beim Import in bearbeitbare, millimetergenaue Dreiecksmodelle umwandeln
- mehrere Objekte und Farb-/Filamentzuweisungen aus einer 3MF-Datei bearbeiten
- Benutzeroberfläche jederzeit zwischen Deutsch und Englisch umschalten
- gerade Schnitte und Mehrpunkt-Schnitte anlegen
- Verbinder einzeln platzieren, verschieben und konfigurieren
- Verbinder eines bereits ausgeführten Schnitts gesammelt verschieben und erst mit „Neu schneiden“ einmalig neu berechnen
- neue Verbinder intelligent in freie, geprüfte Bereiche einsetzen, ohne bestehende Positionen zu verändern
- Verbinder auf Materialabdeckung, Überlappung, Wandreserve und Konflikte mit früheren Schnitten prüfen
- fortlaufende, frei positionierbare Schnittnummern in beide zusammengehörigen Schnittteile gravieren
- Schnittpunkte am Raster, an Winkelschritten und Modellkanten einrasten sowie numerisch positionieren
- parametrierbare Einführfasen an Zapfen und Nut zum leichteren Zusammenstecken
- mehrere Schnitte nacheinander anwenden und erneut bearbeiten
- geschnittene Teile als einzelne STL-Dateien oder gemeinsame 3MF-Datei exportieren
- Referenz-Druckbetten verschiedener Drucker anzeigen
- Verbinder-Testmuster mit wählbarem Bambu-Druckerprofil zur Ermittlung eines geeigneten Spiels erzeugen
- Projekte speichern und später weiterbearbeiten
- neue Versionen über die offizielle GitHub-Release-Seite prüfen und herunterladen
- fehlerhafte Volumenmodelle mit dem Windows-3D-Druckdienst prüfen und reparieren

## Unterstützte Plattform

Die Anwendung ist derzeit für 64-Bit-Windows 10 und Windows 11 vorgesehen. Die Modellreparatur verwendet Windows-spezifische 3D-Druck-Schnittstellen.

## Voraussetzungen zum Erstellen

- Visual Studio 2022 oder Visual Studio 2022 Build Tools
- Workload **Desktopentwicklung mit C++**
- MSVC-v143-Compiler und ein aktuelles Windows SDK
- CMake 3.24 oder neuer
- Git
- vcpkg (in Visual Studio 2022 enthalten oder separat installiert)
- Internetzugang beim ersten Konfigurieren, da CMake die Abhängigkeiten aus ihren offiziellen Repositories lädt

Fertige Binärdateien sind nicht Bestandteil des Quellcodes. Veröffentlichte
Versionen werden auf der GitHub-Release-Seite als vollständiges portables
Windows-Paket angeboten. Eine einzeln kopierte `PartSplice3D.exe` ist kein
vollständiges Paket, weil Schnittberechnung und STEP-Import den danebenliegenden
`PartSpliceWorker.exe` sowie dessen Laufzeitdateien benötigen.

## Einfach unter Windows erstellen

Nach der Installation der oben genannten Voraussetzungen genügt ein
Doppelklick auf `build_windows.bat`. Alternativ kann das Skript in PowerShell
oder der Eingabeaufforderung gestartet werden:

```powershell
.\build_windows.bat
```

Das Skript sucht CMake, Git und vcpkg automatisch in den üblichen
Visual-Studio-Installationspfaden, konfiguriert einen 64-Bit-Release-Build und
erstellt `build\Release\PartSplice3D.exe`. Vorhandene Build-Dateien werden
weiterverwendet und nicht automatisch gelöscht.

### Optional: Windows-EXE signieren

Mit einem eigenen Codesignaturzertifikat unter `Cert:\CurrentUser\My` kann der
Build automatisch signiert werden. Dazu vor dem Build dessen SHA-1-Fingerabdruck
setzen:

```powershell
$env:PARTSPLICE_SIGN_CERT_SHA1 = "0123456789ABCDEF0123456789ABCDEF01234567"
.\build_windows.bat
```

Das Skript signiert Hauptanwendung und Geometrie-Worker mit SHA-256, fordert
HTTPS-Zeitstempel an und prüft beide Signaturen. Das Repository enthält weder
Zertifikate noch private Schlüssel. Ohne die Variable bleibt der lokale Build
unsigniert.

## Manuell mit CMake erstellen

Die folgenden Befehle in einer **Developer PowerShell for VS 2022** ausführen:

```powershell
git clone https://github.com/mpl1337/PartSplice3D.git
cd PartSplice3D
$vcpkg = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release --target PartSplice3D
```

Die fertige Anwendung befindet sich danach hier:

```text
build\Release\PartSplice3D.exe
build\Release\PartSpliceWorker.exe
```

Beide Dateien und die von vcpkg bereitgestellten Open-CASCADE-DLLs gehören
zusammen. Der Worker kapselt STEP-Import und Schnittberechnung, damit ein Fehler
im Geometriekern nicht die Benutzeroberfläche beendet. Unter Windows wird
Open CASCADE ausschließlich vom Worker geladen und nicht von der Oberfläche.

Start aus PowerShell:

```powershell
.\build\Release\PartSplice3D.exe
```

Ein vollständiges lokales Veröffentlichungsverzeichnis und das gleichnamige
ZIP-Archiv mit Hauptprogramm, Worker, Open-CASCADE-Laufzeitdateien, Lizenzen und
SHA-256-Prüfsummen werden so erzeugt:

```powershell
.\package_windows.ps1 -BuildDir .\build\Release -Force
```

Das Skript übernimmt ausschließlich Laufzeitdateien; Bibliotheken, PDBs,
Testprogramme und lokale Modelle gelangen weder in den Ordner noch in das ZIP.

## Abhängigkeiten

Beim Konfigurieren werden GLFW, Dear ImGui, miniz, TinyXML-2, Manifold und Clipper2 in festgelegten Versionen geladen. Der STEP-Import verwendet Open CASCADE Technology über das mitgelieferte `vcpkg.json`-Manifest. Die Oberfläche enthält außerdem die Schrift DejaVu Sans. Die vollständigen Bedingungen stehen in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) und im Ordner [third_party_licenses](third_party_licenses).

Quellversion, vcpkg-Port, Austauschbarkeit und dauerhaftes Quellcodeangebot der
mit dem Windows-Paket ausgelieferten Open-CASCADE-DLLs sind zusätzlich in
[OPEN_CASCADE_SOURCE_OFFER.md](OPEN_CASCADE_SOURCE_OFFER.md) dokumentiert.

STEP/STP wird beim Import trianguliert und anschließend mit demselben robusten Mesh-Workflow wie STL und 3MF bearbeitet. Der Export erfolgt als STL oder 3MF; ein parametrischer STEP-Export ist nicht Bestandteil dieser Version.

## Mehrfarbige 3MF-Dateien und Formatgrenzen

PartSplice 3D übernimmt eine variable Anzahl von Bambu-Filamentslots auf Objekt- und Teil-Ebene
(nicht fest auf vier begrenzt; Bambu-Flächenbemalung wird für bis zu 32 Slots dekodiert),
einfache Bambu-Flächenbemalungen sowie standardisierte 3MF-Basismaterial- und
Farbgruppen. Die Zuweisung wird pro Dreieck gespeichert, durch Schnitte und
boolesche Operationen weitergeführt und sowohl als Standard-3MF-Farbgruppe als
auch als Bambu-kompatible Filament-/Flächenzuweisung exportiert. Neu erzeugte
Schnitt- und Verbinderflächen verwenden den Grund-Filamentslot des jeweiligen
Ausgangsteils. Bei mehreren Farben zeigt die Modellansicht die tatsächlichen
Materialfarben; Projektdateien speichern Palette und Flächenzuordnung mit.
Neu entstehende Schnitt-, Nut- und Verbinderflächen erben den Filamentslot der
jeweils nächstgelegenen lokalen Quellfläche, statt pauschal die Objektgrundfarbe
zu verwenden.

STL besitzt keine standardisierte Farb-, Filament-, Einheiten- oder
Objektstruktur. Ein Mehrfarben-Export als STL warnt deshalb vor dem
unvermeidbaren Verlust und erzeugt einzelne Geometriedateien. STEP wird beim
Import tesselliert: analytische CAD-Flächen, BRep-Topologie, Baugruppenstruktur,
Parameter, PMI und Materialeigenschaften werden nicht zu einem neuen STEP-Modell
zurückgeschrieben.

Folgende 3MF-Eigenschaften sind bewusst nicht vollständig editierbar: komplex
unterteilte Bambu-Flächenbemalung, Farbverläufe innerhalb eines Dreiecks,
Texturen/UV-Koordinaten, Transparenz als Materialeigenschaft, Support-/Naht-/
Fuzzy-Skin-Bemalung, Modifikator- und Negativteile, Slice-/G-Code-Daten,
Montage- und Instanzhierarchien sowie individuelle Objekt-Druckoptionen.
PartSplice zeigt erkannte Fälle unter
`Informationen > Modellstatus & Details > 3MF-Kompatibilität` an, statt sie
stillschweigend als vollständig unterstützt auszugeben. Drucker-, Prozess- und
Filamentprofile aus `project_settings.config` sowie sichere, benutzererstellte
Zusatzdateien im 3MF-Archiv werden erhalten. Veraltete Vorschaubilder,
Slice-Ergebnisse, G-Code und ersetzte Modellressourcen werden absichtlich nicht
kopiert, weil sie nach einem Schnitt nicht mehr zur Geometrie passen.

## Robustheit und Datensicherheit

Die Datei- und Geometrieverarbeitung enthält feste Größen- und Plausibilitätsgrenzen. Ungültige Koordinaten, beschädigte Indizes, übergroße Zähler und inkonsistente Projektbeziehungen werden vor der Verarbeitung abgewiesen. Projekt-, STL- und 3MF-Ausgaben werden zunächst in temporäre Dateien geschrieben und erst nach vollständigem Erfolg ersetzt, damit abgebrochene oder fehlgeschlagene Vorgänge keine vorhandenen Dateien beschädigen.

Mehrfachschnitte werden transaktional angewendet: Entweder werden alle ausgewählten Teile erfolgreich berechnet oder der bestehende Projektzustand bleibt unverändert. Verbinder werden gegen das tatsächliche dreidimensionale Material geprüft, an die Modelloberfläche begrenzt und vor dem Anwenden auf Überschneidungen kontrolliert. Kleine getrennte Modellbestandteile werden nicht automatisch entfernt.

Bei Schnitten durch mehrere Teile bedeutet Grün, dass ein Verbinder in allen
getroffenen Teilen gültig ist. Gelb warnt vor einer unterschrittenen
Wandreserve, Violett vor einem Konflikt mit einem früheren Schnitt. Orange
kennzeichnet einen nur in einem Teil gültigen Verbinder, Rot einen geometrisch
ungültigen Verbinder. Während des Ziehens wird für eine flüssige Darstellung
nur die Kontur aktualisiert. Bei einem neuen Schnitt erfolgt die vollständige
Materialprüfung beim Loslassen. An einem bereits ausgeführten Schnitt können
dagegen mehrere Verbinder nacheinander verschoben werden. Beim Loslassen prüft
der vorhandene Geometrieindex die neue Lage sofort und markiert ungültige
Positionen rot; erst „Neu schneiden“ berechnet die mehrfarbige Ergebnisgeometrie
einmalig neu.

Laden, Schneiden, erneutes Berechnen verschobener Verbinder, Reparieren und Exportieren laufen als abbrechbare Hintergrundaufgaben. Beim Laden, Reparieren und Erzeugen neuer Teile wird auch der zugehörige Geometrieindex bereits im Hintergrund aufgebaut; die Oberfläche übernimmt danach nur noch die fertigen Daten und erzeugt die OpenGL-Darstellung. Der Abbruch ist kooperativ: Eine einzelne bereits laufende Berechnung in Open CASCADE oder Manifold kann zunächst zu Ende laufen, bevor die Anwendung den Abbruch übernimmt.

Der Fortschrittsdialog zeigt bei mehrteiligen Schnitten und STL-Exporten den
aktuellen Arbeitsschritt sowie die Laufzeit. Bei Vorgängen ohne zuverlässig
messbare Einzelschritte wird bewusst nur eine Aktivitätsanzeige dargestellt.
Wichtige Bedienelemente erklären ihre Wirkung beim Darüberfahren. Überlappende
Verbinder lassen sich durch wiederholtes Anklicken durchschalten oder über
`Rechtsklick > Verbinder auswählen` gezielt wählen.

Auch das Projektspeichern läuft im Hintergrund und ersetzt die Zieldatei erst
nach vollständigem Erfolg. Meshes, Undo-Stände, Exporte und Projektschnappschüsse
teilen unveränderliche Geometriedaten, damit große Modelle nicht für jeden
Arbeitsschritt vollständig im Speicher dupliziert werden.

Wiederholte Projektions- und Materialabfragen verwenden einen pro Mesh
aufgebauten BVH-Geometrieindex. Die zweidimensionale Trefferprüfung arbeitet mit
der tatsächlichen orthografischen Silhouette; innere Kanten der
Dreieckstesselierung werden nicht als Modellrand behandelt. Der Index verwendet
für die üblichen Abfragen feste lokale Arbeitsspeicher und wird auch im
Speicherlimit der Rückgängig-Historie berücksichtigt.

Der Quellcode ist nach Zuständigkeiten gegliedert. Anwendungsdaten und
Sitzungszustand liegen in `app_domain` und `app_state`; Laden/Speichern,
Exportieren, Schneiden/Reparieren und Updates in den jeweiligen
`*_workflow`-Modulen. Darstellung und Eingabe sind über `scene_core`,
`viewport_render`, `viewport_input`, `viewport_interaction` und
`interaction_hit_test` getrennt. Die ImGui-Oberfläche besteht aus kleinen
Modulen für Dialoge, Arbeitsbereich, Kontextmenü, Verbindereditor und
Kalibrierung. `main.cpp` enthält dadurch nur noch Initialisierung,
Ereignisschleife und den abgesicherten Programmeinstieg. Geometrieindex,
Projektion, Dateiformate und der isolierte Geometrie-Worker bleiben unabhängig
davon. So können weitere Importformate, Verbindertypen und Oberflächenfunktionen
ergänzt werden, ohne die zentrale Programmschleife wieder zu vermischen.

Fehler aus Programmstart, Fenster-/OpenGL-Initialisierung, Laden, Reparatur,
Geometrieberechnung, Export und Updateprüfung werden lokal unter
`%LOCALAPPDATA%\PartSplice3D\logs` protokolliert. Das Protokoll ist auf 1 MiB je
Datei und drei Sicherungsgenerationen begrenzt und kann über
`Informationen > Fehlerprotokoll öffnen` angezeigt werden.

Die lokale ImGui-Fensteranordnung liegt unter
`%LOCALAPPDATA%\PartSplice3D\imgui.ini`. Dadurch wird beim Starten oder
Exportieren keine `imgui.ini` neben Modellen, Projekten oder der EXE erzeugt.

## Datenschutz

PartSplice 3D enthält keine Telemetrie und lädt Modelle oder Projekte nicht
hoch. Eine Updateprüfung wird nur auf Wunsch gestartet und ruft die öffentliche
GitHub-Release-API auf; dabei fallen die üblichen Netzwerkdaten wie IP-Adresse
und Programmversion im User-Agent an. Lokale Fehlerprotokolle können vollständige
Dateipfade enthalten. Erhaltene 3MF-Zusatzdateien und Profile können Angaben aus
der Eingangsdatei enthalten und sollten vor einer öffentlichen Weitergabe
geprüft werden.

## Verantwortung für gedruckte Teile

Die Geometrieprüfung ersetzt keine Belastungs-, Material- oder Sicherheitsprüfung.
Verbinder, Druckorientierung, Material und Druckparameter müssen für den jeweiligen
Einsatzzweck praktisch geprüft werden. PartSplice 3D ist nicht als alleinige
Grundlage für sicherheitskritische, tragende oder medizinische Bauteile vorgesehen.

## Lizenz

PartSplice 3D ist für private, nichtkommerzielle, schulische und gemeinnützige Zwecke kostenlos. Der Quellcode darf unter den Bedingungen der **PolyForm Noncommercial License 1.0.0** verwendet, verändert und nichtkommerziell weitergegeben werden.

Kommerzielle Nutzung, Verkauf, kostenpflichtige Weitergabe oder die kommerzielle Nutzung eines Forks ist ohne vorherige schriftliche Genehmigung nicht gestattet. Die vollständigen Bedingungen stehen in [LICENSE](LICENSE).

Die Beschränkung betrifft jede kommerzielle Nutzung, nicht nur den Verkauf eines
Forks. Ein öffentliches GitHub-Repository kann im Rahmen der GitHub-Funktionen
angesehen und geforkt werden; daraus entsteht keine zusätzliche Erlaubnis zur
kommerziellen Nutzung außerhalb der ausdrücklich gewährten Rechte.

PartSplice 3D ist damit **source available**, aber keine OSI-zertifizierte Open-Source-Software.

## Beiträge

Externe Beiträge werden nur nach vorheriger Rechteklärung übernommen; Einzelheiten
stehen in [CONTRIBUTING.md](CONTRIBUTING.md).

## Markenzeichen

Genannte Hersteller- und Druckernamen dienen ausschließlich zur Beschreibung kompatibler Druckbett-Voreinstellungen. PartSplice 3D ist ein unabhängiges Projekt und steht insbesondere nicht in Verbindung mit Bambu Lab oder anderen Druckerherstellern.

Von PartSplice exportierte 3MF-Dateien nennen PartSplice 3D als erzeugende
Anwendung. Bambu-kompatible Namensräume und Profilbezeichnungen dienen nur der
technischen Interoperabilität.
