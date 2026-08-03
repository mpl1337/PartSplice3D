#include "project_io.h"
#include "safe_temp.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr std::array<char, 8> kMagic{{'P', 'S', '3', 'D', 'P', 'R', 'J', '1'}};
constexpr uint32_t kVersion = 8;
// A project is deserialized into several in-memory representations.  Keep the
// on-disk limits well below the address-space limit so a crafted file cannot
// force multi-gigabyte allocations before the user can cancel the operation.
constexpr uint64_t kMaxProjectBytes = 512ull * 1024ull * 1024ull;
constexpr uint64_t kMaxString = 1ull * 1024ull * 1024ull;
constexpr uint64_t kMaxBlob = 384ull * 1024ull * 1024ull;
constexpr uint64_t kMaxObjects = 256;
constexpr uint64_t kMaxPartsPerObject = 5'000;
constexpr uint64_t kMaxCutsPerObject = 5'000;
constexpr uint64_t kMaxVerticesPerMesh = 5'000'000;
constexpr uint64_t kMaxTrianglesPerMesh = 10'000'000;
constexpr uint64_t kMaxPointsPerCut = 1'000;
constexpr uint64_t kMaxConnectorsPerCut = 100;
constexpr uint64_t kMaxChildrenPerCut = 4'096;
constexpr uint64_t kMaxMaterials = 256;
constexpr uint64_t kMaxCompatibilityWarnings = 256;
constexpr double kMaxCoordinateMagnitude = 1'000'000'000.0;
constexpr uint64_t kSerializedPlacementBytesV2 = 74;

class OperationCanceled final : public std::runtime_error {
public:
    OperationCanceled() : std::runtime_error("Vorgang abgebrochen.") {}
};

bool finite(double value) { return std::isfinite(value); }
bool finite(Vec2 value) { return finite(value.x) && finite(value.y); }
bool finite(Vec3 value) { return finite(value.x) && finite(value.y) && finite(value.z); }
bool coordinateInRange(Vec2 value) {
    return finite(value) && std::abs(value.x) <= kMaxCoordinateMagnitude &&
           std::abs(value.y) <= kMaxCoordinateMagnitude;
}
bool coordinateInRange(Vec3 value) {
    return finite(value) && std::abs(value.x) <= kMaxCoordinateMagnitude &&
           std::abs(value.y) <= kMaxCoordinateMagnitude &&
           std::abs(value.z) <= kMaxCoordinateMagnitude;
}

bool validConnectorType(ConnectorType type) {
    const int value = static_cast<int>(type);
    return value >= static_cast<int>(ConnectorType::Dovetail) &&
           value <= static_cast<int>(ConnectorType::RoundPin);
}

bool validDistribution(DovetailSettings::Distribution distribution) {
    const int value = static_cast<int>(distribution);
    return value == static_cast<int>(DovetailSettings::Distribution::Count) ||
           value == static_cast<int>(DovetailSettings::Distribution::Spacing);
}

bool validUtf8(const std::string& value) {
    size_t index = 0;
    while (index < value.size()) {
        const unsigned char first = static_cast<unsigned char>(value[index++]);
        if (first <= 0x7f) {
            if (first == 0) return false;
            continue;
        }
        int continuationCount = 0;
        uint32_t codePoint = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            continuationCount = 1;
            codePoint = first & 0x1f;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuationCount = 2;
            codePoint = first & 0x0f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuationCount = 3;
            codePoint = first & 0x07;
        } else {
            return false;
        }
        if (index + static_cast<size_t>(continuationCount) > value.size()) return false;
        for (int n = 0; n < continuationCount; ++n) {
            const unsigned char next = static_cast<unsigned char>(value[index++]);
            if ((next & 0xc0) != 0x80) return false;
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if ((continuationCount == 2 && codePoint < 0x800) ||
            (continuationCount == 3 && codePoint < 0x10000) ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff) || codePoint > 0x10ffff) return false;
    }
    return true;
}

class Writer {
public:
    explicit Writer(const std::filesystem::path& path)
        : output(path, std::ios::binary | std::ios::trunc) {}
    bool good() const { return static_cast<bool>(output); }

    template <class T>
    void pod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
    void boolean(bool value) { pod(static_cast<uint8_t>(value ? 1 : 0)); }
    void text(const std::string& value) {
        pod(static_cast<uint64_t>(value.size()));
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
    }
    void blob(const std::vector<uint8_t>& value) {
        pod(static_cast<uint64_t>(value.size()));
        if (!value.empty())
            output.write(reinterpret_cast<const char*>(value.data()),
                         static_cast<std::streamsize>(value.size()));
    }
    std::ofstream output;
};

class Reader {
public:
    explicit Reader(const std::filesystem::path& path, const std::atomic_bool* cancel)
        : input(path, std::ios::binary), cancelRequested(cancel) {
        checkCanceled();
        if (!input) { ok = false; return; }
        input.seekg(0, std::ios::end);
        const std::streamoff end = input.tellg();
        if (end < 0 || static_cast<uint64_t>(end) > kMaxProjectBytes) {
            ok = false;
            return;
        }
        fileSize = static_cast<uint64_t>(end);
        input.seekg(0, std::ios::beg);
        if (!input) ok = false;
    }

    void checkCanceled() const {
        if (cancelRequested && cancelRequested->load(std::memory_order_relaxed))
            throw OperationCanceled();
    }

    bool good() const { checkCanceled(); return ok && static_cast<bool>(input); }

    uint64_t remaining() {
        checkCanceled();
        if (!good()) return 0;
        const std::streamoff position = input.tellg();
        if (position < 0 || static_cast<uint64_t>(position) > fileSize) {
            ok = false;
            return 0;
        }
        return fileSize - static_cast<uint64_t>(position);
    }

    bool canRead(uint64_t bytes) {
        checkCanceled();
        if (!good() || bytes > remaining()) {
            ok = false;
            return false;
        }
        return true;
    }

    template <class T>
    T pod() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (!canRead(sizeof(T))) return value;
        input.read(reinterpret_cast<char*>(&value), sizeof(value));
        if (!input) ok = false;
        return value;
    }

    bool boolean() {
        const uint8_t value = pod<uint8_t>();
        if (value > 1) ok = false;
        return value != 0;
    }

    uint64_t count(uint64_t maximum, uint64_t minimumBytesPerItem = 0) {
        const uint64_t value = pod<uint64_t>();
        if (!ok || value > maximum) { ok = false; return 0; }
        if (minimumBytesPerItem != 0 &&
            (value > std::numeric_limits<uint64_t>::max() / minimumBytesPerItem ||
             value * minimumBytesPerItem > remaining())) {
            ok = false;
            return 0;
        }
        return value;
    }

    std::string text(uint64_t maximum = kMaxString) {
        const uint64_t size = count(maximum, 1);
        if (!ok) return {};
        std::string value(static_cast<size_t>(size), '\0');
        if (size != 0) input.read(value.data(), static_cast<std::streamsize>(size));
        if (!input) ok = false;
        return value;
    }

    std::vector<uint8_t> blob(uint64_t maximum = kMaxBlob) {
        const uint64_t size = count(maximum, 1);
        if (!ok) return {};
        std::vector<uint8_t> value(static_cast<size_t>(size));
        if (size != 0)
            input.read(reinterpret_cast<char*>(value.data()), static_cast<std::streamsize>(size));
        if (!input) ok = false;
        return value;
    }

    std::ifstream input;
    bool ok = true;
    uint64_t fileSize = 0;
    const std::atomic_bool* cancelRequested = nullptr;
};

