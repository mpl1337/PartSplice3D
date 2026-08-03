#include "export_workflow.h"

#include "app_persistence.h"
#include "file_dialogs.h"
#include "safe_temp.h"
#include "stl_io.h"
#include "three_mf.h"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <future>
#include <memory>
#include <unordered_set>
#include <utility>

namespace {
bool containsMultipleMaterialAssignments(const App& app) {
    std::unordered_set<uint32_t> slots;
    for (const ObjectSession& session : app.objectSessions) {
        for (const StoredPart& part : session.parts) {
            if (!part.active || !part.mesh) continue;
            const TriangleMesh& mesh = *part.mesh;
            slots.insert(std::max(mesh.defaultMaterial, 1u));
            for (uint32_t slot : mesh.triangleMaterials)
                slots.insert(slot == 0 ? std::max(mesh.defaultMaterial, 1u) : slot);
            if (slots.size() > 1) return true;
        }
    }
    return false;
}
}

std::wstring exportPartLetters(size_t index) {
    std::wstring letters;
    size_t value = index + 1;
    while (value > 0) {
        const size_t digit = (value - 1) % 26;
        letters.insert(letters.begin(), static_cast<wchar_t>(L'A' + digit));
        value = (value - 1) / 26;
    }
    return letters;
}


std::wstring sanitizeFileComponent(std::wstring value) {
    for (wchar_t& character : value) {
        if (character < 32 || character == L'<' || character == L'>' || character == L':' ||
            character == L'"' || character == L'/' || character == L'\\' || character == L'|' ||
            character == L'?' || character == L'*') character = L'_';
    }
    while (!value.empty() && (value.back() == L' ' || value.back() == L'.')) value.pop_back();
    size_t first = 0;
    while (first < value.size() && value[first] == L' ') ++first;
    value.erase(0, first);
    if (value.empty()) value = L"PartSplice";
    if (value.size() > 96) value.resize(96);

    std::wstring upper = value;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towupper(character));
    });
    const size_t dot = upper.find(L'.');
    const std::wstring device = upper.substr(0, dot);
    const bool reserved = device == L"CON" || device == L"PRN" || device == L"AUX" || device == L"NUL" ||
        (device.size() == 4 && (device.starts_with(L"COM") || device.starts_with(L"LPT")) &&
         device[3] >= L'1' && device[3] <= L'9');
    if (reserved) value.insert(value.begin(), L'_');
    return value;
}

std::filesystem::path uniqueExportPath(const std::filesystem::path& folder,
                                       const std::wstring& requestedStem,
                                       const std::wstring& extension,
                                       std::unordered_set<std::wstring>& usedNames) {
    const std::wstring stem = sanitizeFileComponent(requestedStem);
    for (size_t suffix = 1; suffix < 100000; ++suffix) {
        const std::wstring name = stem + (suffix == 1 ? L"" : L"_" + std::to_wstring(suffix)) + extension;
        std::wstring key = name;
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
            return static_cast<wchar_t>(std::towlower(character));
        });
        std::error_code error;
        const bool exists = std::filesystem::exists(folder / name, error);
        if (!error && !exists && usedNames.insert(key).second) return folder / name;
    }
    return {};
}

struct OwnedExportItem {
    std::filesystem::path finalPath;
    std::shared_ptr<const TriangleMesh> mesh;
};

