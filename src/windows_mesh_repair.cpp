#include "windows_mesh_repair.h"

#include "three_mf.h"
#include "safe_temp.h"
#include "app_log.h"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <set>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <roapi.h>
#include <wrl/client.h>
#include <wrl/wrappers/corewrappers.h>
#include <winrt/robuffer.h>
#include <winrt/windows.graphics.printing3d.h>
#include <winrt/windows.storage.h>
#endif

const char* windowsRepairStageText(WindowsRepairStage stage) {
    switch (stage) {
        case WindowsRepairStage::Preparing: return "Temporäre 3MF wird vorbereitet.";
        case WindowsRepairStage::Initializing: return "Windows-3D-Schnittstelle wird initialisiert.";
        case WindowsRepairStage::Loading: return "Windows lädt das Modell.";
        case WindowsRepairStage::StartingRepair: return "Windows-Reparatur wird angefordert (RepairAsync).";
        case WindowsRepairStage::Repairing: return "Windows hat die Reparatur gestartet (RepairAsync).";
        case WindowsRepairStage::Saving: return "Windows-Reparatur beendet; Ergebnis wird gespeichert.";
        case WindowsRepairStage::Reading: return "Repariertes Ergebnis wird eingelesen.";
        case WindowsRepairStage::Indexing: return "Ergebnis wird für die Modellprüfung vorbereitet.";
    }
    return "Unbekannte Reparaturphase.";
}