void writeVec2(Writer& out, const Vec2& value) { out.pod(value.x); out.pod(value.y); }
Vec2 readVec2(Reader& in) { return {in.pod<double>(), in.pod<double>()}; }
void writeVec3(Writer& out, const Vec3& value) { out.pod(value.x); out.pod(value.y); out.pod(value.z); }
Vec3 readVec3(Reader& in) { return {in.pod<double>(), in.pod<double>(), in.pod<double>()}; }

void writeSettings(Writer& out, const DovetailSettings& value) {
    out.pod(static_cast<int32_t>(value.distribution));
    out.pod(static_cast<int32_t>(value.count));
    out.pod(value.spacing); out.pod(value.endMargin); out.pod(value.neckWidth);
    out.pod(value.headWidth); out.pod(value.depth); out.pod(value.embed); out.pod(value.clearance);
    out.boolean(value.maleOnLeft); out.boolean(value.validateInsideModel);
    out.pod(static_cast<int32_t>(value.type));
    out.boolean(value.leadChamfer); out.pod(value.chamferWidth); out.pod(value.chamferAngle);
    out.boolean(value.validateWallThickness); out.pod(value.minimumWall);
    out.boolean(value.assemblyMarks); out.pod(static_cast<int32_t>(value.assemblyMarkCode));
    out.pod(value.assemblyMarkPosition); out.pod(value.assemblyMarkSize);
    out.pod(value.assemblyMarkDepth);
    out.boolean(value.assemblyMarkPositionsCustom);
    out.pod(value.assemblyMarkLeftX); out.pod(value.assemblyMarkLeftY);
    out.pod(value.assemblyMarkRightX); out.pod(value.assemblyMarkRightY);
}

DovetailSettings readSettings(Reader& in, uint32_t version) {
    DovetailSettings value;
    // Versions 1-3 did not store this option. Preserve their historical
    // behavior even though new projects now enable engravings by default.
    if (version < 4) value.assemblyMarks = false;
    value.distribution = static_cast<DovetailSettings::Distribution>(in.pod<int32_t>());
    value.count = in.pod<int32_t>();
    value.spacing = in.pod<double>(); value.endMargin = in.pod<double>(); value.neckWidth = in.pod<double>();
    value.headWidth = in.pod<double>(); value.depth = in.pod<double>(); value.embed = in.pod<double>();
    value.clearance = in.pod<double>(); value.maleOnLeft = in.boolean();
    value.validateInsideModel = in.boolean(); value.type = static_cast<ConnectorType>(in.pod<int32_t>());
    if (version >= 2) {
        value.leadChamfer = in.boolean();
        value.chamferWidth = in.pod<double>();
        value.chamferAngle = in.pod<double>();
    }
    if (version >= 4) {
        value.validateWallThickness = in.boolean();
        value.minimumWall = in.pod<double>();
        value.assemblyMarks = in.boolean();
        value.assemblyMarkCode = in.pod<int32_t>();
    }
    if (version >= 6) {
        value.assemblyMarkPosition = in.pod<double>();
        value.assemblyMarkSize = in.pod<double>();
        value.assemblyMarkDepth = in.pod<double>();
    }
    if (version >= 7) {
        value.assemblyMarkPositionsCustom = in.boolean();
        value.assemblyMarkLeftX = in.pod<double>();
        value.assemblyMarkLeftY = in.pod<double>();
        value.assemblyMarkRightX = in.pod<double>();
        value.assemblyMarkRightY = in.pod<double>();
    }
    return value;
}

void writePlacement(Writer& out, const ConnectorPlacement& value) {
    out.pod(value.position); out.boolean(value.maleOnLeft); out.boolean(value.locked);
    out.pod(static_cast<int32_t>(value.type)); out.pod(value.neckWidth); out.pod(value.headWidth);
    out.pod(value.depth); out.pod(value.embed); out.pod(value.clearance);
    out.pod(static_cast<int32_t>(value.leadChamfer));
    out.pod(value.chamferWidth); out.pod(value.chamferAngle);
}

ConnectorPlacement readPlacement(Reader& in, uint32_t version) {
    ConnectorPlacement value;
    value.position = in.pod<double>(); value.maleOnLeft = in.boolean(); value.locked = in.boolean();
    value.type = static_cast<ConnectorType>(in.pod<int32_t>()); value.neckWidth = in.pod<double>();
    value.headWidth = in.pod<double>(); value.depth = in.pod<double>(); value.embed = in.pod<double>();
    value.clearance = in.pod<double>();
    if (version >= 2) {
        value.leadChamfer = in.pod<int32_t>();
        value.chamferWidth = in.pod<double>();
        value.chamferAngle = in.pod<double>();
    }
    return value;
}

void writeMesh(Writer& out, const TriangleMesh& mesh) {
    out.pod(static_cast<uint64_t>(mesh.vertices.size()));
    for (const Vec3& vertex : mesh.vertices) writeVec3(out, vertex);
    out.pod(static_cast<uint64_t>(mesh.triangles.size()));
    for (const auto& triangle : mesh.triangles) {
        out.pod(triangle[0]); out.pod(triangle[1]); out.pod(triangle[2]);
    }
    out.pod(mesh.defaultMaterial);
    out.pod(static_cast<uint64_t>(mesh.triangleMaterials.size()));
    for (const uint32_t material : mesh.triangleMaterials) out.pod(material);
}