BackgroundTaskResult runStlExport(std::vector<OwnedExportItem> items,
                                  std::filesystem::path folder,
                                  const std::atomic_bool* cancelRequested,
                                  const std::shared_ptr<BackgroundProgress>& progress) {
    BackgroundTaskResult task;
    task.kind = BackgroundTaskKind::Export;
    if (progress) {
        progress->completed.store(0, std::memory_order_relaxed);
        progress->total.store(items.size() * 2, std::memory_order_relaxed);
    }
    auto canceled = [&]() {
        return cancelRequested && cancelRequested->load(std::memory_order_relaxed);
    };
    if (canceled()) {
        task.canceled = true;
        task.message = "STL-Export abgebrochen; es wurden keine Dateien übernommen.";
        return task;
    }

    std::filesystem::path staging;
    try {
        staging = createSecureTemporaryDirectory(folder, L".partsplice-export-");
    } catch (const std::exception& exception) {
        task.message = "STL-Export fehlgeschlagen: Temporärer Exportordner konnte nicht erstellt werden: " +
                       std::string(exception.what());
        return task;
    }

    auto removeStaging = [&]() {
        std::error_code cleanupError;
        std::filesystem::remove_all(staging, cleanupError);
    };
    std::string saveError;
    for (const OwnedExportItem& item : items) {
        if (canceled()) {
            removeStaging();
            task.canceled = true;
            task.message = "STL-Export abgebrochen; es wurden keine Dateien übernommen.";
            return task;
        }
        if (!item.mesh || !saveBinaryStl(staging / item.finalPath.filename(), *item.mesh,
                                         saveError, cancelRequested)) {
            removeStaging();
            task.message = "STL-Export abgebrochen; es wurden keine Zieldateien übernommen: " + saveError;
            return task;
        }
        if (progress) progress->completed.fetch_add(1, std::memory_order_relaxed);
    }

    std::vector<std::filesystem::path> committed;
    committed.reserve(items.size());
    std::error_code filesystemError;
    for (const OwnedExportItem& item : items) {
        if (canceled()) {
            for (const std::filesystem::path& path : committed) {
                std::error_code cleanupError;
                std::filesystem::remove(path, cleanupError);
            }
            removeStaging();
            task.canceled = true;
            task.message = "STL-Export abgebrochen und vollständig zurückgerollt.";
            return task;
        }
        filesystemError.clear();
        std::filesystem::copy_file(staging / item.finalPath.filename(), item.finalPath,
                                   std::filesystem::copy_options::none, filesystemError);
        if (filesystemError) {
            const std::string commitError = filesystemError.message();
            for (const std::filesystem::path& path : committed) {
                std::error_code cleanupError;
                std::filesystem::remove(path, cleanupError);
            }
            removeStaging();
            task.message = "STL-Export vollständig zurückgerollt: " + commitError;
            return task;
        }
        committed.push_back(item.finalPath);
        if (progress) progress->completed.fetch_add(1, std::memory_order_relaxed);
    }
    removeStaging();
    task.ok = true;
    task.message = std::to_string(items.size()) + " einzelne STL-Dateien nach " +
                   pathToUtf8(folder) + " exportiert.";
    return task;
}

BackgroundTaskResult runThreeMfExport(std::filesystem::path target,
                                       std::vector<ThreeMfObject> objects,
                                       std::string projectSettings,
                                       std::vector<MeshMaterial> materials,
                                       const std::vector<uint8_t>* sourceArchive,
                                       const std::atomic_bool* cancelRequested) {
    BackgroundTaskResult task;
    task.kind = BackgroundTaskKind::Export;
    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
        task.canceled = true;
        task.message = "3MF-Export abgebrochen.";
        return task;
    }
    std::string error;
    task.ok = saveThreeMf(target, objects, error, projectSettings, {}, {}, {}, cancelRequested,
                          sourceArchive, &materials);
    if (!task.ok && cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
        task.canceled = true;
        task.message = "3MF-Export abgebrochen; die Zieldatei blieb unverändert.";
        return task;
    }
    task.message = task.ok
        ? std::to_string(objects.size()) + " Objekte in eine 3MF-Datei exportiert: " + pathToUtf8(target)
        : "3MF-Export fehlgeschlagen: " + error;
    return task;
}

