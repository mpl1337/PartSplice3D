#include "mesh_ops.h"
#include "project_io.h"
#include "step_io.h"
#include "three_mf.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void writeStatus(const std::filesystem::path& path, const std::string& message) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return;
    output.write(message.data(), static_cast<std::streamsize>(
        std::min<size_t>(message.size(), 64u * 1024u)));
}

int importStep(const std::filesystem::path& input,
               const std::filesystem::path& output,
               const std::filesystem::path& status) {
    StepLoadResult loaded = loadStep(input);
    writeStatus(status, loaded.message);
    if (!loaded.ok) return 10;
    std::string error;
    if (!saveThreeMf(output, loaded.objects, error)) {
        writeStatus(status, "STEP-Worker konnte das Ergebnis nicht schreiben: " + error);
        return 11;
    }
    return 0;
}

int splitModel(const std::filesystem::path& input,
               const std::filesystem::path& output,
               const std::filesystem::path& status) {
    ProjectLoadResult request = loadPartSpliceProject(input);
    if (!request.ok || request.project.objects.size() != 1 ||
        request.project.objects.front().parts.size() != 1) {
        writeStatus(status, "Ungültige oder unvollständige Schnittanfrage.");
        return 20;
    }
    const TriangleMesh& source = request.project.objects.front().parts.front().mesh;
    DovetailResult split = createDovetailSplit(
        source, request.project.cutPoints, request.project.settings,
        request.project.placements, request.project.cutAllActiveParts);
    writeStatus(status, split.message);
    if (!split.ok) return 21;

    PartSpliceProject response;
    ProjectObjectState object;
    object.name = "Worker-Ergebnis";
    object.parts.push_back({1, -1, "Teil A", std::move(split.partA), true});
    object.parts.push_back({2, -1, "Teil B", std::move(split.partB), true});
    object.selectedPartId = 1;
    object.nextPartId = 3;
    response.workerConnectorFlags.reserve(split.connectors.size() * 2);
    for (const ConnectorPreview& connector : split.connectors) {
        response.workerConnectorFlags.push_back(connector.valid ? 1 : 0);
        response.workerConnectorFlags.push_back(connector.overlap ? 1 : 0);
    }
    response.objects.push_back(std::move(object));
    std::string error;
    if (!savePartSpliceProject(output, response, error)) {
        writeStatus(status, "Schnitt-Worker konnte das Ergebnis nicht schreiben: " + error);
        return 22;
    }
    return 0;
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                  LOAD_LIBRARY_SEARCH_SYSTEM32 |
                                  LOAD_LIBRARY_SEARCH_USER_DIRS)) return 2;
    if (argc != 5) return 3;
    const std::wstring mode = argv[1];
    try {
        if (mode == L"--step-import") return importStep(argv[2], argv[3], argv[4]);
        if (mode == L"--split") return splitModel(argv[2], argv[3], argv[4]);
    } catch (const std::bad_alloc&) {
        writeStatus(argv[4], "Nicht genügend Arbeitsspeicher im Geometrie-Worker.");
        return 30;
    } catch (const std::exception& exception) {
        writeStatus(argv[4], std::string("Geometrie-Worker: ") + exception.what());
        return 31;
    } catch (...) {
        writeStatus(argv[4], "Unbekannter Fehler im Geometrie-Worker.");
        return 32;
    }
    writeStatus(argv[4], "Unbekannter Worker-Modus.");
    return 4;
}
#else
int main(int argc, char** argv) {
    if (argc != 5) return 3;
    try {
        const std::string mode = argv[1];
        if (mode == "--step-import") return importStep(argv[2], argv[3], argv[4]);
        if (mode == "--split") return splitModel(argv[2], argv[3], argv[4]);
    } catch (...) { return 31; }
    return 4;
}
#endif
