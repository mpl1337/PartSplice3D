#include "worker_client.h"

#include "project_io.h"
#include "safe_temp.h"
#include "three_mf.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;
    explicit TemporaryDirectory(std::filesystem::path value) : path(std::move(value)) {}
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&& other) noexcept : path(std::move(other.path)) {
        other.path.clear();
    }
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
    ~TemporaryDirectory() {
        if (path.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

std::string readStatus(const std::filesystem::path& path) {
    std::error_code error;
    const uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > 64u * 1024u) return {};
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

#ifdef _WIN32
std::filesystem::path workerExecutable() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        throw std::runtime_error("Pfad der Anwendung konnte nicht ermittelt werden.");
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path() / L"PartSpliceWorker.exe";
}

std::wstring quoted(const std::filesystem::path& path) {
    const std::wstring value = path.wstring();
    if (value.find(L'"') != std::wstring::npos)
        throw std::runtime_error("Ein Worker-Pfad enthält ein unzulässiges Anführungszeichen.");
    return L"\"" + value + L"\"";
}

bool invokeWorker(std::wstring_view mode,
                  const std::filesystem::path& input,
                  const std::filesystem::path& output,
                  const std::filesystem::path& status,
                  const std::atomic_bool* cancelRequested,
                  std::string& error) {
    const std::filesystem::path executable = workerExecutable();
    if (!std::filesystem::is_regular_file(executable)) {
        error = "Die Installation ist unvollständig: PartSpliceWorker.exe fehlt neben "
                "PartSplice3D.exe. Bitte das vollständige portable ZIP entpacken und "
                "PartSplice3D.exe aus diesem Ordner starten.";
        return false;
    }
    std::wstring command = quoted(executable) + L" " + std::wstring(mode) + L" " +
        quoted(input) + L" " + quoted(output) + L" " + quoted(status);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        error = "Geometrie-Worker konnte nicht abgesichert werden (Job-Objekt).";
        return false;
    }
    struct JobGuard { HANDLE value; ~JobGuard() { if (value) CloseHandle(value); } } jobGuard{job};

    // Keep enough memory available for real-world CSG/STEP jobs while preventing
    // a malformed model from exhausting the entire machine. A fixed 4 GiB limit
    // was too small for large but otherwise valid meshes.
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    constexpr unsigned long long gibibyte = 1024ull * 1024ull * 1024ull;
    unsigned long long workerMemoryLimit = 4ull * gibibyte;
    if (GlobalMemoryStatusEx(&memory)) {
        workerMemoryLimit = std::clamp(
            memory.ullAvailPhys * 3ull / 4ull,
            2ull * gibibyte,
            12ull * gibibyte);
    }
    workerMemoryLimit = std::min<unsigned long long>(
        workerMemoryLimit, static_cast<unsigned long long>((std::numeric_limits<SIZE_T>::max)()));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                              JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(workerMemoryLimit);
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        error = "Speicherlimit des Geometrie-Workers konnte nicht gesetzt werden.";
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                        executable.parent_path().c_str(), &startup, &process)) {
        error = "Geometrie-Worker konnte nicht gestartet werden (Windows-Fehler " +
                std::to_string(GetLastError()) + ").";
        return false;
    }
    struct ProcessGuard {
        PROCESS_INFORMATION value;
        ~ProcessGuard() { CloseHandle(value.hThread); CloseHandle(value.hProcess); }
    } processGuard{process};
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        TerminateProcess(process.hProcess, 120);
        error = "Geometrie-Worker konnte dem Sicherheitslimit nicht zugeordnet werden.";
        return false;
    }
    ResumeThread(process.hThread);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    for (;;) {
        const DWORD wait = WaitForSingleObject(process.hProcess, 50);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) {
            TerminateJobObject(job, 121);
            error = "Warten auf den Geometrie-Worker ist fehlgeschlagen.";
            return false;
        }
        if ((cancelRequested && cancelRequested->load(std::memory_order_relaxed)) ||
            std::chrono::steady_clock::now() >= deadline) {
            TerminateJobObject(job, 122);
            error = cancelRequested && cancelRequested->load(std::memory_order_relaxed)
                ? "Vorgang abgebrochen." : "Geometrie-Worker nach 15 Minuten beendet.";
            return false;
        }
    }
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process.hProcess, &exitCode) || exitCode != 0) {
        error = readStatus(status);
        if (error.empty()) {
            if (exitCode == 0xC0000017u || exitCode == 0xC000009Au) {
                error = "Für dieses Modell war nicht genügend Arbeitsspeicher verfügbar.";
            } else {
                error = "Geometrie-Worker ist mit Exitcode " + std::to_string(exitCode) +
                        " fehlgeschlagen.";
            }
        }
        return false;
    }
    return true;
}
#endif

TemporaryDirectory makeWorkerDirectory() {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::temp_directory_path(error);
    if (error) throw std::runtime_error("Temporärer Worker-Ordner ist nicht verfügbar: " + error.message());
    return TemporaryDirectory(createSecureTemporaryDirectory(root, L"PartSpliceWorker-"));
}

}  // namespace