namespace {
void reportStage(std::atomic<WindowsRepairStage>* progress, WindowsRepairStage stage) {
    if (progress) progress->store(stage, std::memory_order_relaxed);
    appLogInfo(windowsRepairStageText(stage));
}


#ifdef _WIN32
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HStringReference;

std::string hresultText(HRESULT value) {
    std::ostringstream stream;
    stream << "Windows-Fehler 0x" << std::hex << std::uppercase << static_cast<uint32_t>(value);
    return stream.str();
}

template <class T>
HRESULT waitFor(const ComPtr<T>& operation, const std::atomic_bool* cancelRequested,
                std::chrono::seconds timeout = std::chrono::seconds(120)) {
    ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
    HRESULT result = operation.As(&info);
    if (FAILED(result)) return result;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    AsyncStatus status = AsyncStatus::Started;
    while (status == AsyncStatus::Started) {
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) {
            info->Cancel();
            return E_ABORT;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            info->Cancel();
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        result = info->get_Status(&status);
        if (FAILED(result)) return result;
    }
    if (status == AsyncStatus::Completed) return S_OK;
    if (status == AsyncStatus::Canceled) return E_ABORT;
    HRESULT error = E_FAIL;
    result = info->get_ErrorCode(&error);
    if (FAILED(result)) return result;
    return FAILED(error) ? error : E_FAIL;
}

template <class T>
HRESULT activationFactory(const wchar_t* className, ComPtr<T>& factory) {
    return RoGetActivationFactory(HStringReference(className).Get(), IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
}

template <class T>
HRESULT activate(const wchar_t* className, ComPtr<T>& object) {
    ComPtr<IInspectable> inspectable;
    HRESULT result = RoActivateInstance(HStringReference(className).Get(), inspectable.GetAddressOf());
    if (FAILED(result)) return result;
    return inspectable.As(&object);
}

HRESULT openReadStream(const std::filesystem::path& path,
                       ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStream>& stream,
                       const std::atomic_bool* cancelRequested) {
    ComPtr<ABI::Windows::Storage::IStorageFileStatics> files;
    HRESULT result = activationFactory(L"Windows.Storage.StorageFile", files);
    if (FAILED(result)) return result;

    ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFile*>> fileOperation;
    result = files->GetFileFromPathAsync(HStringReference(std::filesystem::absolute(path).wstring().c_str()).Get(),
                                         fileOperation.GetAddressOf());
    if (FAILED(result)) return result;
    result = waitFor(fileOperation, cancelRequested);
    if (FAILED(result)) return result;

    ComPtr<ABI::Windows::Storage::IStorageFile> file;
    result = fileOperation->GetResults(file.GetAddressOf());
    if (FAILED(result)) return result;

    ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::Streams::IRandomAccessStream*>> streamOperation;
    result = file->OpenAsync(ABI::Windows::Storage::FileAccessMode_Read, streamOperation.GetAddressOf());
    if (FAILED(result)) return result;
    result = waitFor(streamOperation, cancelRequested);
    if (FAILED(result)) return result;
    return streamOperation->GetResults(stream.GetAddressOf());
}

HRESULT writeStreamToFile(ABI::Windows::Storage::Streams::IRandomAccessStream* randomStream,
                          const std::filesystem::path& path,
                          const std::atomic_bool* cancelRequested) {
    HRESULT result = randomStream->Seek(0);
    if (FAILED(result)) return result;
    ComPtr<ABI::Windows::Storage::Streams::IInputStream> input;
    result = randomStream->QueryInterface(IID_PPV_ARGS(input.GetAddressOf()));
    if (FAILED(result)) return result;

    ComPtr<ABI::Windows::Storage::Streams::IBufferFactory> buffers;
    result = activationFactory(L"Windows.Storage.Streams.Buffer", buffers);
    if (FAILED(result)) return result;

    std::ofstream output(path, std::ios::binary);
    if (!output) return E_ACCESSDENIED;
    for (;;) {
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed)) return E_ABORT;
        ComPtr<ABI::Windows::Storage::Streams::IBuffer> buffer;
        result = buffers->Create(1024 * 1024, buffer.GetAddressOf());
        if (FAILED(result)) return result;
        ComPtr<ABI::Windows::Foundation::IAsyncOperationWithProgress<
            ABI::Windows::Storage::Streams::IBuffer*, UINT32>> readOperation;
        result = input->ReadAsync(buffer.Get(), 1024 * 1024,
                                  ABI::Windows::Storage::Streams::InputStreamOptions_ReadAhead,
                                  readOperation.GetAddressOf());
        if (FAILED(result)) return result;
        result = waitFor(readOperation, cancelRequested);
        if (FAILED(result)) return result;
        ComPtr<ABI::Windows::Storage::Streams::IBuffer> readBuffer;
        result = readOperation->GetResults(readBuffer.GetAddressOf());
        if (FAILED(result)) return result;
        UINT32 length = 0;
        result = readBuffer->get_Length(&length);
        if (FAILED(result)) return result;
        if (length == 0) break;
        ComPtr<Windows::Storage::Streams::IBufferByteAccess> bytes;
        result = readBuffer.As(&bytes);
        if (FAILED(result)) return result;
        byte* data = nullptr;
        result = bytes->Buffer(&data);
        if (FAILED(result)) return result;
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
        if (!output) return E_FAIL;
    }
    output.flush();
    return output ? S_OK : E_FAIL;
}

