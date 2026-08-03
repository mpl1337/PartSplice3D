#include "localization.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::atomic<AppLanguage> language{AppLanguage::German};

std::filesystem::path languagePath() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size())
        return std::filesystem::path(buffer.data()) / L"PartSplice3D" / L"language.txt";
#endif
    return std::filesystem::temp_directory_path() / "PartSplice3D-language.txt";
}

const std::unordered_map<std::string_view, std::string_view>& translations() {
    static const std::unordered_map<std::string_view, std::string_view> values{
        {"Datei oder Projekt öffnen", "Open file or project"},
        {"Datei oder Projekt öffnen ...", "Open file or project..."},
        {"Öffne ein 3D-Modell oder ein gespeichertes PartSplice-Projekt.", "Open a 3D model or a saved PartSplice project."},
        {"Modelle werden beim Laden automatisch geprüft. Gespeicherte Projekte setzen die Bearbeitung mit allen Schnitten fort.", "Models are checked automatically while loading. Saved projects resume editing with all cuts."},
        {"Ohne Datei fortfahren", "Continue without a file"},
        {"Datei / Projekt", "File / Project"},
        {"Neues Projekt", "New project"},
        {"Zuletzt verwendet", "Recent files"},
        {"Keine Einträge", "No entries"},
        {"Projekt speichern", "Save project"},
        {"Projekt speichern unter ...", "Save project as..."},
        {"Exportformat", "Export format"},
        {"3MF – alle Objekte in einer Datei", "3MF – all objects in one file"},
        {"STL – jedes Teil als eigene Datei", "STL – one file per part"},
        {"Druckdateien exportieren ...", "Export print files..."},
        {"Projekt schließen", "Close project"},
        {"Programm beenden", "Exit application"},
        {"Hinzufügen", "Add"},
        {"Gerader Schnitt", "Straight cut"},
        {"Mehrpunkt-Schnitt", "Polyline cut"},
        {"Schnittbearbeitung abbrechen", "Cancel cut editing"},
        {"Aktuellen Schnitt abbrechen", "Cancel current cut"},
        {"Alle getroffenen Teile schneiden", "Cut all intersected parts"},
        {"Verbinder nur vollständig im Material", "Connectors only when fully inside material"},
        {"Schnittnummern eingravieren", "Engrave cut numbers"},
        {"Schnitt und Verbinder erzeugen", "Create cut and connectors"},
        {"Bearbeiten", "Edit"},
        {"Rückgängig", "Undo"},
        {"Informationen", "Information"},
        {"Modellstatus & Details", "Model status & details"},
        {"3MF-Farben & Filamente", "3MF colors & filaments"},
        {"Erkannte Slots: %zu", "Detected slots: %zu"},
        {"3MF-Kompatibilität", "3MF compatibility"},
        {"%zu Eigenschaftshinweis(e)", "%zu property warning(s)"},
        {"Mehr als 256 Materialien/Farben wurden auf die vorhandene Palette begrenzt.", "More than 256 materials/colors were limited to the available palette."},
        {"3MF enthält Textur-, Verbundmaterial-, Gitter- oder Slice-Ressourcen, die nicht bearbeitbar übernommen werden.", "The 3MF contains texture, composite-material, lattice, or slice resources that cannot be imported as editable data."},
        {"Transparenz aus 3MF-Materialfarben wird nicht als Filamenteigenschaft exportiert.", "Transparency from 3MF material colors is not exported as a filament property."},
        {"Komplex unterteilte Bambu-Flächenbemalung konnte nicht vollständig übernommen werden.", "Complex subdivided Bambu face painting could not be preserved completely."},
        {"3MF-Verlaufsfarben pro Dreieck wurden auf die erste Eckfarbe reduziert.", "Per-triangle 3MF gradients were reduced to the first vertex color."},
        {"Zusätzliche Flächenattribute (z. B. Support-, Naht- oder Fuzzy-Skin-Bemalung) werden nicht übernommen.", "Additional face attributes (such as support, seam, or fuzzy-skin painting) are not preserved."},
        {"3MF-Komponenten und Instanzen wurden für die Bearbeitung geometrisch zusammengeführt; ihre Hierarchie geht beim Export verloren.", "3MF components and instances were flattened for editing; their hierarchy is lost on export."},
        {"Eigene 3MF-Dokumentmetadaten wie Autor, Beschreibung oder Copyright werden nicht neu exportiert.", "Custom 3MF document metadata such as author, description, or copyright is not re-exported."},
        {"3MF-Montagebeziehungen werden beim Geometrieexport nicht erhalten.", "3MF assembly relationships are not preserved during geometry export."},
        {"Objekt-/Teil-Metadaten des Slicers (z. B. Quellreferenzen oder individuelle Druckoptionen) werden nicht neu exportiert.", "Slicer object/part metadata (such as source references or individual print options) is not re-exported."},
        {"Bambu-Modifikator-, Negativ-, Support- oder sonstige Spezialteile werden als normale Geometrie behandelt.", "Bambu modifier, negative, support, or other special parts are treated as normal geometry."},
        {"Eine Bambu-Teil-Farbzuweisung war wegen mehrfach verwendeter Objekt-IDs mehrdeutig.", "A Bambu part color assignment was ambiguous because object IDs were reused."},
        {"Wiederholte 3MF-Instanzen werden als eigenständige Objekte exportiert; ihre Instanzbeziehung geht verloren.", "Repeated 3MF instances are exported as separate objects; their instance relationship is lost."},
        {"Fehlerprotokoll öffnen ...", "Open error log..."},
        {"Werkzeuge", "Tools"},
        {"Schnittnummern bearbeiten ...", "Edit cut numbers..."},
        {"Raster ...", "Grid..."},
        {"Verbinder-Testmuster ...", "Connector test samples..."},
        {"Sprache", "Language"},
        {"Deutsch", "German"},
        {"Englisch", "English"},
        {"Rote Verbinder behandeln?", "Handle invalid connectors?"},
        {"Ungültige verwerfen und Schnitt ausführen", "Discard invalid connectors and apply cut"},
        {"Materialprüfung erzwingen", "Force material check override"},
        {"Problematische Verbinder automatisch platzieren", "Automatically reposition problematic connectors"},
        {"Abbrechen", "Cancel"},
        {"Farben beim STL-Export verlieren?", "Lose colors during STL export?"},
        {"STL unterstützt keine Farben oder Filamentzuweisungen.", "STL does not support colors or filament assignments."},
        {"Beim Export entstehen einzelne, einfarbige STL-Dateien. Wähle 3MF, wenn die Mehrfarben-Zuordnung erhalten bleiben soll.", "Export creates separate single-color STL files. Choose 3MF to preserve multi-color assignments."},
        {"Trotzdem als STL exportieren", "Export as STL anyway"},
        {"Schnitt erneut bearbeiten?", "Edit cut again?"},
        {"Erneut bearbeiten", "Edit again"},
        {"Vorherigen Schnitt behandeln?", "Handle previous cut?"},
        {"Es ist bereits ein Schnitt vorbereitet.", "A cut is already prepared."},
        {"Soll dieser zuerst angewendet werden?", "Apply it first?"},
        {"Anwenden und neuen Schnitt beginnen", "Apply and begin new cut"},
        {"Verwerfen und neu beginnen", "Discard and begin again"},
        {"Projektänderungen speichern?", "Save project changes?"},
        {"Nicht speichern", "Don't save"},
        {"Modellprüfung", "Model check"},
        {"Das Modell ist kein wasserdichter Volumenkörper.", "The model is not a watertight solid."},
        {"Offene Kanten: %zu", "Open edges: %zu"},
        {"Mehrfach belegte Kanten: %zu", "Non-manifold edges: %zu"},
        {"Falsch ausgerichtete Kanten: %zu", "Incorrectly oriented edges: %zu"},
        {"Jetzt mit Windows reparieren", "Repair with Windows now"},
        {"Ohne Reparatur fortfahren", "Continue without repair"},
        {"Der Windows-Dienst kann Flächen-Farbzuweisungen verändern. PartSplice prüft und meldet das Ergebnis.", "The Windows service may change per-face color assignments. PartSplice checks and reports the result."},
        {"Kein Modell geladen.", "No model loaded."},
        {"Datei: %s", "File: %s"},
        {"Objekt: %s", "Object: %s"},
        {"Aktives Teil: %s", "Active part: %s"},
        {"Dreiecke: %zu | Eckpunkte: %zu", "Triangles: %zu | Vertices: %zu"},
        {"Abmessungen: %.2f × %.2f × %.2f mm", "Dimensions: %.2f × %.2f × %.2f mm"},
        {"Aktuelle Teile: %zu | Schnitte: %zu", "Current parts: %zu | Cuts: %zu"},
        {"Wasserdichter Volumenkörper", "Watertight solid"},
        {"%zu problematische Kanten", "%zu problematic edges"},
        {"Offen: %zu | Mehrfach: %zu | Ausrichtung: %zu", "Open: %zu | Non-manifold: %zu | Orientation: %zu"},
        {"Problemstellen im Modell anzeigen", "Show problem areas in model"},
        {"Reparatur abbrechen", "Cancel repair"},
        {"Windows-3D-Dienst arbeitet ...", "Windows 3D service is working..."},
        {"Mit Windows reparieren", "Repair with Windows"},
        {"Objektauswahl", "Object selection"},
        {"Objekt", "Object"},
        {"Druckbett", "Print bed"},
        {"Eigene Druckbettmaße", "Custom print-bed dimensions"},
        {"DruckbettOptionen", "PrintBedOptions"},
        {"Rahmen anzeigen", "Show frame"},
        {"Am Modell zentrieren", "Center on model"},
        {"Eigene nutzbare Druckbettfläche", "Custom usable print-bed area"},
        {"Breite [mm]", "Width [mm]"},
        {"Tiefe [mm]", "Depth [mm]"},
        {"Übernehmen", "Apply"},
        {"Objekt wechseln?", "Switch object?"},
        {"Am aktuellen Objekt gibt es einen noch nicht angewendeten Schnitt.", "The current object has a cut that has not yet been applied."},
        {"Soll dieser Schnitt vor dem Wechsel ausgeführt oder verworfen werden?", "Apply or discard this cut before switching?"},
        {"Schnitt anwenden und wechseln", "Apply cut and switch"},
        {"Schnitt verwerfen und wechseln", "Discard cut and switch"},
        {"Modellstruktur", "Model structure"},
        {"MODELL & SCHNITTE", "MODEL & CUTS"},
        {"Schnitte", "Cuts"},
        {"Noch kein Schnitt", "No cuts yet"},
        {"Schnitt erneut bearbeiten", "Edit cut again"},
        {"Schnitt löschen", "Delete cut"},
        {"Aktuelle Teile", "Current parts"},
        {"Original anzeigen", "Show original"},
        {"Explosionsabstand", "Exploded-view distance"},
        {"Schnitte anwenden", "Apply cuts"},
        {"Neu schneiden", "Re-cut"},
        {"Berechnet den vorhandenen Schnitt einmalig mit allen geänderten Verbinderpositionen neu.", "Recalculates the existing cut once with all changed connector positions."},
        {"Schnittmodus-Hinweis", "CutModeHint"},
        {"Raster", "Grid"},
        {"Am Raster einrasten", "Snap to grid"},
        {"Rasterweite [mm]", "Grid spacing [mm]"},
        {"Segmentwinkel einrasten", "Snap segment angles"},
        {"Winkelschritt [°]", "Angle increment [°]"},
        {"An Modellkanten einrasten", "Snap to model edges"},
        {"Schnittnummern", "Cut numbers"},
        {"Kennzeichnung: %d", "Marking: %d"},
        {"Beide zusammengehörigen Teile erhalten dieselbe eingravierte Schnittnummer.", "Both matching parts receive the same engraved cut number."},
        {"Schnittnummer eingravieren", "Engrave cut number"},
        {"Automatische Position [%]", "Automatic position [%]"},
        {"Ziffernhöhe [mm]", "Digit height [mm]"},
        {"Gravurtiefe [mm]", "Engraving depth [mm]"},
        {"Position Zahl auf Teil A", "Number position on part A"},
        {"Position Zahl auf Teil B", "Number position on part B"},
        {"Beide automatisch an der Schnittlinie platzieren", "Place both automatically along cut line"},
        {"Beide Zahlen können im Modell unabhängig und frei in X/Y verschoben werden.", "Both numbers can be moved independently and freely in X/Y on the model."},
        {"Bei dünnen Bauteilen wird die Gravurtiefe automatisch begrenzt.", "Engraving depth is limited automatically on thin parts."},
        {"Ansichtswerkzeuge", "View tools"},
        {"Oben", "Top"},
        {"Einpassen", "Fit"},
        {"Flächen", "Solid"},
        {"Draht", "Wire"},
        {"Bett aus", "Bed off"},
        {"Bett", "Bed"},
        {"Messen ✓", "Measure ✓"},
        {"Messen", "Measure"},
        {"Modelltransparenz", "Model transparency"},
        {"Transparenz", "Transparency"},
        {"Programm beenden?", "Exit application?"},
        {"Speichern und beenden", "Save and exit"},
        {"Ohne Speichern", "Exit without saving"},
        {"Verbinder-Eigenschaften", "Connector properties"},
        {"Verbinder %d", "Connector %d"},
        {"Kopfbreite [mm]", "Head width [mm]"},
        {"Einbindung [mm]", "Embed depth [mm]"},
        {"Spiel je Seite [mm]", "Clearance per side [mm]"},
        {"Einführfase", "Lead-in chamfer"},
        {"Fasenbreite [mm]", "Chamfer width [mm]"},
        {"Fasenwinkel [°]", "Chamfer angle [°]"},
        {"Eingefroren", "Locked"},
        {"Drehen", "Flip"},
        {"Kopieren", "Copy"},
        {"Löschen", "Delete"},
        {"Verbinder-Testmuster", "Connector test samples"},
        {"Geometrie", "Geometry"},
        {"Spielreihe", "Clearance series"},
        {"Kleinster Wert [mm]", "Smallest value [mm]"},
        {"Schrittweite [mm]", "Step [mm]"},
        {"Anzahl Proben", "Number of samples"},
        {"Engere Reihe erzeugen", "Create tighter series"},
        {"3MF-Druckerprofil", "3MF printer profile"},
        {"Bambu-Drucker", "Bambu printer"},
        {"STL speichern ...", "Save STL..."},
        {"3MF speichern ...", "Save 3MF..."},
        {"Nach dem Druck zusammenbauen und bewerten", "Assemble and evaluate after printing"},
        {"Probe %d: %.3f mm", "Sample %d: %.3f mm"},
        {"Als optimale Probe gewählt: %.3f mm Spiel je Seite", "Selected as optimal sample: %.3f mm clearance per side"},
        {"Beste Probe als Standard übernehmen", "Use best sample as default"},
        {"Schnittpunkt-Menü", "CutPointMenu"},
        {"%zu überlappende Verbinder", "%zu overlapping connectors"},
        {"Verbinder auswählen", "Select connector"},
        {"Verbinder hinzufügen", "Add connector"},
        {"Verbinder löschen", "Delete connector"},
        {"Verbinder lösen", "Unlock connector"},
        {"Verbinder einfrieren", "Lock connector"},
        {"Verbinder drehen", "Flip connector"},
        {"Verbinder kopieren", "Copy connector"},
        {"Knickpunkt löschen", "Delete corner point"},
        {"Punkt lösen", "Unlock point"},
        {"Punkt einfrieren", "Lock point"},
        {"Koordinaten eingeben ...", "Enter coordinates..."},
        {"Verbinder hier hinzufügen", "Add connector here"},
        {"Knickpunkt hinzufügen", "Add corner point"},
        {"Kopierten Verbinder hier einfügen", "Paste copied connector here"},
        {"Segment präzise ausrichten", "Align segment precisely"},
        {"Horizontal (0°)", "Horizontal (0°)"},
        {"Vertikal (90°)", "Vertical (90°)"},
        {"Neuen Schnitt starten", "Start new cut"},
        {"Schnittpunkt-Koordinaten", "Cut-point coordinates"},
        {"Schnittpunkt %d", "Cut point %d"},
        {"Neue Version verfügbar", "New version available"},
        {"Installiert: %s", "Installed: %s"},
        {"Verfügbar: %s", "Available: %s"},
        {"Update herunterladen ...", "Download update..."},
        {"Später", "Later"},
        {"PartSplice 3D ist aktuell.", "PartSplice 3D is up to date."},
        {"Installierte Version: %s", "Installed version: %s"},
        {"Neuestes Release: %s", "Latest release: %s"},
        {"Repo nicht gefunden.", "Repository not found."},
        {"Schließen", "Close"},
        {"Vorgang läuft", "Operation in progress"},
        {"Vorgang wird ausgeführt ...", "Operation in progress..."},
        {"Arbeite ...", "Working..."},
        {"Laufzeit: %lld s", "Elapsed: %lld s"},
        {"eingefroren", "locked"},
        {"beweglich", "movable"},
        {"Zapfen lokal links", "male connector locally left"},
        {"Zapfen lokal rechts", "male connector locally right"},
        {"Schnittnummer %d", "Cut number %d"},
        {"Frei ziehen = nur diese Zahl verschieben · Anklicken = Einstellungen", "Drag freely = move only this number · Click = settings"},
        {"Gravur von Schnitt %d", "Engraving for cut %d"},
        {"Doppelklick = Schnittnummer erneut bearbeiten", "Double-click = edit cut number again"}
        ,{"Schwalbenschwanz", "Dovetail"}
        ,{"Rechteck-Zapfen", "Rectangular tab"}
        ,{"Puzzle-Kopf", "Puzzle connector"}
        ,{"Rundzapfen", "Round pin"}
        ,{"Bauart", "Type"}
        ,{"Zapfen auf", "Male connector on"}
        ,{"Teil A / lokal links", "Part A / locally left"}
        ,{"Teil B / lokal rechts", "Part B / locally right"}
        ,{"Nicht bewertet", "Not rated"}
        ,{"Zu stramm", "Too tight"}
        ,{"Optimal", "Optimal"}
        ,{"Zu locker", "Too loose"}
        ,{"Beste", "Best"}
        ,{"Spiel", "Clearance"}
        ,{"Bewertung", "Rating"}
        ,{"GERADER SCHNITT", "STRAIGHT CUT"}
        ,{"MEHRPUNKT-SCHNITT", "POLYLINE CUT"}
        ,{"Ersten Schnittpunkt platzieren", "Place the first cut point"}
        ,{"Zweiten Schnittpunkt platzieren", "Place the second cut point"}
        ,{"Weiteren Knickpunkt platzieren · Rechtsklick beendet", "Place another corner point · Right-click to finish"}
        ,{"Fang: %s%s%s", "Snap: %s%s%s"}
        ,{"Raster ", "Grid "}
        ,{"Winkel ", "Angle "}
        ,{"Modellkante", "Model edge"}
        ,{"| Strg = Fang aus", "| Ctrl = disable snapping"}
        ,{"Ausladung des Verbinders senkrecht zur Schnittlinie.", "Connector reach perpendicular to the cut line."}
        ,{"Zusätzliche Überdeckung am Hals für robuste Boolesche Geometrie.", "Additional overlap at the neck for robust Boolean geometry."}
        ,{"Seitlicher Abstand zwischen Zapfen und Nut. Kleiner sitzt strammer.", "Lateral clearance between male and female connector. Smaller values fit tighter."}
        ,{"Breite der Einführfase an Zapfennase und Nuteinlauf.", "Width of the lead-in chamfer on the male tip and female entrance."}
        ,{"Flankenwinkel der Einführfase; die Passung dahinter bleibt unverändert.", "Flank angle of the lead-in chamfer; the fit behind it remains unchanged."}
        ,{"Erleichtert nur den Einlauf; die Passung bleibt erhalten.", "Only eases insertion; the fit remains unchanged."}
        ,{"Zapfen-Nase und Nut-Einlauf werden angefast; die Passung dahinter bleibt nominal.", "The male tip and female entrance are chamfered; the fit behind them remains nominal."}
        ,{"Wenn selbst Probe 1 zu locker ist, hiermit den Bereich nach unten verschieben.", "If even sample 1 is too loose, use this to shift the range downward."}
        ,{"Profil und Testkörper werden passend zum gewählten Druckbett angeordnet.", "The profile and test bodies are arranged for the selected print bed."}
        ,{"Der Radiopunkt kennzeichnet die Probe, die du als optimale Passung ausgewählt hast.", "The radio button marks the sample selected as the optimal fit."}
        ,{"0 %% = vollständig deckend; höhere Werte machen das Modell durchsichtig.", "0 %% = fully opaque; higher values make the model transparent."}
        ,{"Berechnet den vorbereiteten Schnitt und alle Verbinder im Hintergrund.", "Calculates the prepared cut and all connectors in the background."}
        ,{"Blendet den verschiebbaren Referenzrahmen des Druckbetts ein oder aus.", "Shows or hides the movable print-bed reference frame."}
        ,{"Drehung", "Rotation"}
        ,{"Messung: %.3f mm", "Measurement: %.3f mm"}
        ,{"Montageprüfung bestanden", "Assembly check passed"}
        ,{"Geometrieprüfung bestanden", "Geometry check passed"}
        ,{"nicht vollständig im Material", "not fully inside material"}
        ,{"überschneidet einen Knick der Schnittlinie", "crosses a corner of the cut line"}
        ,{"kollidiert mit einem früheren Schnitt", "collides with an earlier cut"}
        ,{"Wandreserve unterschritten", "wall reserve too small"}
        ,{"Aktiv: Der Linienzug schneidet jedes berührte aktive Teil.\nInaktiv: Nur das im Modellbaum ausgewählte Teil wird geschnitten.", "Active: The polyline cuts every active part it touches.\nInactive: Only the part selected in the model tree is cut."}
        ,{"Prüft Zapfen und Nut gegen das Material. Rote Verbinder werden vor dem Anwenden nochmals bestätigt.", "Checks male and female connectors against the material. Red connectors require confirmation before applying."}
        ,{"Graviert die Schnittnummer sichtbar in beide zusammengehörigen Teile.", "Engraves the cut number visibly into both matching parts."}
        ,{"Lokales, auf 3 Sicherungskopien begrenztes Protokoll unter %%LOCALAPPDATA%%\\PartSplice3D\\logs.", "Local log limited to 3 backup copies under %%LOCALAPPDATA%%\\PartSplice3D\\logs."}
        ,{"Rot oder orange markierte Verbinder sind geometrisch nicht sicher ausführbar. Dazu zählen auch Verbinder, deren vollständiges Werkzeug einen Knick der Schnittlinie überschneidet. Gelbe beziehungsweise violette Verbinder sind möglich, besitzen aber zu wenig Wandreserve oder berühren einen früheren Schnitt.", "Red or orange connectors cannot be created safely. This includes connectors whose complete tool crosses a corner of the cut line. Yellow or purple connectors are possible but have insufficient wall reserve or touch an earlier cut."}
        ,{"Materialwarnungen können erzwungen werden. Überschneidungen mit Schnittknicken oder anderen Verbindern\nwerden zum Schutz vor Hohlräumen weiterhin verworfen.", "Material warnings can be overridden. Intersections with cut corners or other connectors\nare still discarded to prevent cavities."}
        ,{"Das aktuelle Schnittergebnis und davon abhängige spätere Schnitte werden dabei zurückgenommen. Linie, Knickpunkte, Verbinderpositionen und Einzelparameter werden wiederhergestellt.", "The current cut result and dependent later cuts will be rolled back. The line, corner points, connector positions and individual parameters will be restored."}
        ,{"Der momentan vorbereitete, noch nicht angewendete Schnitt wird verworfen.", "The currently prepared cut that has not yet been applied will be discarded."}
        ,{"Der bisherige Linienzug ist noch nicht vollständig und kann nur verworfen werden.", "The current polyline is incomplete and can only be discarded."}
        ,{"Das aktuelle Projekt enthält ungespeicherte Änderungen. Möchtest du die Bearbeitung als PartSplice-Projekt speichern?", "The current project contains unsaved changes. Do you want to save the work as a PartSplice project?"}
        ,{"Für saubere Schnitte und Verbinder sollte das Modell zuerst repariert werden. PartSplice 3D kann dafür den lokalen Windows-3D-Reparaturdienst verwenden. Die Originaldatei bleibt unverändert.", "For clean cuts and connectors, the model should be repaired first. PartSplice 3D can use the local Windows 3D repair service. The original file remains unchanged."}
        ,{"Punkte können zusätzlich per Rechtsklick numerisch bearbeitet werden. Auf einem Segment stehen Horizontal, Vertikal und der nächste Winkelschritt zur Verfügung. Beim Platzieren oder Ziehen setzt Strg den Fang vorübergehend aus.", "Points can also be edited numerically with a right-click. Horizontal, vertical and the next angle increment are available on a segment. Hold Ctrl while placing or dragging to disable snapping temporarily."}
        ,{"Erzeugt mehrere zusammengehörige Zapfen-/Nut-Proben mit unterschiedlichen Spielwerten als STL- oder 3MF-Datei. Die Anzahl kleiner Markierungslöcher entspricht der Probennummer.", "Creates several matching male/female samples with different clearance values as an STL or 3MF file. The number of small marker holes identifies the sample number."}
        ,{"1. Zapfen und Nut mit derselben Probennummer zusammenstecken. 2. Jede Passung nach deinem eigenen Gefühl unten bewerten. 3. Den Radiopunkt bei der insgesamt besten Passung setzen und diese anschließend als Standard übernehmen.", "1. Join the male and female samples with the same number. 2. Rate each fit below based on how it feels. 3. Select the best overall fit and apply it as the default."}
        ,{"Die offizielle GitHub-Release-Seite wird im Browser geöffnet. Das Herunterladen und Installieren erfolgt bewusst außerhalb von PartSplice 3D.", "The official GitHub release page will open in your browser. Downloading and installation intentionally take place outside PartSplice 3D."}
        ,{"Es gibt Änderungen, die noch nicht als Projekt gespeichert wurden. Möchtest du die Bearbeitung vor dem Beenden speichern?", "There are changes that have not yet been saved as a project. Do you want to save your work before exiting?"}
        ,{"Der Schnitt ist noch nicht vollständig und kann nur verworfen werden.", "The cut is incomplete and can only be discarded."}
        ,{"Projekt wird atomar gespeichert ...", "Project is being saved atomically..."}
        ,{"Abbruch angefordert ...", "Cancellation requested..."}
        ,{"PartSplice 3D aktualisieren", "Update PartSplice 3D"}
        ,{"Die offizielle GitHub-Release-Seite wird im Browser geöffnet. PartSplice 3D lädt oder startet aus Sicherheitsgründen nichts ungefragt.", "The official GitHub release page will open in your browser. For security, PartSplice 3D never downloads or starts anything without asking."}
        ,{"Schnitt und Verbinder werden im Hintergrund berechnet ...", "Cut and connectors are being calculated in the background..."}
        ,{"Verschobener Verbinder und betroffene Teile werden im Hintergrund neu berechnet ...", "The moved connector and affected parts are being recalculated in the background..."}
        ,{"PartSplice-Projekt wird im Hintergrund gespeichert ...", "The PartSplice project is being saved in the background..."}
        ,{"Datei wird geladen und geprüft ...", "The file is being loaded and checked..."}
        ,{"STL-Dateien werden geprüft und atomar exportiert ...", "STL files are being checked and exported atomically..."}
        ,{"3MF-Datei wird geprüft und atomar exportiert ...", "The 3MF file is being checked and exported atomically..."}
    };
    return values;
}