TriangleMesh readMesh(Reader& in, uint32_t version) {
    TriangleMesh mesh;
    const uint64_t vertices = in.count(kMaxVerticesPerMesh, sizeof(double) * 3);
    if (!in.good()) return mesh;
    mesh.vertices.reserve(static_cast<size_t>(vertices));
    for (uint64_t index = 0; index < vertices && in.good(); ++index) {
        const Vec3 vertex = readVec3(in);
        if (!coordinateInRange(vertex)) in.ok = false;
        mesh.vertices.push_back(vertex);
        if (coordinateInRange(vertex)) mesh.bounds.expand(vertex);
    }
    const uint64_t triangles = in.count(kMaxTrianglesPerMesh, sizeof(uint32_t) * 3);
    if (!in.good()) return mesh;
    mesh.triangles.reserve(static_cast<size_t>(triangles));
    for (uint64_t index = 0; index < triangles && in.good(); ++index) {
        std::array<uint32_t, 3> triangle{{in.pod<uint32_t>(), in.pod<uint32_t>(), in.pod<uint32_t>()}};
        if (triangle[0] >= mesh.vertices.size() || triangle[1] >= mesh.vertices.size() ||
            triangle[2] >= mesh.vertices.size()) in.ok = false;
        mesh.triangles.push_back(triangle);
    }
    if (version >= 8) {
        mesh.defaultMaterial = in.pod<uint32_t>();
        const uint64_t materials = in.count(kMaxTrianglesPerMesh, sizeof(uint32_t));
        if (in.good()) mesh.triangleMaterials.reserve(static_cast<size_t>(materials));
        for (uint64_t index = 0; index < materials && in.good(); ++index)
            mesh.triangleMaterials.push_back(in.pod<uint32_t>());
    }
    return mesh;
}

template <class T, class WriteItem>
void writeVector(Writer& out, const std::vector<T>& values, WriteItem writeItem) {
    out.pod(static_cast<uint64_t>(values.size()));
    for (const T& value : values) writeItem(value);
}

template <class T, class ReadItem>
std::vector<T> readVector(Reader& in, uint64_t maximum, uint64_t minimumBytesPerItem,
                          ReadItem readItem) {
    const uint64_t size = in.count(maximum, minimumBytesPerItem);
    std::vector<T> values;
    if (!in.good()) return values;
    values.reserve(static_cast<size_t>(size));
    for (uint64_t index = 0; index < size && in.good(); ++index) values.push_back(readItem());
    return values;
}

void writeCut(Writer& out, const ProjectCutState& cut) {
    out.pod(static_cast<int32_t>(cut.id)); out.pod(static_cast<int32_t>(cut.sourcePartId));
    out.text(cut.sourceLabel);
    writeVector(out, cut.childPartIds, [&](int value) { out.pod(static_cast<int32_t>(value)); });
    writeVector(out, cut.childLabels, [&](const std::string& value) { out.text(value); });
    writeVector(out, cut.points, [&](const Vec2& value) { writeVec2(out, value); });
    out.pod(static_cast<uint64_t>(cut.pointLocked.size()));
    for (bool value : cut.pointLocked) out.boolean(value);
    writeVector(out, cut.placements, [&](const ConnectorPlacement& value) { writePlacement(out, value); });
    writeSettings(out, cut.settings); out.pod(static_cast<int32_t>(cut.mode));
    out.boolean(cut.cutAllActiveParts); out.boolean(cut.forcedInvalidConnectors);
}

ProjectCutState readCut(Reader& in, uint32_t version) {
    ProjectCutState cut;
    cut.id = in.pod<int32_t>(); cut.sourcePartId = in.pod<int32_t>(); cut.sourceLabel = in.text();
    cut.childPartIds = readVector<int>(in, kMaxChildrenPerCut, sizeof(int32_t),
        [&]() { return static_cast<int>(in.pod<int32_t>()); });
    cut.childLabels = readVector<std::string>(in, kMaxChildrenPerCut, sizeof(uint64_t),
        [&]() { return in.text(); });
    cut.points = readVector<Vec2>(in, kMaxPointsPerCut, sizeof(double) * 2,
        [&]() { return readVec2(in); });
    const uint64_t locks = in.count(kMaxPointsPerCut, sizeof(uint8_t));
    if (in.good()) cut.pointLocked.reserve(static_cast<size_t>(locks));
    for (uint64_t index = 0; index < locks && in.good(); ++index) cut.pointLocked.push_back(in.boolean());
    cut.placements = readVector<ConnectorPlacement>(in, kMaxConnectorsPerCut, 3,
        [&]() { return readPlacement(in, version); });
    cut.settings = readSettings(in, version); cut.mode = in.pod<int32_t>();
    cut.cutAllActiveParts = in.boolean(); cut.forcedInvalidConnectors = in.boolean();
    return cut;
}

void writeProject(Writer& out, const PartSpliceProject& project) {
    out.output.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    out.pod(kVersion);
    out.text(project.originalPath); out.text(project.originalFileName); out.blob(project.originalFileData);
    out.text(project.sourceThreeMfProjectSettings);
    writeVector(out, project.materials, [&](const MeshMaterial& material) {
        out.text(material.name); out.text(material.color);
    });
    writeVector(out, project.threeMfCompatibilityWarnings,
                [&](const std::string& warning) { out.text(warning); });
    out.pod(static_cast<uint64_t>(project.objects.size()));
    for (const ProjectObjectState& object : project.objects) {
        out.text(object.name); out.pod(static_cast<int32_t>(object.plateIndex));
        out.pod(static_cast<uint64_t>(object.parts.size()));
        for (const ProjectPartState& part : object.parts) {
            out.pod(static_cast<int32_t>(part.id)); out.pod(static_cast<int32_t>(part.parentCutId));
            out.text(part.label); writeMesh(out, part.meshData()); out.boolean(part.active);
        }
        writeVector(out, object.cuts, [&](const ProjectCutState& cut) { writeCut(out, cut); });
        out.pod(static_cast<int32_t>(object.selectedPartId));
        out.pod(static_cast<int32_t>(object.nextPartId)); out.pod(static_cast<int32_t>(object.nextCutId));
    }
    out.pod(static_cast<int32_t>(project.activeObjectIndex)); out.pod(static_cast<int32_t>(project.exportFormat));
    out.boolean(project.drawingLine); out.pod(static_cast<int32_t>(project.cutMode));
    writeVector(out, project.cutPoints, [&](const Vec2& value) { writeVec2(out, value); });
    out.pod(static_cast<uint64_t>(project.cutPointLocked.size()));
    for (bool value : project.cutPointLocked) out.boolean(value);
    writeVector(out, project.placements, [&](const ConnectorPlacement& value) { writePlacement(out, value); });
    writeSettings(out, project.settings); out.boolean(project.cutAllActiveParts);
    out.boolean(project.showSource); out.boolean(project.wireframe); out.boolean(project.showPrintBed);
    out.pod(project.printBedWidth); out.pod(project.printBedDepth); writeVec2(out, project.printBedCenter);
    out.pod(project.printBedRotation); out.pod(static_cast<int32_t>(project.printBedPreset));
    out.pod(project.customBedWidth); out.pod(project.customBedDepth);
    out.boolean(project.cameraTop); out.pod(project.cameraYaw); out.pod(project.cameraPitch);
    out.pod(project.cameraOrbitDistance); out.pod(project.cameraTopHeight); writeVec2(out, project.cameraPan);
    out.pod(project.modelTransparency);
    out.blob(project.workerConnectorFlags);
}