HRESULT repairThreeMf(const std::filesystem::path& inputPath, const std::filesystem::path& outputPath,
                      const std::atomic_bool* cancelRequested,
                      std::atomic<WindowsRepairStage>* progress) {
    reportStage(progress, WindowsRepairStage::Loading);
    ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStream> inputStream;
    HRESULT result = openReadStream(inputPath, inputStream, cancelRequested);
    if (FAILED(result)) return result;

    ComPtr<ABI::Windows::Graphics::Printing3D::IPrinting3D3MFPackage> package;
    result = activate(L"Windows.Graphics.Printing3D.Printing3D3MFPackage", package);
    if (FAILED(result)) return result;

    ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Graphics::Printing3D::Printing3DModel*>> loadOperation;
    result = package->LoadModelFromPackageAsync(inputStream.Get(), loadOperation.GetAddressOf());
    if (FAILED(result)) return result;
    result = waitFor(loadOperation, cancelRequested);
    if (FAILED(result)) return result;
    ComPtr<ABI::Windows::Graphics::Printing3D::IPrinting3DModel> model;
    result = loadOperation->GetResults(model.GetAddressOf());
    if (FAILED(result)) return result;

    ComPtr<ABI::Windows::Foundation::IAsyncAction> repairOperation;
    reportStage(progress, WindowsRepairStage::StartingRepair);
    result = model->RepairAsync(repairOperation.GetAddressOf());
    if (FAILED(result)) return result;
    reportStage(progress, WindowsRepairStage::Repairing);
    result = waitFor(repairOperation, cancelRequested);
    if (FAILED(result)) return result;
    result = repairOperation->GetResults();
    if (FAILED(result)) return result;

    reportStage(progress, WindowsRepairStage::Saving);
    ComPtr<ABI::Windows::Foundation::IAsyncAction> packageOperation;
    result = package->SaveModelToPackageAsync(model.Get(), packageOperation.GetAddressOf());
    if (FAILED(result)) return result;
    result = waitFor(packageOperation, cancelRequested);
    if (FAILED(result)) return result;
    result = packageOperation->GetResults();
    if (FAILED(result)) return result;

    ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::Streams::IRandomAccessStream*>> saveOperation;
    result = package->SaveAsync(saveOperation.GetAddressOf());
    if (FAILED(result)) return result;
    result = waitFor(saveOperation, cancelRequested);
    if (FAILED(result)) return result;
    ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStream> outputStream;
    result = saveOperation->GetResults(outputStream.GetAddressOf());
    if (FAILED(result)) return result;
    return writeStreamToFile(outputStream.Get(), outputPath, cancelRequested);
}
#endif

TriangleMesh combineObjects(const std::vector<ThreeMfObject>& objects) {
    TriangleMesh combined;
    size_t vertexCount = 0;
    size_t triangleCount = 0;
    for (const auto& object : objects) {
        vertexCount += object.mesh.vertices.size();
        triangleCount += object.mesh.triangles.size();
    }
    combined.vertices.reserve(vertexCount);
    combined.triangles.reserve(triangleCount);
    for (const auto& object : objects) {
        const TriangleMesh& source = object.meshData();
        const size_t oldTriangleCount = combined.triangles.size();
        if (combined.vertices.empty()) combined.defaultMaterial = std::max(source.defaultMaterial, 1u);
        const bool needsExplicit = !source.triangleMaterials.empty() ||
                                   source.defaultMaterial != combined.defaultMaterial;
        const bool preserveExplicitMaterials = !combined.triangleMaterials.empty() || needsExplicit;
        if (combined.triangleMaterials.empty() && needsExplicit)
            combined.triangleMaterials.assign(oldTriangleCount, combined.defaultMaterial);
        const uint32_t offset = static_cast<uint32_t>(combined.vertices.size());
        for (const Vec3& vertex : source.vertices) {
            combined.vertices.push_back(vertex);
            combined.bounds.expand(vertex);
        }
        for (size_t triangleIndex = 0; triangleIndex < source.triangles.size(); ++triangleIndex) {
            const auto& triangle = source.triangles[triangleIndex];
            combined.triangles.push_back({triangle[0] + offset, triangle[1] + offset, triangle[2] + offset});
            if (preserveExplicitMaterials) {
                uint32_t material = source.triangleMaterials.empty()
                    ? source.defaultMaterial : source.triangleMaterials[triangleIndex];
                if (material == 0) material = source.defaultMaterial;
                combined.triangleMaterials.push_back(std::max(material, 1u));
            }
        }
    }
    return combined;
}

} // namespace

