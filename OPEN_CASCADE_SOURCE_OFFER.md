# Open CASCADE Technology: Quellcode und Austauschbarkeit

Das portable Windows-Paket von PartSplice 3D 1.0.0 enthält dynamische
Bibliotheken von Open CASCADE Technology (OCCT) 7.9.2. OCCT steht unter der
GNU Lesser General Public License 2.1 mit der Open-CASCADE-Ausnahme. Die
vollständigen Lizenztexte liegen in `third_party_licenses`.

## Exakt verwendete Quellen

- OCCT 7.9.2, offizieller Tag `V7_9_2`:
  <https://github.com/Open-Cascade-SAS/OCCT/tree/V7_9_2>
- Quellarchiv des Tags:
  <https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_9_2.zip>
- vcpkg-Port und dessen Patches beim festgeschriebenen Baseline-Commit:
  <https://github.com/microsoft/vcpkg/tree/3af1d1e60af2b2abf55760538cd607829029b07a/ports/opencascade>

Das im Repository enthaltene `vcpkg.json` schreibt denselben Baseline-Commit
und das Paket `opencascade` fest. Die Build-Anleitung in `README.md` lädt damit
die Quellen und Port-Patches erneut und erzeugt ABI-kompatible DLLs. PartSplice
nimmt keine eigenen Änderungen am OCCT-Quellcode vor.

## Austauschbarkeit

Nur `PartSpliceWorker.exe` bindet OCCT dynamisch ein. Das Programm verwendet
keine Signatur- oder Integritätssperre, die den Austausch der mitgelieferten
OCCT-DLLs gegen eine selbst erstellte, ABI-kompatible Version verhindert.
Inkompatible Bibliotheken können naturgemäß nicht funktionieren.

## Dauerhaftes Quellcodeangebot

Die oben bezeichneten vollständigen korrespondierenden Quellen und
Buildinformationen werden mindestens drei Jahre nach der letzten Verteilung
von PartSplice 3D 1.0.0 bereitgehalten. Falls einer der Links nicht mehr
erreichbar ist, kann eine Kopie ohne Lizenzgebühr über ein GitHub-Issue unter
<https://github.com/mpl1337/PartSplice3D/issues> angefordert werden; lediglich
tatsächlich anfallende Kosten eines gewünschten physischen Datenträgers und
Versands dürfen berechnet werden.
