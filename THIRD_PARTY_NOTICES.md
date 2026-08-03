# Drittanbieter-Hinweise

PartSplice 3D verwendet folgende Komponenten. Diese bleiben unabhängig von der PartSplice-3D-Lizenz unter ihren jeweiligen Originalbedingungen lizenziert.

| Komponente | Version | Lizenz | Lizenztext |
| --- | --- | --- | --- |
| Manifold | 3.5.1 | Apache License 2.0 | [Manifold-Apache-2.0.txt](third_party_licenses/Manifold-Apache-2.0.txt) |
| Dear ImGui | 1.92.9 | MIT License | [Dear-ImGui-MIT.txt](third_party_licenses/Dear-ImGui-MIT.txt) |
| GLFW | 3.4 | zlib/libpng License | [GLFW-zlib.md](third_party_licenses/GLFW-zlib.md) |
| miniz | 3.1.2 | MIT License | [miniz-MIT.txt](third_party_licenses/miniz-MIT.txt) |
| TinyXML-2 | 10.1.0 | zlib License | [TinyXML2-zlib.txt](third_party_licenses/TinyXML2-zlib.txt) |
| Clipper2 | von Manifold eingebundene Version | Boost Software License 1.0 | [Clipper2-Boost-1.0.txt](third_party_licenses/Clipper2-Boost-1.0.txt) |
| DejaVu Sans | eingebettete Schrift | DejaVu/Bitstream-Vera-/Arev-Bedingungen | [DejaVu-Fonts.txt](third_party_licenses/DejaVu-Fonts.txt) |
| Open CASCADE Technology | 7.9.2 | GNU LGPL 2.1 mit Open-CASCADE-Ausnahme | [LGPL 2.1](third_party_licenses/GNU-LGPL-2.1.txt), [Ausnahmetext](third_party_licenses/Open-CASCADE-exception.txt) |

Die Bibliotheken werden beim CMake-Konfigurieren aus ihren offiziellen Repositories geladen. Die Originalquellen werden nicht in diesem Repository mitgeführt. Open CASCADE wird als dynamische Bibliothek über vcpkg eingebunden. DejaVu Sans wird als Ressource in die Anwendung eingebettet.

Die zum Windows-Paket gehörende Open-CASCADE-Quellversion, der exakt verwendete
vcpkg-Port, die Wiederherstellung eines kompatiblen Builds und das dauerhafte
Quellcodeangebot sind in [OPEN_CASCADE_SOURCE_OFFER.md](OPEN_CASCADE_SOURCE_OFFER.md)
dokumentiert. `PartSpliceWorker.exe` prüft keine Signatur der Open-CASCADE-DLLs
und verhindert ihren Austausch gegen eine ABI-kompatible, selbst erstellte
Version nicht.