std::wstring normalizedPathKeyNoThrow(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path normalized = std::filesystem::absolute(path, error);
    if (error) normalized = path;
    std::wstring value = normalized.lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

bool exportAllParts(App& app) {
    if (app.busy()) {
        app.status = "Es läuft bereits ein Vorgang.";
        return false;
    }
    if (app.pendingAppliedCutRebuildId >= 0) {
        app.status = "Export erst starten, nachdem ‚Neu schneiden‘ die geänderten Verbinder übernommen hat.";
        return false;
    }
    try {
        app.storeActiveObjectSession();
        if (app.exportFormat == ExportFormat::Stl && !app.colorLossExportConfirmed &&
            containsMultipleMaterialAssignments(app)) {
            app.showColorLossPrompt = true;
            app.status = "STL kann keine Farben oder Filamentzuweisungen speichern.";
            return false;
        }
        app.colorLossExportConfirmed = false;
        const std::wstring baseName = sanitizeFileComponent(
            app.sourcePath.empty() ? L"PartSplice" : app.sourcePath.stem().wstring());
        app.backgroundCancel = std::make_shared<std::atomic_bool>(false);
        const std::shared_ptr<std::atomic_bool> cancel = app.backgroundCancel;

        if (app.exportFormat == ExportFormat::Stl) {
            const auto folder = selectFolderDialog();
            if (!folder) {
                app.backgroundCancel.reset();
                app.status = "Export abgebrochen.";
                return false;
            }

            std::vector<OwnedExportItem> items;
            std::unordered_set<std::wstring> usedNames;
            for (const ObjectSession& session : app.objectSessions) {
                size_t ordinal = 0;
                for (const StoredPart& part : session.parts) {
                    if (!part.active || !part.mesh) continue;
                    std::wstring stem = baseName;
                    if (app.objectSessions.size() > 1) {
                        std::wstring sessionName = pathFromUtf8(session.name).stem().wstring();
                        stem += L"_" + sanitizeFileComponent(std::move(sessionName));
                    }
                    stem += L"_Teil" + exportPartLetters(ordinal++);
                    std::filesystem::path finalPath = uniqueExportPath(*folder, stem, L".stl", usedNames);
                    if (finalPath.empty()) {
                        app.backgroundCancel.reset();
                        app.status = "STL-Export fehlgeschlagen: Es konnte kein eindeutiger Dateiname erzeugt werden.";
                        return false;
                    }
                    items.push_back({std::move(finalPath), part.mesh});
                }
            }
            if (items.empty()) {
                app.backgroundCancel.reset();
                app.status = "STL-Export fehlgeschlagen: Es gibt keine aktiven Teile.";
                return false;
            }

            app.backgroundRunning = true;
            app.backgroundProgress = std::make_shared<BackgroundProgress>();
            app.backgroundProgress->total.store(items.size() * 2, std::memory_order_relaxed);
            app.backgroundStarted = std::chrono::steady_clock::now();
            app.backgroundKind = BackgroundTaskKind::Export;
            app.backgroundLabel = "STL-Dateien werden geprüft und atomar exportiert ...";
            app.status = app.backgroundLabel;
            app.backgroundFuture = std::async(std::launch::async,
                [items = std::move(items), folder = *folder, cancel,
                 progress = app.backgroundProgress]() mutable {
                    try {
                        return runStlExport(std::move(items), std::move(folder), cancel.get(), progress);
                    } catch (const std::bad_alloc&) {
                        BackgroundTaskResult result;
                        result.kind = BackgroundTaskKind::Export;
                        result.message = "STL-Export fehlgeschlagen: Nicht genügend Arbeitsspeicher.";
                        return result;
                    } catch (const std::exception& exception) {
                        BackgroundTaskResult result;
                        result.kind = BackgroundTaskKind::Export;
                        result.message = std::string("STL-Export fehlgeschlagen: ") + exception.what();
                        return result;
                    }
                });
            return true;
        }

        std::vector<ThreeMfObject> objects;
        for (const ObjectSession& session : app.objectSessions) {
            size_t activeCount = 0;
            for (const StoredPart& part : session.parts) if (part.active) ++activeCount;
            for (const StoredPart& part : session.parts) {
                if (!part.active || !part.mesh) continue;
                const std::string name = activeCount == 1
                    ? session.name : session.name + " - " + part.label;
                ThreeMfObject object;
                object.name = name;
                object.plateIndex = session.plateIndex;
                object.meshReference = part.mesh;
                objects.push_back(std::move(object));
            }
        }
        if (objects.empty()) {
            app.backgroundCancel.reset();
            app.status = "3MF-Export fehlgeschlagen: Es gibt keine aktiven Teile.";
            return false;
        }

        const std::wstring defaultName = baseName + L"_geschnitten.3mf";
        auto target = saveThreeMfDialog(defaultName.c_str());
        if (!target) {
            app.backgroundCancel.reset();
            app.status = "Export abgebrochen.";
            return false;
        }
        if (!app.sourcePath.empty() &&
            normalizedPathKeyNoThrow(*target) == normalizedPathKeyNoThrow(app.sourcePath)) {
            std::filesystem::path safe = app.sourcePath.parent_path() /
                (app.sourcePath.stem().wstring() + L"_geschnitten.3mf");
            int suffix = 2;
            for (;;) {
                std::error_code existsError;
                const bool exists = std::filesystem::exists(safe, existsError);
                if (existsError || !exists) break;
                safe = app.sourcePath.parent_path() /
                    (app.sourcePath.stem().wstring() + L"_geschnitten_" +
                     std::to_wstring(suffix++) + L".3mf");
            }
            target = safe;
        }

        app.backgroundRunning = true;
        app.backgroundProgress = std::make_shared<BackgroundProgress>();
        app.backgroundStarted = std::chrono::steady_clock::now();
        app.backgroundKind = BackgroundTaskKind::Export;
        app.backgroundLabel = "3MF-Datei wird geprüft und atomar exportiert ...";
        app.status = app.backgroundLabel;
        const std::string settings = app.sourceThreeMfProjectSettings;
        const std::vector<MeshMaterial> materials = app.materials;
        const bool sourceIsThreeMf = lowercaseExtension(pathFromUtf8(app.originalFileName)) == ".3mf";
        const std::vector<uint8_t>* sourceArchive = sourceIsThreeMf ? &app.originalFileData : nullptr;
        app.backgroundFuture = std::async(std::launch::async,
            [target = *target, objects = std::move(objects), settings, materials,
             cancel, sourceArchive]() mutable {
                try {
                    return runThreeMfExport(std::move(target), std::move(objects), settings, materials,
                                             sourceArchive, cancel.get());
                } catch (const std::bad_alloc&) {
                    BackgroundTaskResult result;
                    result.kind = BackgroundTaskKind::Export;
                    result.message = "3MF-Export fehlgeschlagen: Nicht genügend Arbeitsspeicher.";
                    return result;
                } catch (const std::exception& exception) {
                    BackgroundTaskResult result;
                    result.kind = BackgroundTaskKind::Export;
                    result.message = std::string("3MF-Export fehlgeschlagen: ") + exception.what();
                    return result;
                }
            });
        return true;
    } catch (const std::bad_alloc&) {
        app.backgroundRunning = false;
        app.backgroundKind = BackgroundTaskKind::None;
        app.backgroundLabel.clear();
        app.backgroundCancel.reset();
        app.status = "Export fehlgeschlagen: Nicht genügend Arbeitsspeicher.";
    } catch (const std::exception& exception) {
        app.backgroundRunning = false;
        app.backgroundKind = BackgroundTaskKind::None;
        app.backgroundLabel.clear();
        app.backgroundCancel.reset();
        app.status = std::string("Export fehlgeschlagen: ") + exception.what();
    } catch (...) {
        app.backgroundRunning = false;
        app.backgroundKind = BackgroundTaskKind::None;
        app.backgroundLabel.clear();
        app.backgroundCancel.reset();
        app.status = "Export ist mit einem unbekannten Fehler fehlgeschlagen.";
    }
    return false;
}