PartSpliceProject readProject(Reader& in, uint32_t version) {
    PartSpliceProject project;
    project.originalPath = in.text(); project.originalFileName = in.text();
    project.originalFileData = in.blob(); project.sourceThreeMfProjectSettings = in.text(64ull * 1024ull * 1024ull);
    if (version >= 8) {
        project.materials = readVector<MeshMaterial>(in, kMaxMaterials, sizeof(uint64_t) * 2,
            [&]() { return MeshMaterial{in.text(), in.text()}; });
        project.threeMfCompatibilityWarnings = readVector<std::string>(
            in, kMaxCompatibilityWarnings, sizeof(uint64_t), [&]() { return in.text(); });
    }
    const uint64_t objects = in.count(kMaxObjects, 32);
    if (in.good()) project.objects.reserve(static_cast<size_t>(objects));
    for (uint64_t objectIndex = 0; objectIndex < objects && in.good(); ++objectIndex) {
        ProjectObjectState object;
        object.name = in.text(); object.plateIndex = in.pod<int32_t>();
        const uint64_t parts = in.count(kMaxPartsPerObject, 32);
        if (in.good()) object.parts.reserve(static_cast<size_t>(parts));
        for (uint64_t partIndex = 0; partIndex < parts && in.good(); ++partIndex) {
            ProjectPartState part;
            part.id = in.pod<int32_t>(); part.parentCutId = in.pod<int32_t>();
            part.label = in.text(); part.mesh = readMesh(in, version); part.active = in.boolean();
            object.parts.push_back(std::move(part));
        }
        object.cuts = readVector<ProjectCutState>(in, kMaxCutsPerObject, 32,
            [&]() { return readCut(in, version); });
        object.selectedPartId = in.pod<int32_t>(); object.nextPartId = in.pod<int32_t>();
        object.nextCutId = in.pod<int32_t>(); project.objects.push_back(std::move(object));
    }
    project.activeObjectIndex = in.pod<int32_t>(); project.exportFormat = in.pod<int32_t>();
    project.drawingLine = in.boolean(); project.cutMode = in.pod<int32_t>();
    project.cutPoints = readVector<Vec2>(in, kMaxPointsPerCut, sizeof(double) * 2,
        [&]() { return readVec2(in); });
    const uint64_t locks = in.count(kMaxPointsPerCut, sizeof(uint8_t));
    if (in.good()) project.cutPointLocked.reserve(static_cast<size_t>(locks));
    for (uint64_t index = 0; index < locks && in.good(); ++index) project.cutPointLocked.push_back(in.boolean());
    project.placements = readVector<ConnectorPlacement>(in, kMaxConnectorsPerCut, 3,
        [&]() { return readPlacement(in, version); });
    project.settings = readSettings(in, version); project.cutAllActiveParts = in.boolean();
    project.showSource = in.boolean(); project.wireframe = in.boolean(); project.showPrintBed = in.boolean();
    project.printBedWidth = in.pod<double>(); project.printBedDepth = in.pod<double>();
    project.printBedCenter = readVec2(in); project.printBedRotation = in.pod<double>();
    project.printBedPreset = in.pod<int32_t>(); project.customBedWidth = in.pod<double>();
    project.customBedDepth = in.pod<double>(); project.cameraTop = in.boolean();
    project.cameraYaw = in.pod<double>(); project.cameraPitch = in.pod<double>();
    project.cameraOrbitDistance = in.pod<double>(); project.cameraTopHeight = in.pod<double>();
    project.cameraPan = readVec2(in);
    if (version >= 5) project.modelTransparency = in.pod<double>();
    if (version >= 3) project.workerConnectorFlags = in.blob(kMaxConnectorsPerCut * 2);
    return project;
}

bool validateSettings(const DovetailSettings& settings, std::string& error) {
    constexpr double kMaxDimension = 1'000'000.0;
    if (!validDistribution(settings.distribution) || !validConnectorType(settings.type)) {
        error = "Projekt enthält einen unbekannten Verbinder- oder Verteilungsmodus.";
        return false;
    }
    if (settings.count < 1 || settings.count > 100 ||
        !finite(settings.spacing) || settings.spacing <= 0.0 || settings.spacing > kMaxDimension ||
        !finite(settings.endMargin) || settings.endMargin < 0.0 || settings.endMargin > kMaxDimension ||
        !finite(settings.neckWidth) || settings.neckWidth <= 0.0 || settings.neckWidth > kMaxDimension ||
        !finite(settings.headWidth) || settings.headWidth <= 0.0 || settings.headWidth > kMaxDimension ||
        !finite(settings.depth) || settings.depth <= 0.0 || settings.depth > kMaxDimension ||
        !finite(settings.embed) || settings.embed <= 0.0 || settings.embed > kMaxDimension ||
        !finite(settings.clearance) || settings.clearance < 0.0 || settings.clearance > 1000.0 ||
        !finite(settings.chamferWidth) || settings.chamferWidth < 0.0 || settings.chamferWidth > kMaxDimension ||
        !finite(settings.chamferAngle) || settings.chamferAngle < 0.0 || settings.chamferAngle > 90.0 ||
        !finite(settings.minimumWall) || settings.minimumWall < 0.0 || settings.minimumWall > 20.0 ||
        settings.assemblyMarkCode < 1 || settings.assemblyMarkCode > 1'000'000'000 ||
        !finite(settings.assemblyMarkPosition) || settings.assemblyMarkPosition < 0.0 ||
        settings.assemblyMarkPosition > 1.0 || !finite(settings.assemblyMarkSize) ||
        settings.assemblyMarkSize < 2.0 || settings.assemblyMarkSize > 30.0 ||
        !finite(settings.assemblyMarkDepth) || settings.assemblyMarkDepth < 0.05 ||
        settings.assemblyMarkDepth > 5.0 || !finite(settings.assemblyMarkLeftX) ||
        !finite(settings.assemblyMarkLeftY) || !finite(settings.assemblyMarkRightX) ||
        !finite(settings.assemblyMarkRightY) ||
        std::abs(settings.assemblyMarkLeftX) > kMaxCoordinateMagnitude ||
        std::abs(settings.assemblyMarkLeftY) > kMaxCoordinateMagnitude ||
        std::abs(settings.assemblyMarkRightX) > kMaxCoordinateMagnitude ||
        std::abs(settings.assemblyMarkRightY) > kMaxCoordinateMagnitude) {
        error = "Projekt enthält ungültige Verbindermaße.";
        return false;
    }
    if ((settings.type == ConnectorType::Dovetail || settings.type == ConnectorType::Puzzle) &&
        settings.headWidth <= settings.neckWidth) {
        error = "Projekt enthält einen Verbinder mit Kopfbreite kleiner oder gleich der Halsbreite.";
        return false;
    }
    return true;
}