WindowsMeshRepairResult repairMeshWithWindowsService(
    const TriangleMesh& mesh, const std::atomic_bool* cancelRequested,
    std::atomic<WindowsRepairStage>* progress) {
    WindowsMeshRepairResult response;
    reportStage(progress, WindowsRepairStage::Preparing);
    try {
#ifdef _WIN32
    const std::filesystem::path temporaryDirectory = std::filesystem::temp_directory_path();
    const std::filesystem::path inputPath = reserveSecureTemporaryFile(
        temporaryDirectory, L"PartSplice3D-Reparatur-eingang-", L".3mf");
    const std::filesystem::path outputPath = reserveSecureTemporaryFile(
        temporaryDirectory, L"PartSplice3D-Reparatur-ausgang-", L".3mf");
    struct Cleanup {
        std::filesystem::path input;
        std::filesystem::path output;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(input, ignored);
            std::filesystem::remove(output, ignored);
        }
    } cleanup{inputPath, outputPath};

    std::string error;
    if (!saveThreeMf(inputPath, {{"Zu reparierendes Modell", mesh}}, error)) {
        response.message = "Temporäre 3MF konnte nicht erzeugt werden: " + error;
        return response;
    }

    // The Windows repair API expects to create its destination itself. Its
    // random path was reserved above and is released only immediately before
    // handing it to the service.
    std::error_code removeError;
    if (!std::filesystem::remove(outputPath, removeError) || removeError) {
        response.message = "Temporäre Ausgabedatei konnte nicht vorbereitet werden: " + removeError.message();
        return response;
    }

    reportStage(progress, WindowsRepairStage::Initializing);
    const HRESULT initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized)) {
        response.message = "Windows-3D-Dienst konnte nicht initialisiert werden: " + hresultText(initialized);
        return response;
    }
    struct ApartmentGuard { ~ApartmentGuard() { RoUninitialize(); } } apartmentGuard;
    const HRESULT repaired = repairThreeMf(inputPath, outputPath, cancelRequested, progress);
    if (FAILED(repaired)) {
        response.canceled = repaired == E_ABORT;
        if (repaired == E_ABORT)
            response.message = "Windows-3D-Reparatur wurde abgebrochen.";
        else if (repaired == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
            response.message = "Windows-3D-Reparatur wurde nach 120 Sekunden wegen Zeitüberschreitung abgebrochen.";
        else
            response.message = "Windows-3D-Reparatur fehlgeschlagen: " + hresultText(repaired);
        return response;
    }

    reportStage(progress, WindowsRepairStage::Reading);
    ThreeMfLoadResult loaded = loadThreeMf(outputPath);
    if (!loaded.ok || loaded.objects.empty()) {
        response.message = "Repariertes 3MF-Ergebnis konnte nicht gelesen werden: " + loaded.message;
        return response;
    }
    response.mesh = combineObjects(loaded.objects);
    if (response.mesh.triangles.empty()) {
        response.message = "Der Windows-3D-Dienst lieferte kein verwendbares Objekt.";
        return response;
    }
    response.ok = true;
    response.message = "Windows-3D-Reparatur abgeschlossen.";
    std::set<uint32_t> beforeMaterials;
    std::set<uint32_t> afterMaterials;
    auto collect = [](const TriangleMesh& value, std::set<uint32_t>& target) {
        target.insert(std::max(value.defaultMaterial, 1u));
        for (uint32_t material : value.triangleMaterials)
            target.insert(material == 0 ? std::max(value.defaultMaterial, 1u) : material);
    };
    collect(mesh, beforeMaterials);
    collect(response.mesh, afterMaterials);
    if (beforeMaterials.size() == 1 && afterMaterials.size() == 1)
        response.mesh.defaultMaterial = *beforeMaterials.begin();
    else if (beforeMaterials != afterMaterials)
        response.message += " Achtung: Der Windows-Dienst hat Farb-/Filamentzuweisungen verändert; bitte das Ergebnis prüfen.";
#else
    (void)mesh;
    (void)cancelRequested;
    response.message = "Der Windows-3D-Reparaturdienst ist nur unter Windows verfügbar.";
#endif
    } catch (const std::bad_alloc&) {
        response.message = "Nicht genügend Arbeitsspeicher für die Windows-3D-Reparatur.";
    } catch (const std::exception& exception) {
        response.message = std::string("Windows-3D-Reparatur ist mit einer Ausnahme fehlgeschlagen: ") + exception.what();
    }
    return response;
}