void replaceAll(std::string& text, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    size_t offset = 0;
    while ((offset = text.find(from, offset)) != std::string::npos) {
        text.replace(offset, from.size(), to);
        offset += to.size();
    }
}

std::string translateFallback(std::string text) {
    static const std::pair<std::string_view, std::string_view> phrases[]{
        {"vollständig im Material", "fully inside the material"},
        {"nicht vollständig", "not complete"},
        {"Nicht genügend Arbeitsspeicher", "Not enough memory"},
        {"konnte nicht gestartet werden", "could not be started"},
        {"konnte nicht geöffnet werden", "could not be opened"},
        {"konnte nicht gespeichert werden", "could not be saved"},
        {"wurde automatisch verschoben", "was moved automatically"},
        {"wurden automatisch verschoben", "were moved automatically"},
        {"Keine bessere freie Position gefunden", "No better free position found"},
        {"Schnittnummer", "cut number"},
        {"Schnittpunkt", "cut point"},
        {"Schnittlinie", "cut line"},
        {"Schnittberechnung", "cut calculation"},
        {"Schnitt", "cut"},
        {"Verbinder", "connector"},
        {"Modellprüfung", "model check"},
        {"Volumenkörper", "solid"},
        {"Druckbett", "print bed"},
        {"Projekt", "project"},
        {"Datei", "file"},
        {"Modell", "model"},
        {"Bauteil", "part"},
        {"Teile", "parts"},
        {"Teil ", "Part "},
        {"Fehler", "error"},
        {"Reparatur", "repair"},
        {"gespeichert", "saved"},
        {"geladen", "loaded"},
        {"abgebrochen", "cancelled"},
        {"ungültig", "invalid"},
        {"Ungültige", "Invalid"},
        {"gültig", "valid"},
        {"Aktiv", "Active"},
        {"Inaktiv", "Inactive"},
        {"ausgewählt", "selected"},
        {"geöffnet", "opened"},
        {"geschlossen", "closed"},
        {"übernommen", "applied"},
        {"verschoben", "moved"},
        {"gelöscht", "deleted"},
        {"hinzufügen", "add"},
        {"löschen", "delete"},
        {"einfrieren", "lock"},
        {"lösen", "unlock"},
        {"anzeigen", "show"},
        {"ausblenden", "hide"},
        {"Abbrechen", "Cancel"},
        {"Speichern", "Save"},
        {"Öffnen", "Open"},
        {"Beenden", "Exit"},
        {"Keine ", "No "},
        {"Kein ", "No "},
        {"Es gibt ", "There are "},
        {"Es ist ", "There is "},
        {"wird ", "is being "},
        {"wurde ", "was "},
        {"werden ", "are being "},
        {"können ", "can "},
        {"kann ", "can "},
        {"und ", "and "},
        {" oder ", " or "},
        {" im ", " in the "},
        {" am ", " on the "},
        {" auf ", " on "},
        {" für ", " for "},
        {" mit ", " with "},
        {" nicht ", " not "}
    };
    for (const auto& [from, to] : phrases) replaceAll(text, from, to);
    return text;
}

} // namespace