bool validatePlacement(const ConnectorPlacement& placement, std::string& error) {
    constexpr double kMaxDimension = 1'000'000.0;
    auto optionalPositive = [](double value) {
        return value == -1.0 || (finite(value) && value > 0.0 && value <= kMaxDimension);
    };
    auto optionalNonNegative = [](double value) {
        return value == -1.0 || (finite(value) && value >= 0.0 && value <= kMaxDimension);
    };
    if (!finite(placement.position) || placement.position < 0.0 || placement.position > 1.0 ||
        !validConnectorType(placement.type) ||
        !optionalPositive(placement.neckWidth) || !optionalPositive(placement.headWidth) ||
        !optionalPositive(placement.depth) || !optionalPositive(placement.embed) ||
        !optionalNonNegative(placement.clearance) ||
        (placement.clearance != -1.0 && placement.clearance > 1000.0) ||
        placement.leadChamfer < -1 || placement.leadChamfer > 1 ||
        !optionalNonNegative(placement.chamferWidth) ||
        !(placement.chamferAngle == -1.0 || (finite(placement.chamferAngle) &&
                                            placement.chamferAngle >= 0.0 && placement.chamferAngle <= 90.0))) {
        error = "Projekt enthält eine ungültige Verbinderplatzierung.";
        return false;
    }
    return true;
}

bool validateMesh(const TriangleMesh& mesh, std::string& error) {
    if (mesh.vertices.empty() || mesh.triangles.empty() ||
        mesh.vertices.size() > kMaxVerticesPerMesh ||
        mesh.triangles.size() > kMaxTrianglesPerMesh ||
        mesh.defaultMaterial == 0 || mesh.defaultMaterial > kMaxMaterials ||
        (!mesh.triangleMaterials.empty() &&
         mesh.triangleMaterials.size() != mesh.triangles.size()) ||
        std::any_of(mesh.triangleMaterials.begin(), mesh.triangleMaterials.end(),
                    [](uint32_t material) { return material > kMaxMaterials; })) {
        error = "Projekt enthält ein leeres Teil oder ungültige Materialzuweisungen.";
        return false;
    }
    for (const Vec3& vertex : mesh.vertices) {
        if (!coordinateInRange(vertex)) { error = "Projekt enthält ungültige oder extrem große Mesh-Koordinaten."; return false; }
    }
    for (const auto& triangle : mesh.triangles) {
        if (triangle[0] >= mesh.vertices.size() || triangle[1] >= mesh.vertices.size() ||
            triangle[2] >= mesh.vertices.size() || triangle[0] == triangle[1] ||
            triangle[1] == triangle[2] || triangle[2] == triangle[0]) {
            error = "Projekt enthält ungültige oder degenerierte Dreiecke.";
            return false;
        }
    }
    return true;
}

bool validateCut(const ProjectCutState& cut,
                 const std::unordered_set<int>& partIds,
                 const std::unordered_set<int>& cutIds,
                 const std::vector<ProjectPartState>& parts,
                 std::string& error) {
    if (cut.id <= 0 || cut.sourcePartId <= 0 || partIds.count(cut.sourcePartId) == 0 ||
        cutIds.count(cut.id) == 0 || cut.mode < 0 || cut.mode > 1 ||
        cut.childPartIds.size() != cut.childLabels.size() || cut.childPartIds.size() < 2 ||
        cut.points.size() < 2 || cut.pointLocked.size() != cut.points.size()) {
        error = "Projekt enthält einen strukturell ungültigen Schnitt.";
        return false;
    }
    if (!validateSettings(cut.settings, error)) return false;
    for (Vec2 point : cut.points)
        if (!coordinateInRange(point)) { error = "Projekt enthält ungültige oder extrem große Schnittpunkte."; return false; }
    for (const ConnectorPlacement& placement : cut.placements)
        if (!validatePlacement(placement, error)) return false;

    std::unordered_set<int> uniqueChildren;
    for (size_t index = 0; index < cut.childPartIds.size(); ++index) {
        const int childId = cut.childPartIds[index];
        if (!uniqueChildren.insert(childId).second || partIds.count(childId) == 0) {
            error = "Projekt enthält doppelte oder unbekannte Schnitt-Kindteile.";
            return false;
        }
        const auto found = std::find_if(parts.begin(), parts.end(),
            [&](const ProjectPartState& part) { return part.id == childId; });
        if (found == parts.end() || found->parentCutId != cut.id || found->label != cut.childLabels[index]) {
            error = "Projekt enthält widersprüchliche Schnitt-Kindreferenzen.";
            return false;
        }
    }
    const auto source = std::find_if(parts.begin(), parts.end(),
        [&](const ProjectPartState& part) { return part.id == cut.sourcePartId; });
    if (source == parts.end() || (!cut.sourceLabel.empty() && source->label != cut.sourceLabel)) {
        error = "Projekt enthält eine widersprüchliche Schnitt-Quellreferenz.";
        return false;
    }
    return true;
}

bool sameSettings(const DovetailSettings& first, const DovetailSettings& second) {
    return first.distribution == second.distribution && first.count == second.count &&
           first.spacing == second.spacing && first.endMargin == second.endMargin &&
           first.neckWidth == second.neckWidth && first.headWidth == second.headWidth &&
           first.depth == second.depth && first.embed == second.embed &&
           first.clearance == second.clearance && first.leadChamfer == second.leadChamfer &&
           first.chamferWidth == second.chamferWidth && first.chamferAngle == second.chamferAngle &&
           first.validateWallThickness == second.validateWallThickness &&
           first.minimumWall == second.minimumWall &&
           first.assemblyMarks == second.assemblyMarks &&
           first.assemblyMarkCode == second.assemblyMarkCode &&
           first.assemblyMarkPosition == second.assemblyMarkPosition &&
           first.assemblyMarkSize == second.assemblyMarkSize &&
           first.assemblyMarkDepth == second.assemblyMarkDepth &&
           first.assemblyMarkPositionsCustom == second.assemblyMarkPositionsCustom &&
           first.assemblyMarkLeftX == second.assemblyMarkLeftX &&
           first.assemblyMarkLeftY == second.assemblyMarkLeftY &&
           first.assemblyMarkRightX == second.assemblyMarkRightX &&
           first.assemblyMarkRightY == second.assemblyMarkRightY &&
           first.maleOnLeft == second.maleOnLeft &&
           first.validateInsideModel == second.validateInsideModel && first.type == second.type;
}

bool samePlacement(const ConnectorPlacement& first, const ConnectorPlacement& second) {
    return first.position == second.position && first.maleOnLeft == second.maleOnLeft &&
           first.locked == second.locked && first.type == second.type &&
           first.neckWidth == second.neckWidth && first.headWidth == second.headWidth &&
           first.depth == second.depth && first.embed == second.embed &&
           first.clearance == second.clearance && first.leadChamfer == second.leadChamfer &&
           first.chamferWidth == second.chamferWidth && first.chamferAngle == second.chamferAngle;
}