StepLoadResult loadStepIsolated(const std::filesystem::path& path,
                                const std::atomic_bool* cancelRequested) {
#ifndef _WIN32
    return loadStep(path, cancelRequested);
#else
    StepLoadResult result;
    try {
        TemporaryDirectory temporary = makeWorkerDirectory();
        const auto output = temporary.path / L"result.3mf";
        const auto status = temporary.path / L"status.txt";
        std::string error;
        if (!invokeWorker(L"--step-import", path, output, status, cancelRequested, error)) {
            result.message = error;
            return result;
        }
        ThreeMfLoadResult loaded = loadThreeMf(output, cancelRequested);
        if (!loaded.ok) {
            result.message = "Worker-Ergebnis konnte nicht gelesen werden: " + loaded.message;
            return result;
        }
        result.objects = std::move(loaded.objects);
        result.message = readStatus(status);
        if (result.message.empty()) result.message = "STEP-Datei im isolierten Worker geladen.";
        result.ok = true;
    } catch (const std::exception& exception) {
        result.message = exception.what();
    }
    return result;
#endif
}

DovetailResult createDovetailSplitIsolated(
    const TriangleMesh& source,
    const std::vector<Vec2>& cutPoints,
    const DovetailSettings& settings,
    const std::vector<ConnectorPlacement>& placements,
    bool requireConnector,
    const std::atomic_bool* cancelRequested) {
    const std::shared_ptr<const TriangleMesh> borrowed(
        &source, [](const TriangleMesh*) {});
    return createDovetailSplitIsolated(borrowed, cutPoints, settings, placements,
                                       requireConnector, cancelRequested);
}

DovetailResult createDovetailSplitIsolated(
    const std::shared_ptr<const TriangleMesh>& source,
    const std::vector<Vec2>& cutPoints,
    const DovetailSettings& settings,
    const std::vector<ConnectorPlacement>& placements,
    bool requireConnector,
    const std::atomic_bool* cancelRequested) {
    if (!source) {
        DovetailResult result;
        result.message = "Geometrie-Worker erhielt kein Quellmesh.";
        return result;
    }
#ifndef _WIN32
    return createDovetailSplit(*source, cutPoints, settings, placements, requireConnector);
#else
    DovetailResult result;
    try {
        TemporaryDirectory temporary = makeWorkerDirectory();
        const auto input = temporary.path / L"request.ps3d";
        const auto output = temporary.path / L"result.ps3d";
        const auto status = temporary.path / L"status.txt";
        PartSpliceProject request;
        request.settings = settings;
        request.cutPoints = cutPoints;
        request.cutPointLocked.assign(cutPoints.size(), false);
        request.placements = placements;
        request.cutAllActiveParts = requireConnector;
        ProjectObjectState object;
        object.name = "Worker";
        ProjectPartState sourcePart;
        sourcePart.id = 1;
        sourcePart.parentCutId = -1;
        sourcePart.label = "Quelle";
        sourcePart.active = true;
        sourcePart.meshReference = source;
        object.parts.push_back(std::move(sourcePart));
        object.selectedPartId = 1;
        object.nextPartId = 2;
        request.objects.push_back(std::move(object));
        std::string error;
        if (!savePartSpliceProject(input, request, error)) {
            result.message = "Worker-Anfrage konnte nicht geschrieben werden: " + error;
            return result;
        }
        if (!invokeWorker(L"--split", input, output, status, cancelRequested, error)) {
            result.message = error;
            return result;
        }
        ProjectLoadResult loaded = loadPartSpliceProject(output, cancelRequested);
        if (!loaded.ok || loaded.project.objects.size() != 1 ||
            loaded.project.objects.front().parts.size() != 2) {
            result.message = "Ungültiges Ergebnis des Geometrie-Workers.";
            return result;
        }
        result.partA = std::move(loaded.project.objects.front().parts[0].mesh);
        result.partB = std::move(loaded.project.objects.front().parts[1].mesh);
        DovetailSettings previewSettings = settings;
        previewSettings.validateInsideModel = false;
        result.connectors = makeConnectorPreview(*source, cutPoints, previewSettings, placements);
        const std::vector<uint8_t>& flags = loaded.project.workerConnectorFlags;
        if (flags.size() != result.connectors.size() * 2 ||
            std::any_of(flags.begin(), flags.end(), [](uint8_t value) { return value > 1; })) {
            result.message = "Geometrie-Worker lieferte unvollständige Verbinderinformationen.";
            return result;
        }
        for (size_t index = 0; index < result.connectors.size(); ++index) {
            result.connectors[index].valid = flags[index * 2] != 0;
            result.connectors[index].overlap = flags[index * 2 + 1] != 0;
        }
        result.message = readStatus(status);
        result.ok = true;
    } catch (const std::exception& exception) {
        result.message = exception.what();
    }
    return result;
#endif
}