AppLanguage currentLanguage() noexcept { return language.load(std::memory_order_relaxed); }

void setCurrentLanguage(AppLanguage value) noexcept {
    language.store(value, std::memory_order_relaxed);
}

bool loadLanguagePreference() {
    std::ifstream input(languagePath());
    std::string value;
    if (!(input >> value)) return false;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    setCurrentLanguage(value == "en" || value == "english"
                           ? AppLanguage::English : AppLanguage::German);
    return true;
}

bool saveLanguagePreference() {
    const std::filesystem::path path = languagePath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    std::ofstream output(path, std::ios::trunc);
    if (!output) return false;
    output << (currentLanguage() == AppLanguage::English ? "en\n" : "de\n");
    return static_cast<bool>(output);
}

std::string localizedText(std::string_view german) {
    if (currentLanguage() == AppLanguage::German || german.empty()) return std::string(german);
    const size_t idPosition = german.find("##");
    const std::string_view visible = idPosition == std::string_view::npos
        ? german : german.substr(0, idPosition);
    const std::string_view id = idPosition == std::string_view::npos
        ? std::string_view{} : german.substr(idPosition);
    std::string translated;
    if (const auto found = translations().find(visible); found != translations().end())
        translated = std::string(found->second);
    else
        translated = translateFallback(std::string(visible));
    translated.append(id);
    return translated;
}

const char* localizedCString(const char* german) {
    thread_local std::string translated;
    translated = localizedText(german != nullptr ? german : "");
    return translated.c_str();
}

std::wstring localizedWide(std::wstring_view german, std::wstring_view english) {
    return std::wstring(currentLanguage() == AppLanguage::English ? english : german);
}