bool sameOperationDefinition(const ProjectCutState& first, const ProjectCutState& second) {
    if (first.mode != second.mode || first.cutAllActiveParts != second.cutAllActiveParts ||
        first.forcedInvalidConnectors != second.forcedInvalidConnectors ||
        !sameSettings(first.settings, second.settings) ||
        first.points.size() != second.points.size() ||
        first.pointLocked != second.pointLocked ||
        first.placements.size() != second.placements.size()) return false;
    for (size_t index = 0; index < first.points.size(); ++index) {
        if (first.points[index].x != second.points[index].x ||
            first.points[index].y != second.points[index].y) return false;
    }
    for (size_t index = 0; index < first.placements.size(); ++index)
        if (!samePlacement(first.placements[index], second.placements[index])) return false;
    return true;
}

bool addSerializedSize(uint64_t& total, uint64_t value) {
    if (value > kMaxProjectBytes - total) return false;
    total += value;
    return true;
}

bool addTextSize(uint64_t& total, const std::string& value, uint64_t maximum) {
    return value.size() <= maximum && addSerializedSize(total, sizeof(uint64_t)) &&
           addSerializedSize(total, static_cast<uint64_t>(value.size()));
}

bool estimateProjectSize(const PartSpliceProject& project, std::string& error) {
    uint64_t total = kMagic.size() + sizeof(uint32_t);
    if (!addTextSize(total, project.originalPath, kMaxString) ||
        !addTextSize(total, project.originalFileName, kMaxString) ||
        project.originalFileData.size() > kMaxBlob ||
        !addSerializedSize(total, sizeof(uint64_t)) ||
        !addSerializedSize(total, static_cast<uint64_t>(project.originalFileData.size())) ||
        !addTextSize(total, project.sourceThreeMfProjectSettings, 64ull * 1024ull * 1024ull) ||
        !addSerializedSize(total, sizeof(uint64_t)) ||
        project.workerConnectorFlags.size() > kMaxConnectorsPerCut * 2 ||
        !addSerializedSize(total, sizeof(uint64_t) +
                           static_cast<uint64_t>(project.workerConnectorFlags.size())) ||
        !addSerializedSize(total, sizeof(uint64_t))) {
        error = "Projekt überschreitet die zulässige Dateigröße oder Text-/Blob-Grenzen.";
        return false;
    }
    for (const MeshMaterial& material : project.materials) {
        if (!addTextSize(total, material.name, kMaxString) ||
            !addTextSize(total, material.color, 64)) {
            error = "Projekt überschreitet die zulässige Materialdatenmenge.";
            return false;
        }
    }
    if (!addSerializedSize(total, sizeof(uint64_t))) {
        error = "Projekt überschreitet die zulässige Dateigröße.";
        return false;
    }
    for (const std::string& warning : project.threeMfCompatibilityWarnings) {
        if (!addTextSize(total, warning, kMaxString)) {
            error = "Projekt überschreitet die zulässige Hinweisdatenmenge.";
            return false;
        }
    }
    for (const ProjectObjectState& object : project.objects) {
        if (!addTextSize(total, object.name, kMaxString) ||
            !addSerializedSize(total, sizeof(int32_t) + sizeof(uint64_t))) {
            error = "Projekt überschreitet die zulässige Dateigröße.";
            return false;
        }
        for (const ProjectPartState& part : object.parts) {
            const TriangleMesh& mesh = part.meshData();
            if (!addSerializedSize(total, sizeof(int32_t) * 2) ||
                !addTextSize(total, part.label, kMaxString) ||
                !addSerializedSize(total, sizeof(uint64_t)) ||
                mesh.vertices.size() > (kMaxProjectBytes / (sizeof(double) * 3)) ||
                !addSerializedSize(total, static_cast<uint64_t>(mesh.vertices.size()) * sizeof(double) * 3) ||
                !addSerializedSize(total, sizeof(uint64_t)) ||
                mesh.triangles.size() > (kMaxProjectBytes / (sizeof(uint32_t) * 3)) ||
                !addSerializedSize(total, static_cast<uint64_t>(mesh.triangles.size()) * sizeof(uint32_t) * 3) ||
                !addSerializedSize(total, sizeof(uint32_t) + sizeof(uint64_t)) ||
                !addSerializedSize(total, static_cast<uint64_t>(mesh.triangleMaterials.size()) * sizeof(uint32_t)) ||
                !addSerializedSize(total, sizeof(uint8_t))) {
                error = "Projekt überschreitet die zulässige Dateigröße.";
                return false;
            }
        }
        if (!addSerializedSize(total, sizeof(uint64_t))) {
            error = "Projekt überschreitet die zulässige Dateigröße.";
            return false;
        }
        for (const ProjectCutState& cut : object.cuts) {
            if (!addSerializedSize(total, sizeof(int32_t) * 2) ||
                !addTextSize(total, cut.sourceLabel, kMaxString) ||
                !addSerializedSize(total, sizeof(uint64_t) +
                    static_cast<uint64_t>(cut.childPartIds.size()) * sizeof(int32_t)) ||
                !addSerializedSize(total, sizeof(uint64_t))) {
                error = "Projekt überschreitet die zulässige Dateigröße.";
                return false;
            }
            for (const std::string& label : cut.childLabels) {
                if (!addTextSize(total, label, kMaxString)) {
                    error = "Projekt überschreitet die zulässige Dateigröße.";
                    return false;
                }
            }
            constexpr uint64_t settingsBytes = sizeof(int32_t) * 3 + sizeof(double) * 18 + sizeof(uint8_t) * 5;
            const uint64_t placementBytes = kSerializedPlacementBytesV2;
            if (!addSerializedSize(total, sizeof(uint64_t) +
                    static_cast<uint64_t>(cut.points.size()) * sizeof(double) * 2) ||
                !addSerializedSize(total, sizeof(uint64_t) +
                    static_cast<uint64_t>(cut.pointLocked.size()) * sizeof(uint8_t)) ||
                !addSerializedSize(total, sizeof(uint64_t) +
                    static_cast<uint64_t>(cut.placements.size()) * placementBytes) ||
                !addSerializedSize(total, settingsBytes + sizeof(int32_t) + sizeof(uint8_t) * 2)) {
                error = "Projekt überschreitet die zulässige Dateigröße.";
                return false;
            }
        }
        if (!addSerializedSize(total, sizeof(int32_t) * 3)) {
            error = "Projekt überschreitet die zulässige Dateigröße.";
            return false;
        }
    }
    // Remaining global UI and current-cut state. The estimate is deliberately
    // conservative; it only needs to reject impossible or oversized saves.
    constexpr uint64_t globalFixedBytes = 1024;
    const uint64_t placementBytes = kSerializedPlacementBytesV2;
    if (!addSerializedSize(total, globalFixedBytes) ||
        !addSerializedSize(total, static_cast<uint64_t>(project.cutPoints.size()) * sizeof(double) * 2) ||
        !addSerializedSize(total, static_cast<uint64_t>(project.cutPointLocked.size()) * sizeof(uint8_t)) ||
        !addSerializedSize(total, static_cast<uint64_t>(project.placements.size()) * placementBytes)) {
        error = "Projekt überschreitet die Sicherheitsgrenze von 512 MiB.";
        return false;
    }
    return true;
}

bool validateProject(const PartSpliceProject& project, std::string& error) {
    if (!estimateProjectSize(project, error)) return false;
    if (!validUtf8(project.originalPath) || !validUtf8(project.originalFileName) ||
        (!project.sourceThreeMfProjectSettings.empty() &&
         !validUtf8(project.sourceThreeMfProjectSettings))) {
        error = "Projekt enthält ungültig kodierte Datei- oder Pfadnamen.";
        return false;
    }
    if (project.materials.size() > kMaxMaterials ||
        project.threeMfCompatibilityWarnings.size() > kMaxCompatibilityWarnings) {
        error = "Projekt enthält zu viele Materialien oder Kompatibilitätshinweise.";
        return false;
    }
    for (const MeshMaterial& material : project.materials) {
        if (!validUtf8(material.name) || !validUtf8(material.color) ||
            material.name.size() > kMaxString || material.color.size() > 64) {
            error = "Projekt enthält ungültige Materialdaten.";
            return false;
        }
    }
    for (const std::string& warning : project.threeMfCompatibilityWarnings) {
        if (!validUtf8(warning) || warning.size() > kMaxString) {
            error = "Projekt enthält einen ungültigen Kompatibilitätshinweis.";
            return false;
        }
    }
    if (project.workerConnectorFlags.size() > kMaxConnectorsPerCut * 2 ||
        std::any_of(project.workerConnectorFlags.begin(), project.workerConnectorFlags.end(),
                    [](uint8_t value) { return value > 1; })) {
        error = "Projekt enthält ungültige interne Worker-Daten.";
        return false;
    }
    if (project.objects.empty() || project.objects.size() > kMaxObjects ||
        project.cutPoints.size() > kMaxPointsPerCut ||
        project.placements.size() > kMaxConnectorsPerCut ||
        project.activeObjectIndex < 0 ||
        static_cast<size_t>(project.activeObjectIndex) >= project.objects.size() ||
        project.exportFormat < 0 || project.exportFormat > 1 ||
        project.cutMode < 0 || project.cutMode > 1 ||
        project.printBedPreset < 0 || project.printBedPreset > 14) {
        error = "Projektzustand enthält ungültige Hauptparameter.";
        return false;
    }
    if (!validateSettings(project.settings, error)) return false;
    if (project.cutPointLocked.size() != project.cutPoints.size()) {
        error = "Anzahl der Schnittpunkte und Sperrmarkierungen stimmt nicht überein.";
        return false;
    }
    for (Vec2 point : project.cutPoints)
        if (!coordinateInRange(point)) { error = "Projekt enthält ungültige oder extrem große Schnittpunkte."; return false; }
    for (const ConnectorPlacement& placement : project.placements)
        if (!validatePlacement(placement, error)) return false;

    const double scalarValues[] = {
        project.printBedWidth, project.printBedDepth, project.printBedRotation,
        project.customBedWidth, project.customBedDepth, project.cameraYaw,
        project.cameraPitch, project.cameraOrbitDistance, project.cameraTopHeight,
        project.modelTransparency
    };
    for (double value : scalarValues)
        if (!finite(value)) { error = "Projekt enthält nicht endliche Anzeige- oder Druckbettwerte."; return false; }
    if (!coordinateInRange(project.printBedCenter) || !coordinateInRange(project.cameraPan) ||
        project.printBedWidth <= 0.0 || project.printBedDepth <= 0.0 ||
        project.customBedWidth <= 0.0 || project.customBedDepth <= 0.0 ||
        project.cameraOrbitDistance <= 0.0 || project.cameraTopHeight <= 0.0 ||
        project.modelTransparency < 0.0 || project.modelTransparency > 85.0 ||
        project.printBedWidth > 100'000.0 || project.printBedDepth > 100'000.0 ||
        project.customBedWidth > 100'000.0 || project.customBedDepth > 100'000.0) {
        error = "Projekt enthält unzulässige Anzeige- oder Druckbettwerte.";
        return false;
    }

    for (const ProjectObjectState& object : project.objects) {
        if (!validUtf8(object.name) || object.parts.empty() || object.parts.size() > kMaxPartsPerObject ||
            object.cuts.size() > kMaxCutsPerObject || object.name.size() > kMaxString ||
            object.plateIndex < 1 || object.plateIndex > 10'000) {
            error = "Projekt enthält ein Objekt ohne Teile oder mit ungültiger Plattennummer.";
            return false;
        }
        std::unordered_set<int> partIds;
        int maxPartId = 0;
        size_t activePartCount = 0;
        for (const ProjectPartState& part : object.parts) {
            if (part.id <= 0 || !validUtf8(part.label) || part.label.size() > kMaxString ||
                !partIds.insert(part.id).second || !validateMesh(part.meshData(), error)) {
                if (error.empty()) error = "Projekt enthält doppelte oder ungültige Teil-IDs.";
                return false;
            }
            maxPartId = std::max(maxPartId, part.id);
            if (part.active) ++activePartCount;
        }
        if (activePartCount == 0) {
            error = "Projektobjekt besitzt kein aktives Ergebnisbauteil.";
            return false;
        }
        std::unordered_set<int> cutIds;
        std::unordered_map<int, const ProjectCutState*> operationDefinitions;
        int maxCutId = 0;
        for (const ProjectCutState& cut : object.cuts) {
            if (cut.id <= 0) {
                error = "Projekt enthält eine ungültige Schnitt-ID.";
                return false;
            }
            cutIds.insert(cut.id);
            const auto [definition, inserted] = operationDefinitions.emplace(cut.id, &cut);
            if (!inserted && !sameOperationDefinition(*definition->second, cut)) {
                error = "Mehrere Zielteile derselben Schnittoperation besitzen widersprüchliche Schnitt- oder Verbinderdaten.";
                return false;
            }
            if (!validUtf8(cut.sourceLabel) || cut.sourceLabel.size() > kMaxString || cut.childPartIds.size() > kMaxChildrenPerCut ||
                cut.childLabels.size() > kMaxChildrenPerCut || cut.points.size() > kMaxPointsPerCut ||
                cut.pointLocked.size() > kMaxPointsPerCut || cut.placements.size() > kMaxConnectorsPerCut ||
                std::any_of(cut.childLabels.begin(), cut.childLabels.end(),
                            [](const std::string& label) {
                                return label.size() > kMaxString || !validUtf8(label);
                            })) {
                error = "Projekt enthält einen Schnitt, der Sicherheitsgrenzen überschreitet.";
                return false;
            }
            maxCutId = std::max(maxCutId, cut.id);
        }
        std::unordered_set<int> sourcePartIds;
        std::unordered_map<int, std::vector<int>> partGraph;
        std::unordered_map<int, int> childOwners;
        for (const ProjectCutState& cut : object.cuts) {
            if (!sourcePartIds.insert(cut.sourcePartId).second ||
                !validateCut(cut, partIds, cutIds, object.parts, error)) {
                if (error.empty()) error = "Projekt enthält mehrere Schnitte für dasselbe Quellteil.";
                return false;
            }
            const auto source = std::find_if(object.parts.begin(), object.parts.end(),
                [&](const ProjectPartState& part) { return part.id == cut.sourcePartId; });
            if (source == object.parts.end() || source->active) {
                error = "Ein bereits geschnittenes Quellteil ist fälschlich noch aktiv.";
                return false;
            }
            for (int childId : cut.childPartIds) {
                if (childId == cut.sourcePartId || !childOwners.emplace(childId, cut.id).second) {
                    error = "Projekt enthält zyklische oder mehrfach zugeordnete Schnitt-Kindteile.";
                    return false;
                }
                partGraph[cut.sourcePartId].push_back(childId);
            }
        }
        for (const ProjectPartState& part : object.parts) {
            if (part.parentCutId != -1) {
                if (cutIds.count(part.parentCutId) == 0) {
                    error = "Projekt enthält ein Teil mit unbekannter Eltern-Schnitt-ID.";
                    return false;
                }
                const auto owner = childOwners.find(part.id);
                if (owner == childOwners.end() || owner->second != part.parentCutId) {
                    error = "Projekt enthält ein verwaistes oder falsch zugeordnetes Schnitt-Kindteil.";
                    return false;
                }
            } else if (childOwners.count(part.id) != 0) {
                error = "Projekt enthält ein Kindteil ohne passende Eltern-Schnitt-ID.";
                return false;
            }
        }
        std::unordered_map<int, uint8_t> visitState;
        for (int partId : partIds) {
            if (visitState[partId] == 2) continue;
            std::vector<std::pair<int, size_t>> stack{{partId, 0}};
            visitState[partId] = 1;
            while (!stack.empty()) {
                auto& [current, childIndex] = stack.back();
                const auto edges = partGraph.find(current);
                if (edges == partGraph.end() || childIndex >= edges->second.size()) {
                    visitState[current] = 2;
                    stack.pop_back();
                    continue;
                }
                const int child = edges->second[childIndex++];
                const uint8_t state = visitState[child];
                if (state == 1) {
                    error = "Projekt enthält einen Zyklus im Schnittbaum.";
                    return false;
                }
                if (state == 0) {
                    visitState[child] = 1;
                    stack.emplace_back(child, 0);
                }
            }
        }
        const auto selected = std::find_if(object.parts.begin(), object.parts.end(),
            [&](const ProjectPartState& part) { return part.id == object.selectedPartId; });
        if (selected == object.parts.end() || !selected->active ||
            object.nextPartId <= maxPartId || object.nextCutId <= maxCutId ||
            object.nextPartId >= std::numeric_limits<int>::max() ||
            object.nextCutId >= std::numeric_limits<int>::max()) {
            error = "Projekt enthält eine ungültige Auswahl oder kollidierende nächste IDs.";
            return false;
        }
    }
    return true;
}

bool replaceAtomically(const std::filesystem::path& temporary,
                       const std::filesystem::path& target,
                       std::string& error) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Projektdatei konnte nicht atomar ersetzt werden (Windows-Fehler " +
                std::to_string(GetLastError()) + ").";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        error = "Projektdatei konnte nicht atomar ersetzt werden: " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
#endif
    return true;
}

} // namespace

bool savePartSpliceProject(const std::filesystem::path& path,
                           const PartSpliceProject& project,
                           std::string& error) {
    std::filesystem::path temporary;
    try {
        if (!validateProject(project, error)) return false;
        temporary = reserveSecureTemporarySibling(path);
        Writer writer(temporary);
        if (!writer.good()) {
            error = "Projektdatei konnte nicht zum Schreiben geöffnet werden.";
            return false;
        }
        writeProject(writer, project);
        writer.output.flush();
        if (!writer.good()) {
            error = "Projektdatei konnte nicht vollständig geschrieben werden.";
            writer.output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        writer.output.close();
        return replaceAtomically(temporary, path, error);
    } catch (const std::bad_alloc&) {
        error = "Nicht genügend Arbeitsspeicher zum Speichern des Projekts.";
    } catch (const std::length_error& exception) {
        error = std::string("Projekt ist zu groß: ") + exception.what();
    } catch (const std::exception& exception) {
        error = std::string("Projekt konnte nicht gespeichert werden: ") + exception.what();
    } catch (...) {
        error = "Projekt konnte wegen eines unbekannten Fehlers nicht gespeichert werden.";
    }
    if (!temporary.empty()) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
    }
    return false;
}

ProjectLoadResult loadPartSpliceProject(const std::filesystem::path& path,
                                        const std::atomic_bool* cancelRequested) {
    ProjectLoadResult result;
    try {
        Reader reader(path, cancelRequested);
        if (!reader.input) { result.message = "Projektdatei konnte nicht geöffnet werden."; return result; }
        if (!reader.good()) { result.message = "Projektdatei ist zu groß oder ihre Größe konnte nicht gelesen werden."; return result; }
        std::array<char, 8> magic{};
        if (!reader.canRead(magic.size())) { result.message = "Projektdatei ist abgeschnitten."; return result; }
        reader.input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        const uint32_t version = reader.pod<uint32_t>();
        if (!reader.good() || magic != kMagic || version < 1 || version > kVersion) {
            result.message = "Dies ist keine unterstützte PartSplice-3D-Projektdatei.";
            return result;
        }
        result.project = readProject(reader, version);
        if (!reader.good() || reader.remaining() != 0) {
            result.message = "Die Projektdatei ist beschädigt, abgeschnitten oder überschreitet Sicherheitsgrenzen.";
            return result;
        }
        std::string validationError;
        if (!validateProject(result.project, validationError)) {
            result.message = "Die Projektdatei ist semantisch ungültig: " + validationError;
            return result;
        }
        result.ok = true;
        result.message = "PartSplice-Projekt geladen und vollständig geprüft.";
    } catch (const OperationCanceled&) {
        result.message = "Ladevorgang abgebrochen.";
    } catch (const std::bad_alloc&) {
        result.message = "Nicht genügend Arbeitsspeicher zum Laden der Projektdatei.";
    } catch (const std::length_error&) {
        result.message = "Projektdatei fordert eine unzulässig große Speicherstruktur an.";
    } catch (const std::exception& exception) {
        result.message = std::string("Projektdatei konnte nicht geladen werden: ") + exception.what();
    }
    return result;
}
