#include "three_mf.h"
#include "safe_temp.h"

#include <miniz.h>
#include <tinyxml2.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using Transform = std::array<double, 12>;

constexpr uint64_t kMaxArchiveBytes = 512ull * 1024ull * 1024ull;
constexpr uint64_t kMaxEntryBytes = 128ull * 1024ull * 1024ull;
// Mesh XML is legitimately much larger than the compressed 3MF file. Keep a
// separate, bounded budget for model documents so detailed models are not
// rejected by the smaller limit intended for auxiliary package entries.
constexpr uint64_t kMaxModelEntryBytes = 384ull * 1024ull * 1024ull;
constexpr uint64_t kMaxMetadataBytes = 16ull * 1024ull * 1024ull;
constexpr uint64_t kMaxTotalExtractedBytes = 512ull * 1024ull * 1024ull;
constexpr uint64_t kMaxCompressionRatio = 1000;
constexpr mz_uint kMaxArchiveEntries = 4'096;
constexpr size_t kMaxResources = 20'000;
constexpr size_t kMaxMaterials = 256;
constexpr size_t kMaxBuildItems = 4'096;
constexpr size_t kMaxVerticesTotal = 10'000'000;
constexpr size_t kMaxTrianglesTotal = 20'000'000;
constexpr size_t kMaxComponentsTotal = 200'000;
constexpr size_t kMaxFlattenDepth = 128;
constexpr size_t kMaxOutputVertices = 10'000'000;
constexpr size_t kMaxOutputTriangles = 20'000'000;
constexpr double kMaxCoordinateMagnitude = 1'000'000'000.0;
constexpr double kMaxTransformMagnitude = 1'000'000'000.0;

class OperationCanceled final : public std::runtime_error {
public:
    OperationCanceled() : std::runtime_error("Vorgang abgebrochen.") {}
};

bool cancellationRequested(const std::atomic_bool* cancelRequested) {
    return cancelRequested && cancelRequested->load(std::memory_order_relaxed);
}

void throwIfCanceled(const std::atomic_bool* cancelRequested) {
    if (cancellationRequested(cancelRequested)) throw OperationCanceled();
}

bool finite(double value) { return std::isfinite(value); }
bool finite(Vec3 value) { return finite(value.x) && finite(value.y) && finite(value.z); }
bool coordinateInRange(Vec3 value) {
    return finite(value) && std::abs(value.x) <= kMaxCoordinateMagnitude &&
           std::abs(value.y) <= kMaxCoordinateMagnitude &&
           std::abs(value.z) <= kMaxCoordinateMagnitude;
}

Transform identityTransform() {
    return {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
}

bool parseTransform(const char* text, Transform& result) {
    result = identityTransform();
    if (!text || *text == '\0') return true;
    std::istringstream stream(text);
    for (double& value : result) {
        if (!(stream >> value) || !finite(value) || std::abs(value) > kMaxTransformMagnitude) return false;
    }
    std::string trailing;
    return !(stream >> trailing);
}

Vec3 transformed(Vec3 point, const Transform& matrix) {
    return {point.x * matrix[0] + point.y * matrix[3] + point.z * matrix[6] + matrix[9],
            point.x * matrix[1] + point.y * matrix[4] + point.z * matrix[7] + matrix[10],
            point.x * matrix[2] + point.y * matrix[5] + point.z * matrix[8] + matrix[11]};
}

Transform composed(const Transform& first, const Transform& second) {
    Transform output{};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            output[row * 3 + column] = first[row * 3] * second[column] +
                                       first[row * 3 + 1] * second[3 + column] +
                                       first[row * 3 + 2] * second[6 + column];
        }
    }
    const Vec3 translation = transformed({first[9], first[10], first[11]}, second);
    output[9] = translation.x;
    output[10] = translation.y;
    output[11] = translation.z;
    return output;
}

bool transformInRange(const Transform& transform) {
    return std::all_of(transform.begin(), transform.end(), [](double value) {
        return finite(value) && std::abs(value) <= kMaxTransformMagnitude;
    });
}

const char* localName(const char* name) {
    if (!name) return "";
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}

tinyxml2::XMLElement* firstChild(tinyxml2::XMLElement* parent, const char* name) {
    if (!parent) return nullptr;
    for (auto* child = parent->FirstChildElement(); child; child = child->NextSiblingElement())
        if (std::strcmp(localName(child->Name()), name) == 0) return child;
    return nullptr;
}

std::vector<uint8_t> readFileLimited(const std::filesystem::path& path, std::string& error,
                                     const std::atomic_bool* cancelRequested) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { error = "3MF-Datei konnte nicht geöffnet werden."; return {}; }
    const std::streamoff end = input.tellg();
    if (end <= 0) { error = "3MF-Datei ist leer."; return {}; }
    if (static_cast<uint64_t>(end) > kMaxArchiveBytes) {
        error = "3MF-Datei überschreitet die Sicherheitsgrenze von 512 MiB.";
        return {};
    }
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(end));
    constexpr size_t chunkSize = 4u * 1024u * 1024u;
    size_t offset = 0;
    while (offset < data.size()) {
        throwIfCanceled(cancelRequested);
        const size_t chunk = std::min(chunkSize, data.size() - offset);
        input.read(reinterpret_cast<char*>(data.data() + offset), static_cast<std::streamsize>(chunk));
        if (!input) { error = "3MF-Datei konnte nicht vollständig gelesen werden."; return {}; }
        offset += chunk;
    }
    return data;
}

bool preflightZipEntryCount(const std::vector<uint8_t>& data, std::string& error) {
    constexpr uint32_t signature = 0x06054b50u;
    constexpr size_t minimumSize = 22;
    if (data.size() < minimumSize) { error = "3MF/ZIP-Container ist abgeschnitten."; return false; }
    const size_t searchStart = data.size() > 65'557 ? data.size() - 65'557 : 0;
    auto read16 = [&](size_t offset) -> uint16_t {
        return static_cast<uint16_t>(static_cast<uint16_t>(data[offset]) |
               static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8));
    };
    auto read32 = [&](size_t offset) {
        return static_cast<uint32_t>(data[offset]) |
               (static_cast<uint32_t>(data[offset + 1]) << 8) |
               (static_cast<uint32_t>(data[offset + 2]) << 16) |
               (static_cast<uint32_t>(data[offset + 3]) << 24);
    };
    for (size_t offset = data.size() - minimumSize + 1; offset-- > searchStart;) {
        if (read32(offset) != signature) continue;
        const uint16_t commentLength = read16(offset + 20);
        if (offset + minimumSize + commentLength != data.size()) continue;
        const uint16_t disk = read16(offset + 4);
        const uint16_t directoryDisk = read16(offset + 6);
        const uint16_t entriesOnDisk = read16(offset + 8);
        const uint16_t entriesTotal = read16(offset + 10);
        if (disk != 0 || directoryDisk != 0 || entriesOnDisk != entriesTotal) {
            error = "Mehrteilige ZIP-Archive werden für 3MF nicht unterstützt.";
            return false;
        }
        if (entriesTotal == 0xffffu) {
            error = "ZIP64-Archive werden aus Sicherheitsgründen nicht unterstützt.";
            return false;
        }
        if (entriesTotal == 0 || entriesTotal > kMaxArchiveEntries) {
            error = "3MF enthält keine oder zu viele ZIP-Einträge.";
            return false;
        }
        return true;
    }
    error = "3MF enthält kein gültiges ZIP-Zentralverzeichnis.";
    return false;
}

double unitScale(const char* unit) {
    if (!unit || std::strcmp(unit, "millimeter") == 0) return 1.0;
    if (std::strcmp(unit, "micron") == 0) return 0.001;
    if (std::strcmp(unit, "centimeter") == 0) return 10.0;
    if (std::strcmp(unit, "meter") == 0) return 1000.0;
    if (std::strcmp(unit, "inch") == 0) return 25.4;
    if (std::strcmp(unit, "foot") == 0) return 304.8;
    return 0.0;
}

std::string canonicalArchivePath(std::string path, std::string& error) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    std::vector<std::string> parts;
    size_t position = 0;
    while (position <= path.size()) {
        const size_t slash = path.find('/', position);
        const std::string part = path.substr(position, slash == std::string::npos ? std::string::npos : slash - position);
        if (part.empty() || part == ".") {
            // Ignore.
        } else if (part == "..") {
            if (parts.empty()) { error = "3MF enthält einen unzulässigen Pfad außerhalb des Archivs."; return {}; }
            parts.pop_back();
        } else {
            parts.push_back(part);
        }
        if (slash == std::string::npos) break;
        position = slash + 1;
    }
    std::ostringstream normalized;
    for (size_t index = 0; index < parts.size(); ++index) {
        if (index) normalized << '/';
        normalized << parts[index];
    }
    return normalized.str();
}

std::string resolveArchivePath(const std::string& currentModel,
                               const char* referenced,
                               std::string& error) {
    if (!referenced || *referenced == '\0') return currentModel;
    std::string value = referenced;
    if (!value.empty() && value.front() != '/') {
        const size_t slash = currentModel.find_last_of('/');
        if (slash != std::string::npos) value = currentModel.substr(0, slash + 1) + value;
    }
    return canonicalArchivePath(value, error);
}

class ArchiveReader {
public:
    explicit ArchiveReader(const std::vector<uint8_t>& data, const std::atomic_bool* cancelRequested)
        : cancelRequested_(cancelRequested) {
        throwIfCanceled(cancelRequested_);
        if (mz_zip_reader_init_mem(&archive_, data.data(), data.size(), 0)) initialized_ = true;
    }
    ~ArchiveReader() { if (initialized_) mz_zip_reader_end(&archive_); }
    ArchiveReader(const ArchiveReader&) = delete;
    ArchiveReader& operator=(const ArchiveReader&) = delete;

    void checkCanceled() const { throwIfCanceled(cancelRequested_); }

    bool initialized() const { checkCanceled(); return initialized_; }
    mz_uint fileCount() const { checkCanceled(); return initialized_ ? mz_zip_reader_get_num_files(const_cast<mz_zip_archive*>(&archive_)) : 0; }

    int locate(const std::string& path, mz_uint flags = MZ_ZIP_FLAG_CASE_SENSITIVE) {
        checkCanceled();
        return initialized_ ? mz_zip_reader_locate_file(&archive_, path.c_str(), nullptr, flags) : -1;
    }

    bool stat(int index, mz_zip_archive_file_stat& stat, std::string& error) {
        checkCanceled();
        if (index < 0 || !mz_zip_reader_file_stat(&archive_, static_cast<mz_uint>(index), &stat)) {
            error = "3MF-ZIP-Eintrag konnte nicht geprüft werden.";
            return false;
        }
        if (stat.m_is_directory) { error = "Erwarteter 3MF-Dateieintrag ist ein Verzeichnis."; return false; }
        if (stat.m_is_encrypted) { error = "Verschlüsselte 3MF-Einträge werden nicht unterstützt."; return false; }
        return true;
    }

    bool extract(int index, uint64_t maximum, std::vector<uint8_t>& output, std::string& error) {
        mz_zip_archive_file_stat entry{};
        if (!stat(index, entry, error)) return false;
        if (entry.m_uncomp_size > maximum) {
            error = "Ein 3MF-Eintrag überschreitet die zulässige entpackte Größe.";
            return false;
        }
        if (entry.m_comp_size == 0 && entry.m_uncomp_size != 0) {
            error = "3MF-Eintrag besitzt eine ungültige komprimierte Größe.";
            return false;
        }
        if (entry.m_uncomp_size > 1024 * 1024 && entry.m_comp_size != 0 &&
            entry.m_uncomp_size / entry.m_comp_size > kMaxCompressionRatio) {
            error = "3MF-Eintrag weist ein verdächtig hohes Kompressionsverhältnis auf.";
            return false;
        }
        if (entry.m_uncomp_size > kMaxTotalExtractedBytes - totalExtracted_) {
            error = "3MF überschreitet das zulässige Gesamtbudget für entpackte Daten.";
            return false;
        }
        size_t extractedSize = 0;
        checkCanceled();
        void* memory = mz_zip_reader_extract_to_heap(&archive_, static_cast<mz_uint>(index), &extractedSize, 0);
        if (cancellationRequested(cancelRequested_)) {
            if (memory) mz_free(memory);
            throw OperationCanceled();
        }
        if (!memory || extractedSize != entry.m_uncomp_size) {
            if (memory) mz_free(memory);
            error = "3MF-Eintrag konnte nicht vollständig entpackt werden.";
            return false;
        }
        output.assign(static_cast<const uint8_t*>(memory), static_cast<const uint8_t*>(memory) + extractedSize);
        mz_free(memory);
        totalExtracted_ += extractedSize;
        return true;
    }

    bool extractText(int index, uint64_t maximum, std::string& output, std::string& error) {
        std::vector<uint8_t> bytes;
        if (!extract(index, maximum, bytes, error)) return false;
        output.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    }

    bool findFirstModel(std::string& path, std::string& error) {
        int index = locate("3D/3dmodel.model");
        if (index >= 0) { path = "3D/3dmodel.model"; return true; }
        const mz_uint count = fileCount();
        for (mz_uint fileIndex = 0; fileIndex < count; ++fileIndex) {
            if ((fileIndex & 0xFFu) == 0) checkCanceled();
            mz_zip_archive_file_stat entry{};
            if (!mz_zip_reader_file_stat(&archive_, fileIndex, &entry)) continue;
            std::string name = entry.m_filename ? entry.m_filename : "";
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (lower.size() >= 6 && lower.ends_with(".model")) {
                path = canonicalArchivePath(name, error);
                return !path.empty();
            }
        }
        error = "3MF enthält kein lesbares 3D-Modell.";
        return false;
    }

private:
    mz_zip_archive archive_{};
    bool initialized_ = false;
    uint64_t totalExtracted_ = 0;
    const std::atomic_bool* cancelRequested_ = nullptr;
};

struct ResourceKey {
    std::string document;
    int id = 0;
    bool operator==(const ResourceKey&) const = default;
};

struct ResourceKeyHash {
    size_t operator()(const ResourceKey& key) const noexcept {
        return std::hash<std::string>{}(key.document) ^
               (static_cast<size_t>(static_cast<uint32_t>(key.id)) * 0x9e3779b97f4a7c15ull);
    }
};

struct ResourceObject {
    std::string name;
    TriangleMesh mesh;
    struct Component { ResourceKey target; Transform transform = identityTransform(); };
    std::vector<Component> components;
};

struct BuildItem {
    ResourceKey target;
    Transform transform = identityTransform();
};

struct ParseContext {
    explicit ParseContext(ArchiveReader& archiveReader, const std::atomic_bool* cancel)
        : archive(archiveReader), cancelRequested(cancel) {}
    ArchiveReader& archive;
    std::unordered_map<ResourceKey, ResourceObject, ResourceKeyHash> resources;
    std::unordered_map<ResourceKey, std::vector<uint32_t>, ResourceKeyHash> materialGroups;
    std::vector<MeshMaterial> materials;
    bool projectPaletteLoaded = false;
    std::set<std::string> compatibilityWarnings;
    std::unordered_set<std::string> loadedDocuments;
    std::vector<BuildItem> buildItems;
    std::string rootDocument;
    size_t totalVertices = 0;
    size_t totalTriangles = 0;
    size_t totalComponents = 0;
    size_t paintedTriangles = 0;
    size_t unsupportedPaintTriangles = 0;
    size_t gradientTriangles = 0;
    std::string error;
    const std::atomic_bool* cancelRequested = nullptr;
};

std::string normalizedColor(std::string color) {
    if (color.size() == 9 && color.front() == '#') color.resize(7);
    if (color.size() != 7 || color.front() != '#') return "#BCBCBC";
    for (size_t index = 1; index < color.size(); ++index) {
        const unsigned char value = static_cast<unsigned char>(color[index]);
        if (!std::isxdigit(value)) return "#BCBCBC";
        color[index] = static_cast<char>(std::toupper(value));
    }
    return color;
}

uint32_t ensureMaterial(ParseContext& context, std::string name, std::string color) {
    color = normalizedColor(std::move(color));
    for (size_t index = 0; index < context.materials.size(); ++index) {
        if (context.materials[index].color == color &&
            (context.projectPaletteLoaded || name.empty() || context.materials[index].name == name))
            return static_cast<uint32_t>(index + 1);
    }
    if (context.materials.size() >= kMaxMaterials) {
        context.compatibilityWarnings.insert(
            "Mehr als 256 Materialien/Farben wurden auf die vorhandene Palette begrenzt.");
        return 1;
    }
    if (name.empty()) name = "Filament " + std::to_string(context.materials.size() + 1);
    context.materials.push_back({std::move(name), std::move(color)});
    return static_cast<uint32_t>(context.materials.size());
}

const tinyxml2::XMLAttribute* attributeByLocalName(const tinyxml2::XMLElement* element,
                                                  const char* wanted) {
    if (!element) return nullptr;
    for (const tinyxml2::XMLAttribute* attribute = element->FirstAttribute();
         attribute; attribute = attribute->Next())
        if (std::strcmp(localName(attribute->Name()), wanted) == 0) return attribute;
    return nullptr;
}

uint32_t materialFromProperty(const ParseContext& context,
                              const std::string& documentPath,
                              int propertyId, int propertyIndex) {
    if (propertyId <= 0 || propertyIndex < 0) return 0;
    const auto group = context.materialGroups.find({documentPath, propertyId});
    if (group == context.materialGroups.end() ||
        static_cast<size_t>(propertyIndex) >= group->second.size()) return 0;
    return group->second[static_cast<size_t>(propertyIndex)];
}

uint32_t decodeSimplePaintColor(const char* text, bool& unsupported) {
    unsupported = false;
    if (!text || *text == '\0' || std::strcmp(text, "0") == 0) return 0;
    std::string value(text);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    if (value == "4") return 1;
    if (value == "8") return 2;
    // Bambu's TriangleSelector stores an unsplit painted face as
    // <remainder><zero or more F><C>. This extends the old two-nibble
    // representation and currently covers Extruder1 through Extruder32.
    if (value.size() >= 2 && value.back() == 'C') {
        const char remainderDigit = value.front();
        int remainder = remainderDigit >= '0' && remainderDigit <= '9'
            ? remainderDigit - '0'
            : remainderDigit >= 'A' && remainderDigit <= 'E'
                ? remainderDigit - 'A' + 10 : -1;
        bool continuationValid = remainder >= 0;
        for (size_t index = 1; index + 1 < value.size(); ++index)
            continuationValid = continuationValid && value[index] == 'F';
        if (continuationValid) {
            const uint32_t slot = 3u + static_cast<uint32_t>(remainder) +
                                  static_cast<uint32_t>(value.size() - 2) * 15u;
            if (slot <= 32u) return slot;
        }
    }
    unsupported = true;
    return 0;
}

bool queryCoordinate(tinyxml2::XMLElement* element, const char* name, double& value) {
    return element->QueryDoubleAttribute(name, &value) == tinyxml2::XML_SUCCESS && finite(value);
}

bool parsePositiveInt(const char* text, int& value, int maximum = std::numeric_limits<int>::max()) {
    if (text == nullptr || *text == '\0') return false;
    int parsed = 0;
    const char* end = text + std::strlen(text);
    const auto result = std::from_chars(text, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed <= 0 || parsed > maximum) return false;
    value = parsed;
    return true;
}

bool appendMesh(TriangleMesh& destination, const TriangleMesh& source,
                const Transform& transform, std::string& error,
                const std::atomic_bool* cancelRequested) {
    if (source.vertices.size() > kMaxOutputVertices - destination.vertices.size() ||
        source.triangles.size() > kMaxOutputTriangles - destination.triangles.size() ||
        destination.vertices.size() + source.vertices.size() > std::numeric_limits<uint32_t>::max()) {
        error = "3MF-Geometrie ist nach dem Auflösen der Komponenten zu groß.";
        return false;
    }
    const uint32_t base = static_cast<uint32_t>(destination.vertices.size());
    const size_t previousTriangles = destination.triangles.size();
    if (destination.vertices.empty()) destination.defaultMaterial = std::max(source.defaultMaterial, 1u);
    const bool sourceNeedsExplicit = !source.triangleMaterials.empty() ||
                                     source.defaultMaterial != destination.defaultMaterial;
    const bool preserveExplicitMaterials = !destination.triangleMaterials.empty() || sourceNeedsExplicit;
    if (destination.triangleMaterials.empty() && sourceNeedsExplicit)
        destination.triangleMaterials.assign(previousTriangles, destination.defaultMaterial);
    destination.vertices.reserve(destination.vertices.size() + source.vertices.size());
    for (size_t vertexIndex = 0; vertexIndex < source.vertices.size(); ++vertexIndex) {
        if ((vertexIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
        Vec3 vertex = transformed(source.vertices[vertexIndex], transform);
        if (!coordinateInRange(vertex)) { error = "3MF-Transformation erzeugt ungültige oder extrem große Koordinaten."; return false; }
        destination.vertices.push_back(vertex);
        destination.bounds.expand(vertex);
    }
    destination.triangles.reserve(destination.triangles.size() + source.triangles.size());
    for (size_t triangleIndex = 0; triangleIndex < source.triangles.size(); ++triangleIndex) {
        if ((triangleIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
        const auto& triangle = source.triangles[triangleIndex];
        if (triangle[0] >= source.vertices.size() || triangle[1] >= source.vertices.size() ||
            triangle[2] >= source.vertices.size() || triangle[0] == triangle[1] ||
            triangle[1] == triangle[2] || triangle[2] == triangle[0]) {
            error = "3MF enthält einen ungültigen oder degenerierten Dreiecksindex.";
            return false;
        }
        destination.triangles.push_back({triangle[0] + base, triangle[1] + base, triangle[2] + base});
        if (preserveExplicitMaterials) {
            uint32_t material = source.triangleMaterials.empty()
                ? source.defaultMaterial : source.triangleMaterials[triangleIndex];
            if (material == 0 && source.defaultMaterial != destination.defaultMaterial)
                material = source.defaultMaterial;
            destination.triangleMaterials.push_back(material);
        }
    }
    return true;
}

bool parseDocument(ParseContext& context, const std::string& requestedPath, bool isRoot);

bool parseResources(ParseContext& context, tinyxml2::XMLElement* model,
                    const std::string& documentPath, double scale,
                    std::vector<std::string>& referencedDocuments) {
    auto* resourcesElement = firstChild(model, "resources");
    // Material/property resources are parsed first because objects and their
    // triangles may refer to a group declared later in the XML document.
    for (auto* element = resourcesElement ? resourcesElement->FirstChildElement() : nullptr;
         element; element = element->NextSiblingElement()) {
        throwIfCanceled(context.cancelRequested);
        const char* type = localName(element->Name());
        if (std::strcmp(type, "basematerials") != 0 &&
            std::strcmp(type, "colorgroup") != 0) {
            if (std::strcmp(type, "object") != 0 &&
                (std::strstr(type, "texture") != nullptr ||
                 std::strcmp(type, "compositematerials") == 0 ||
                 std::strcmp(type, "multiproperties") == 0 ||
                 std::strcmp(type, "beamlattice") == 0 ||
                 std::strcmp(type, "slicestack") == 0))
                context.compatibilityWarnings.insert(
                    "3MF enthält Textur-, Verbundmaterial-, Gitter- oder Slice-Ressourcen, die nicht bearbeitbar übernommen werden.");
            continue;
        }
        int id = 0;
        if (element->QueryIntAttribute("id", &id) != tinyxml2::XML_SUCCESS || id <= 0) {
            context.error = "3MF enthält eine Farbgruppe ohne gültige Resource-ID.";
            return false;
        }
        std::vector<uint32_t> slots;
        for (auto* entry = element->FirstChildElement(); entry; entry = entry->NextSiblingElement()) {
            const char* entryType = localName(entry->Name());
            std::string name;
            std::string color;
            if (std::strcmp(type, "basematerials") == 0 && std::strcmp(entryType, "base") == 0) {
                if (const char* value = entry->Attribute("name")) name = value;
                if (const char* value = entry->Attribute("displaycolor")) color = value;
            } else if (std::strcmp(type, "colorgroup") == 0 && std::strcmp(entryType, "color") == 0) {
                if (const char* value = entry->Attribute("color")) color = value;
            } else {
                continue;
            }
            if (color.size() == 9 && color.substr(7) != "FF" && color.substr(7) != "ff")
                context.compatibilityWarnings.insert(
                    "Transparenz aus 3MF-Materialfarben wird nicht als Filamenteigenschaft exportiert.");
            slots.push_back(ensureMaterial(context, std::move(name), std::move(color)));
        }
        context.materialGroups[{documentPath, id}] = std::move(slots);
    }

    for (auto* object = resourcesElement ? resourcesElement->FirstChildElement() : nullptr;
         object; object = object->NextSiblingElement()) {
        throwIfCanceled(context.cancelRequested);
        if (std::strcmp(localName(object->Name()), "object") != 0) continue;
        int id = 0;
        if (object->QueryIntAttribute("id", &id) != tinyxml2::XML_SUCCESS || id <= 0) {
            context.error = "3MF enthält ein Objekt ohne gültige positive Resource-ID.";
            return false;
        }
        if (context.resources.size() >= kMaxResources) {
            context.error = "3MF enthält zu viele Ressourcenobjekte.";
            return false;
        }
        ResourceKey key{documentPath, id};
        if (context.resources.find(key) != context.resources.end()) {
            context.error = "3MF enthält eine doppelte Resource-ID im selben Modellteil.";
            return false;
        }
        ResourceObject resource;
        int objectPropertyId = 0;
        int objectPropertyIndex = 0;
        if (const auto* attribute = attributeByLocalName(object, "pid"))
            parsePositiveInt(attribute->Value(), objectPropertyId);
        if (const auto* attribute = attributeByLocalName(object, "pindex")) {
            const char* value = attribute->Value();
            const char* end = value + std::strlen(value);
            std::from_chars(value, end, objectPropertyIndex);
        }
        if (const uint32_t material = materialFromProperty(
                context, documentPath, objectPropertyId, objectPropertyIndex))
            resource.mesh.defaultMaterial = material;
        if (const char* name = object->Attribute("name")) {
            resource.name = name;
            if (resource.name.size() > 1024 * 1024) {
                context.error = "3MF enthält einen unzulässig langen Objektnamen.";
                return false;
            }
        }

        if (auto* mesh = firstChild(object, "mesh")) {
            if (auto* vertices = firstChild(mesh, "vertices")) {
                size_t vertexOrdinal = 0;
                for (auto* vertex = vertices->FirstChildElement(); vertex; vertex = vertex->NextSiblingElement()) {
                    if ((++vertexOrdinal & 0x3FFFu) == 0) throwIfCanceled(context.cancelRequested);
                    if (std::strcmp(localName(vertex->Name()), "vertex") != 0) continue;
                    Vec3 point;
                    if (!queryCoordinate(vertex, "x", point.x) || !queryCoordinate(vertex, "y", point.y) ||
                        !queryCoordinate(vertex, "z", point.z)) {
                        context.error = "3MF enthält einen Vertex mit fehlenden, NaN- oder unendlichen Koordinaten.";
                        return false;
                    }
                    point = point * scale;
                    if (!coordinateInRange(point)) { context.error = "3MF-Koordinate ist ungültig oder extrem groß."; return false; }
                    if (++context.totalVertices > kMaxVerticesTotal) {
                        context.error = "3MF enthält zu viele Vertices.";
                        return false;
                    }
                    resource.mesh.vertices.push_back(point);
                    resource.mesh.bounds.expand(point);
                }
            }
            if (auto* triangles = firstChild(mesh, "triangles")) {
                size_t triangleOrdinal = 0;
                for (auto* triangle = triangles->FirstChildElement(); triangle; triangle = triangle->NextSiblingElement()) {
                    if ((++triangleOrdinal & 0x3FFFu) == 0) throwIfCanceled(context.cancelRequested);
                    if (std::strcmp(localName(triangle->Name()), "triangle") != 0) continue;
                    int v1 = -1, v2 = -1, v3 = -1;
                    if (triangle->QueryIntAttribute("v1", &v1) != tinyxml2::XML_SUCCESS ||
                        triangle->QueryIntAttribute("v2", &v2) != tinyxml2::XML_SUCCESS ||
                        triangle->QueryIntAttribute("v3", &v3) != tinyxml2::XML_SUCCESS ||
                        v1 < 0 || v2 < 0 || v3 < 0 || v1 == v2 || v2 == v3 || v1 == v3 ||
                        static_cast<size_t>(std::max({v1, v2, v3})) >= resource.mesh.vertices.size()) {
                        context.error = "3MF enthält einen ungültigen oder degenerierten Dreiecksindex.";
                        return false;
                    }
                    if (++context.totalTriangles > kMaxTrianglesTotal) {
                        context.error = "3MF enthält zu viele Dreiecke.";
                        return false;
                    }
                    resource.mesh.triangles.push_back({static_cast<uint32_t>(v1),
                                                       static_cast<uint32_t>(v2),
                                                       static_cast<uint32_t>(v3)});

                    uint32_t material = 0;
                    bool explicitlyAssigned = false;
                    const tinyxml2::XMLAttribute* paint = attributeByLocalName(triangle, "paint_color");
                    if (!paint) paint = attributeByLocalName(triangle, "mmu_segmentation");
                    if (paint) {
                        bool unsupported = false;
                        material = decodeSimplePaintColor(paint->Value(), unsupported);
                        explicitlyAssigned = material != 0;
                        if (unsupported) {
                            ++context.unsupportedPaintTriangles;
                            context.compatibilityWarnings.insert(
                                "Komplex unterteilte Bambu-Flächenbemalung konnte nicht vollständig übernommen werden.");
                        } else if (material != 0) {
                            ++context.paintedTriangles;
                        }
                    }
                    if (!explicitlyAssigned) {
                        int propertyId = objectPropertyId;
                        int p1 = objectPropertyIndex;
                        if (const auto* attribute = attributeByLocalName(triangle, "pid"))
                            parsePositiveInt(attribute->Value(), propertyId);
                        auto parseIndex = [&](const char* name, int& target) {
                            if (const auto* attribute = attributeByLocalName(triangle, name)) {
                                const char* value = attribute->Value();
                                const char* end = value + std::strlen(value);
                                const auto parsed = std::from_chars(value, end, target);
                                return parsed.ec == std::errc{} && parsed.ptr == end && target >= 0;
                            }
                            return true;
                        };
                        if (!parseIndex("p1", p1)) {
                            context.error = "3MF enthält einen ungültigen Materialindex an einem Dreieck.";
                            return false;
                        }
                        int p2 = p1;
                        int p3 = p1;
                        if (!parseIndex("p2", p2) || !parseIndex("p3", p3)) {
                            context.error = "3MF enthält einen ungültigen Materialindex an einem Dreieck.";
                            return false;
                        }
                        const uint32_t first = materialFromProperty(context, documentPath, propertyId, p1);
                        const uint32_t second = materialFromProperty(context, documentPath, propertyId, p2);
                        const uint32_t third = materialFromProperty(context, documentPath, propertyId, p3);
                        material = first;
                        explicitlyAssigned = material != 0;
                        if ((second != 0 && second != first) || (third != 0 && third != first)) {
                            ++context.gradientTriangles;
                            context.compatibilityWarnings.insert(
                                "3MF-Verlaufsfarben pro Dreieck wurden auf die erste Eckfarbe reduziert.");
                        }
                    }
                    if (explicitlyAssigned && resource.mesh.triangleMaterials.empty())
                        resource.mesh.triangleMaterials.assign(resource.mesh.triangles.size() - 1, 0);
                    if (!resource.mesh.triangleMaterials.empty())
                        resource.mesh.triangleMaterials.push_back(material);
                    for (const tinyxml2::XMLAttribute* attribute = triangle->FirstAttribute();
                         attribute; attribute = attribute->Next()) {
                        const char* attributeName = localName(attribute->Name());
                        if (std::strcmp(attributeName, "v1") == 0 ||
                            std::strcmp(attributeName, "v2") == 0 ||
                            std::strcmp(attributeName, "v3") == 0 ||
                            std::strcmp(attributeName, "pid") == 0 ||
                            std::strcmp(attributeName, "p1") == 0 ||
                            std::strcmp(attributeName, "p2") == 0 ||
                            std::strcmp(attributeName, "p3") == 0 ||
                            std::strcmp(attributeName, "paint_color") == 0 ||
                            std::strcmp(attributeName, "mmu_segmentation") == 0) continue;
                        context.compatibilityWarnings.insert(
                            "Zusätzliche Flächenattribute (z. B. Support-, Naht- oder Fuzzy-Skin-Bemalung) werden nicht übernommen.");
                        break;
                    }
                }
            }
        }

        if (auto* components = firstChild(object, "components")) {
            context.compatibilityWarnings.insert(
                "3MF-Komponenten und Instanzen wurden für die Bearbeitung geometrisch zusammengeführt; ihre Hierarchie geht beim Export verloren.");
            for (auto* component = components->FirstChildElement(); component;
                 component = component->NextSiblingElement()) {
                throwIfCanceled(context.cancelRequested);
                if (std::strcmp(localName(component->Name()), "component") != 0) continue;
                ResourceObject::Component entry;
                if (component->QueryIntAttribute("objectid", &entry.target.id) != tinyxml2::XML_SUCCESS ||
                    entry.target.id <= 0 || !parseTransform(component->Attribute("transform"), entry.transform)) {
                    context.error = "3MF enthält eine Komponente mit ungültiger ID oder Transformation.";
                    return false;
                }
                const char* referencedPath = nullptr;
                for (const tinyxml2::XMLAttribute* attribute = component->FirstAttribute();
                     attribute; attribute = attribute->Next()) {
                    if (std::strcmp(localName(attribute->Name()), "path") == 0) {
                        referencedPath = attribute->Value();
                        break;
                    }
                }
                entry.target.document = resolveArchivePath(documentPath, referencedPath, context.error);
                if (entry.target.document.empty()) return false;
                entry.transform[9] *= scale;
                entry.transform[10] *= scale;
                entry.transform[11] *= scale;
                if (++context.totalComponents > kMaxComponentsTotal) {
                    context.error = "3MF enthält zu viele Komponentenreferenzen.";
                    return false;
                }
                resource.components.push_back(entry);
                if (entry.target.document != documentPath)
                    referencedDocuments.push_back(entry.target.document);
            }
        }
        context.resources.emplace(std::move(key), std::move(resource));
    }
    return true;
}

bool parseDocument(ParseContext& context, const std::string& requestedPath, bool isRoot) {
    throwIfCanceled(context.cancelRequested);
    std::string path = canonicalArchivePath(requestedPath, context.error);
    if (path.empty()) return false;
    if (!context.loadedDocuments.insert(path).second) return true;
    const int index = context.archive.locate(path);
    if (index < 0) {
        context.error = "Ein referenzierter 3MF-Modellteil fehlt im Archiv: " + path;
        return false;
    }
    std::string xml;
    if (!context.archive.extractText(index, kMaxModelEntryBytes, xml, context.error)) return false;
    tinyxml2::XMLDocument document;
    if (document.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
        context.error = "3MF-Modell-XML ist ungültig: " + path;
        return false;
    }
    auto* model = document.RootElement();
    if (!model || std::strcmp(localName(model->Name()), "model") != 0) {
        context.error = "3MF-Modellwurzel fehlt: " + path;
        return false;
    }
    const double scale = unitScale(model->Attribute("unit"));
    if (scale <= 0.0) { context.error = "3MF verwendet eine unbekannte oder ungültige Einheit."; return false; }
    for (auto* child = model->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (std::strcmp(localName(child->Name()), "metadata") != 0) continue;
        const char* name = child->Attribute("name");
        if (name && (std::strcmp(name, "Application") == 0 ||
                     std::strcmp(name, "BambuStudio:3mfVersion") == 0 ||
                     std::strcmp(name, "Generator") == 0)) continue;
        context.compatibilityWarnings.insert(
            "Eigene 3MF-Dokumentmetadaten wie Autor, Beschreibung oder Copyright werden nicht neu exportiert.");
        break;
    }

    std::vector<std::string> referencedDocuments;
    if (!parseResources(context, model, path, scale, referencedDocuments)) return false;

    if (isRoot) {
        context.rootDocument = path;
        if (auto* build = firstChild(model, "build")) {
            for (auto* child = model->FirstChildElement(); child; child = child->NextSiblingElement()) {
                if (std::strcmp(localName(child->Name()), "assemble") == 0) {
                    context.compatibilityWarnings.insert(
                        "3MF-Montagebeziehungen werden beim Geometrieexport nicht erhalten.");
                    break;
                }
            }
            for (auto* item = build->FirstChildElement(); item; item = item->NextSiblingElement()) {
                throwIfCanceled(context.cancelRequested);
                if (std::strcmp(localName(item->Name()), "item") != 0) continue;
                BuildItem buildItem;
                buildItem.target.document = path;
                if (item->QueryIntAttribute("objectid", &buildItem.target.id) != tinyxml2::XML_SUCCESS ||
                    buildItem.target.id <= 0 || !parseTransform(item->Attribute("transform"), buildItem.transform)) {
                    context.error = "3MF enthält einen ungültigen Build-Eintrag.";
                    return false;
                }
                buildItem.transform[9] *= scale;
                buildItem.transform[10] *= scale;
                buildItem.transform[11] *= scale;
                if (context.buildItems.size() >= kMaxBuildItems) {
                    context.error = "3MF enthält zu viele Build-Einträge.";
                    return false;
                }
                context.buildItems.push_back(buildItem);
            }
        }
    }

    for (const std::string& referenced : referencedDocuments) {
        throwIfCanceled(context.cancelRequested);
        if (!parseDocument(context, referenced, false)) return false;
    }
    return true;
}

bool flattenResource(const ParseContext& context, const ResourceKey& key,
                     const Transform& transform, TriangleMesh& output,
                     std::unordered_set<ResourceKey, ResourceKeyHash>& visiting,
                     size_t depth, std::string& error) {
    throwIfCanceled(context.cancelRequested);
    if (depth > kMaxFlattenDepth) {
        error = "3MF-Komponentenhierarchie ist zu tief.";
        return false;
    }
    const auto found = context.resources.find(key);
    if (found == context.resources.end()) {
        error = "3MF-Komponente verweist auf ein unbekanntes Ressourcenobjekt.";
        return false;
    }
    if (!visiting.insert(key).second) {
        error = "3MF enthält eine zyklische Komponentenreferenz.";
        return false;
    }
    bool appended = false;
    if (!found->second.mesh.triangles.empty()) {
        if (!appendMesh(output, found->second.mesh, transform, error, context.cancelRequested)) {
            visiting.erase(key);
            return false;
        }
        appended = true;
    }
    for (const auto& component : found->second.components) {
        throwIfCanceled(context.cancelRequested);
        const Transform childTransform = composed(component.transform, transform);
        if (!transformInRange(childTransform)) {
            error = "3MF-Komponenten erzeugen eine ungültige oder zu große Transformation.";
            visiting.erase(key);
            return false;
        }
        if (!flattenResource(context, component.target, childTransform, output,
                             visiting, depth + 1, error)) {
            visiting.erase(key);
            return false;
        }
        appended = true;
    }
    visiting.erase(key);
    if (!appended) {
        error = "3MF-Ressourcenobjekt enthält weder ein Mesh noch verwendbare Komponenten.";
        return false;
    }
    return true;
}

std::string xmlEscapeAttribute(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (unsigned char character : value) {
        switch (character) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&apos;"; break;
            default:
                if (character >= 0x20 || character == '\t' || character == '\n' || character == '\r')
                    output.push_back(static_cast<char>(character));
                break;
        }
    }
    return output;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream output;
    for (unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(character) << std::dec << std::setfill(' ');
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    return output.str();
}

bool checkedAdd(uint64_t& total, uint64_t value, uint64_t maximum) {
    if (value > maximum - total) return false;
    total += value;
    return true;
}

bool checkedMultiply(uint64_t value, uint64_t multiplier, uint64_t& product) {
    if (value != 0 && multiplier > std::numeric_limits<uint64_t>::max() / value) return false;
    product = value * multiplier;
    return true;
}

bool validateExportObjects(const std::vector<ThreeMfObject>& objects, std::string& error,
                           const std::atomic_bool* cancelRequested) {
    if (objects.empty()) { error = "Keine Objekte zum Exportieren vorhanden."; return false; }
    if (objects.size() > kMaxResources) { error = "Zu viele Objekte für den 3MF-Export."; return false; }
    size_t totalVertices = 0;
    size_t totalTriangles = 0;
    uint64_t estimatedModelBytes = 4096;
    uint64_t estimatedSettingsBytes = 1024;
    for (const ThreeMfObject& object : objects) {
        throwIfCanceled(cancelRequested);
        const TriangleMesh& mesh = object.meshData();
        uint64_t escapedNameBytes = 0;
        uint64_t vertexBytes = 0;
        uint64_t triangleBytes = 0;
        if (!checkedMultiply(static_cast<uint64_t>(object.name.size()), 6, escapedNameBytes) ||
            !checkedMultiply(static_cast<uint64_t>(mesh.vertices.size()), 160, vertexBytes) ||
            !checkedMultiply(static_cast<uint64_t>(mesh.triangles.size()), 128, triangleBytes) ||
            !checkedAdd(estimatedModelBytes, 512, kMaxModelEntryBytes) ||
            !checkedAdd(estimatedModelBytes, escapedNameBytes, kMaxModelEntryBytes) ||
            !checkedAdd(estimatedModelBytes, vertexBytes, kMaxModelEntryBytes) ||
            !checkedAdd(estimatedModelBytes, triangleBytes, kMaxModelEntryBytes) ||
            !checkedAdd(estimatedSettingsBytes, 512, kMaxMetadataBytes) ||
            !checkedAdd(estimatedSettingsBytes, escapedNameBytes, kMaxMetadataBytes)) {
            error = "3MF-XML würde die zulässige entpackte Größe überschreiten.";
            return false;
        }
        if (object.name.size() > 1024 * 1024 || mesh.vertices.empty() || mesh.triangles.empty()) {
            error = "Mindestens ein 3MF-Objekt besitzt keinen gültigen Namen oder keine Geometrie.";
            return false;
        }
        if (object.plateIndex < 1 || object.plateIndex > 10'000) {
            error = "Mindestens ein 3MF-Objekt besitzt eine ungültige Plattennummer.";
            return false;
        }
        if (mesh.vertices.size() > std::numeric_limits<uint32_t>::max()) {
            error = "Mindestens ein 3MF-Objekt enthält zu viele Vertices.";
            return false;
        }
        if (mesh.defaultMaterial == 0 || mesh.defaultMaterial > kMaxMaterials ||
            (!mesh.triangleMaterials.empty() &&
             mesh.triangleMaterials.size() != mesh.triangles.size()) ||
            std::any_of(mesh.triangleMaterials.begin(), mesh.triangleMaterials.end(),
                        [](uint32_t slot) { return slot > kMaxMaterials; })) {
            error = "Mindestens ein 3MF-Objekt enthält ungültige Farb-/Filamentzuweisungen.";
            return false;
        }
        if (mesh.vertices.size() > kMaxVerticesTotal - totalVertices ||
            mesh.triangles.size() > kMaxTrianglesTotal - totalTriangles) {
            error = "3MF-Export überschreitet die zulässige Gesamtgeometrie.";
            return false;
        }
        totalVertices += mesh.vertices.size();
        totalTriangles += mesh.triangles.size();
        for (size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            if ((vertexIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
            const Vec3& vertex = mesh.vertices[vertexIndex];
            if (!coordinateInRange(vertex)) { error = "3MF-Export enthält ungültige oder extrem große Koordinaten."; return false; }
        }
        for (size_t triangleIndex = 0; triangleIndex < mesh.triangles.size(); ++triangleIndex) {
            if ((triangleIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
            const auto& triangle = mesh.triangles[triangleIndex];
            if (triangle[0] >= mesh.vertices.size() || triangle[1] >= mesh.vertices.size() ||
                triangle[2] >= mesh.vertices.size() || triangle[0] == triangle[1] ||
                triangle[1] == triangle[2] || triangle[2] == triangle[0]) {
                error = "3MF-Export enthält einen ungültigen Dreiecksindex.";
                return false;
            }
        }
    }
    return true;
}

uint32_t resolvedTriangleMaterial(const TriangleMesh& mesh, size_t triangleIndex) {
    if (mesh.triangleMaterials.empty()) return std::max(mesh.defaultMaterial, 1u);
    const uint32_t slot = mesh.triangleMaterials[triangleIndex];
    return slot == 0 ? std::max(mesh.defaultMaterial, 1u) : slot;
}

std::string encodeSimplePaintColor(uint32_t slot) {
    if (slot == 1) return "4";
    if (slot == 2) return "8";
    if (slot < 3 || slot > 32) return {};
    uint32_t value = slot - 3;
    const uint32_t continuationCount = value / 15;
    const uint32_t remainder = value % 15;
    const char digit = remainder < 10 ? static_cast<char>('0' + remainder)
                                      : static_cast<char>('A' + remainder - 10);
    return std::string(1, digit) + std::string(continuationCount, 'F') + 'C';
}

std::string modelXml(const std::vector<ThreeMfObject>& objects,
                     const std::vector<MeshMaterial>& materials,
                     const std::atomic_bool* cancelRequested) {
    std::ostringstream xml;
    xml << std::setprecision(17);
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<model unit=\"millimeter\" xml:lang=\"de-DE\" "
            "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\" "
            "xmlns:m=\"http://schemas.microsoft.com/3dmanufacturing/material/2015/02\" "
            "xmlns:BambuStudio=\"http://schemas.bambulab.com/package/2021\">\n"
        << "  <metadata name=\"Application\">PartSplice 3D</metadata>\n"
        << "  <metadata name=\"BambuStudio:3mfVersion\">1</metadata>\n"
        << "  <metadata name=\"Generator\">PartSplice 3D</metadata>\n"
        << "  <resources>\n";
    const size_t colorGroupId = objects.size() + 1;
    xml << "    <m:basematerials id=\"" << colorGroupId << "\">\n";
    for (size_t index = 0; index < materials.size(); ++index) {
        const MeshMaterial& material = materials[index];
        const std::string name = material.name.empty()
            ? "Filament " + std::to_string(index + 1) : material.name;
        xml << "      <m:base name=\"" << xmlEscapeAttribute(name)
            << "\" displaycolor=\"" << xmlEscapeAttribute(normalizedColor(material.color))
            << "\"/>\n";
    }
    xml << "    </m:basematerials>\n";
    for (size_t index = 0; index < objects.size(); ++index) {
        throwIfCanceled(cancelRequested);
        const TriangleMesh& mesh = objects[index].meshData();
        const std::string safeName = xmlEscapeAttribute(objects[index].name.empty() ? "Objekt" : objects[index].name);
        const uint32_t defaultSlot = std::max(mesh.defaultMaterial, 1u);
        xml << "    <object id=\"" << (index + 1) << "\" type=\"model\" name=\"" << safeName
            << "\" pid=\"" << colorGroupId << "\" pindex=\"" << (defaultSlot - 1) << "\">\n"
            << "      <mesh>\n        <vertices>\n";
        for (size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            if ((vertexIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
            const Vec3& vertex = mesh.vertices[vertexIndex];
            xml << "          <vertex x=\"" << vertex.x << "\" y=\"" << vertex.y
                << "\" z=\"" << vertex.z << "\"/>\n";
        }
        xml << "        </vertices>\n        <triangles>\n";
        for (size_t triangleIndex = 0; triangleIndex < mesh.triangles.size(); ++triangleIndex) {
            if ((triangleIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
            const auto& triangle = mesh.triangles[triangleIndex];
            const uint32_t slot = resolvedTriangleMaterial(mesh, triangleIndex);
            xml << "          <triangle v1=\"" << triangle[0] << "\" v2=\"" << triangle[1]
                << "\" v3=\"" << triangle[2] << "\"";
            if (slot != defaultSlot) {
                xml << " pid=\"" << colorGroupId
                    << "\" p1=\"" << (slot - 1) << "\" p2=\"" << (slot - 1)
                    << "\" p3=\"" << (slot - 1) << "\"";
                const std::string paint = encodeSimplePaintColor(slot);
                if (!paint.empty()) xml << " paint_color=\"" << paint << "\"";
            }
            xml << "/>\n";
        }
        xml << "        </triangles>\n      </mesh>\n    </object>\n";
    }
    xml << "  </resources>\n  <build>\n";
    for (size_t index = 0; index < objects.size(); ++index) {
        throwIfCanceled(cancelRequested);
        xml << "    <item objectid=\"" << (index + 1) << "\"/>\n";
    }
    xml << "  </build>\n</model>\n";
    return xml.str();
}

std::string modelSettingsXml(const std::vector<ThreeMfObject>& objects,
                             const std::atomic_bool* cancelRequested) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<config>\n";
    for (size_t index = 0; index < objects.size(); ++index) {
        throwIfCanceled(cancelRequested);
        const TriangleMesh& mesh = objects[index].meshData();
        const std::string safeName = xmlEscapeAttribute(objects[index].name.empty() ? "Objekt" : objects[index].name);
        xml << "  <object id=\"" << (index + 1) << "\">\n"
            << "    <metadata key=\"name\" value=\"" << safeName << "\"/>\n"
            << "    <metadata key=\"extruder\" value=\"" << std::max(mesh.defaultMaterial, 1u) << "\"/>\n"
            << "    <metadata face_count=\"" << mesh.triangles.size() << "\"/>\n"
            << "  </object>\n";
    }
    std::set<int> plates;
    for (const ThreeMfObject& object : objects) plates.insert(std::max(1, object.plateIndex));
    for (const int plate : plates) {
        throwIfCanceled(cancelRequested);
        xml << "  <plate>\n"
            << "    <metadata key=\"plater_id\" value=\"" << plate << "\"/>\n"
            << "    <metadata key=\"plater_name\" value=\"\"/>\n"
            << "    <metadata key=\"locked\" value=\"false\"/>\n";
        std::set<uint32_t> usedSlots;
        for (const ThreeMfObject& object : objects) {
            if (std::max(1, object.plateIndex) != plate) continue;
            const TriangleMesh& mesh = object.meshData();
            usedSlots.insert(std::max(mesh.defaultMaterial, 1u));
            for (size_t triangleIndex = 0; triangleIndex < mesh.triangles.size(); ++triangleIndex)
                usedSlots.insert(resolvedTriangleMaterial(mesh, triangleIndex));
        }
        std::ostringstream filamentMap;
        bool firstSlot = true;
        for (uint32_t slot : usedSlots) {
            if (!firstSlot) filamentMap << ',';
            firstSlot = false;
            filamentMap << slot;
        }
        xml << "    <metadata key=\"filament_map\" value=\"" << filamentMap.str() << "\"/>\n";
        for (size_t index = 0; index < objects.size(); ++index) {
            throwIfCanceled(cancelRequested);
            if (std::max(1, objects[index].plateIndex) != plate) continue;
            xml << "    <model_instance>\n"
                << "      <metadata key=\"object_id\" value=\"" << (index + 1) << "\"/>\n"
                << "      <metadata key=\"instance_id\" value=\"0\"/>\n"
                << "      <metadata key=\"identify_id\" value=\"" << (index + 1) << "\"/>\n"
                << "    </model_instance>\n";
        }
        xml << "  </plate>\n";
    }
    xml << "  <assemble/>\n</config>\n";
    return xml.str();
}

void applyModelSettings(ParseContext& context,
                        std::unordered_map<ResourceKey, int, ResourceKeyHash>& objectPlates,
                        std::unordered_map<ResourceKey, uint32_t, ResourceKeyHash>& objectMaterials) {
    const int settingsIndex = context.archive.locate("Metadata/model_settings.config", 0);
    if (settingsIndex < 0) return;
    std::string settings;
    std::string ignored;
    if (!context.archive.extractText(settingsIndex, kMaxMetadataBytes, settings, ignored)) return;
    tinyxml2::XMLDocument document;
    if (document.Parse(settings.data(), settings.size()) != tinyxml2::XML_SUCCESS) return;
    auto* config = document.RootElement();
    for (auto* object = config ? config->FirstChildElement() : nullptr; object; object = object->NextSiblingElement()) {
        throwIfCanceled(context.cancelRequested);
        if (std::strcmp(localName(object->Name()), "object") != 0) continue;
        int objectId = 0;
        object->QueryIntAttribute("id", &objectId);
        for (auto* metadata = object->FirstChildElement(); metadata; metadata = metadata->NextSiblingElement()) {
            if (std::strcmp(localName(metadata->Name()), "metadata") != 0 ||
                !metadata->Attribute("key") || !metadata->Attribute("value")) continue;
            const char* key = metadata->Attribute("key");
            if (std::strcmp(key, "name") == 0) {
                const auto resource = context.resources.find({context.rootDocument, objectId});
                if (resource != context.resources.end()) resource->second.name = metadata->Attribute("value");
            } else if (std::strcmp(key, "extruder") == 0) {
                int slot = 0;
                if (parsePositiveInt(metadata->Attribute("value"), slot,
                                     static_cast<int>(kMaxMaterials)))
                    objectMaterials[{context.rootDocument, objectId}] = static_cast<uint32_t>(slot);
            } else {
                context.compatibilityWarnings.insert(
                    "Objekt-/Teil-Metadaten des Slicers (z. B. Quellreferenzen oder individuelle Druckoptionen) werden nicht neu exportiert.");
            }
        }
        for (auto* part = object->FirstChildElement(); part; part = part->NextSiblingElement()) {
            if (std::strcmp(localName(part->Name()), "part") != 0) continue;
            int partId = 0;
            part->QueryIntAttribute("id", &partId);
            if (const char* subtype = part->Attribute("subtype")) {
                if (std::strcmp(subtype, "normal_part") != 0) {
                    context.compatibilityWarnings.insert(
                        "Bambu-Modifikator-, Negativ-, Support- oder sonstige Spezialteile werden als normale Geometrie behandelt.");
                }
            }
            int slot = 0;
            for (auto* metadata = part->FirstChildElement(); metadata;
                 metadata = metadata->NextSiblingElement()) {
                if (std::strcmp(localName(metadata->Name()), "metadata") != 0 ||
                    !metadata->Attribute("key") || !metadata->Attribute("value")) continue;
                if (std::strcmp(metadata->Attribute("key"), "extruder") == 0)
                    parsePositiveInt(metadata->Attribute("value"), slot,
                                     static_cast<int>(kMaxMaterials));
                else
                    context.compatibilityWarnings.insert(
                        "Objekt-/Teil-Metadaten des Slicers (z. B. Quellreferenzen oder individuelle Druckoptionen) werden nicht neu exportiert.");
            }
            if (partId <= 0 || slot <= 0) continue;
            size_t matches = 0;
            for (const auto& [resourceKey, resource] : context.resources) {
                (void)resource;
                if (resourceKey.id != partId) continue;
                objectMaterials[resourceKey] = static_cast<uint32_t>(slot);
                ++matches;
            }
            if (matches > 1)
                context.compatibilityWarnings.insert(
                    "Eine Bambu-Teil-Farbzuweisung war wegen mehrfach verwendeter Objekt-IDs mehrdeutig.");
        }
    }
    int fallbackPlate = 0;
    for (auto* plate = config ? config->FirstChildElement() : nullptr; plate; plate = plate->NextSiblingElement()) {
        throwIfCanceled(context.cancelRequested);
        if (std::strcmp(localName(plate->Name()), "plate") != 0) continue;
        int plateIndex = ++fallbackPlate;
        for (auto* metadata = plate->FirstChildElement(); metadata; metadata = metadata->NextSiblingElement()) {
            if (std::strcmp(localName(metadata->Name()), "metadata") == 0 &&
                metadata->Attribute("key") && std::strcmp(metadata->Attribute("key"), "plater_id") == 0 &&
                metadata->Attribute("value")) {
                int parsedPlate = 0;
                if (parsePositiveInt(metadata->Attribute("value"), parsedPlate, 10'000))
                    plateIndex = parsedPlate;
                break;
            }
        }
        for (auto* instance = plate->FirstChildElement(); instance; instance = instance->NextSiblingElement()) {
            if (std::strcmp(localName(instance->Name()), "model_instance") != 0) continue;
            for (auto* metadata = instance->FirstChildElement(); metadata; metadata = metadata->NextSiblingElement()) {
                if (std::strcmp(localName(metadata->Name()), "metadata") == 0 &&
                    metadata->Attribute("key") && std::strcmp(metadata->Attribute("key"), "object_id") == 0 &&
                    metadata->Attribute("value")) {
                    int objectId = 0;
                    if (parsePositiveInt(metadata->Attribute("value"), objectId))
                        objectPlates[{context.rootDocument, objectId}] = plateIndex;
                    break;
                }
            }
        }
    }
}

bool replaceAtomically(const std::filesystem::path& temporary,
                       const std::filesystem::path& target,
                       std::string& error) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "3MF-Datei konnte nicht atomar ersetzt werden (Windows-Fehler " +
                std::to_string(GetLastError()) + ").";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        error = "3MF-Datei konnte nicht atomar ersetzt werden: " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
#endif
    return true;
}

struct PreservedPackageEntries {};

bool copySafePackageEntries(mz_zip_archive& writer,
                            const std::vector<uint8_t>* sourceData,
                            PreservedPackageEntries& copied,
                            std::string& error,
                            const std::atomic_bool* cancelRequested) {
    if (sourceData == nullptr || sourceData->empty()) return true;
    if (!preflightZipEntryCount(*sourceData, error)) return false;

    mz_zip_archive source{};
    if (!mz_zip_reader_init_mem(&source, sourceData->data(), sourceData->size(), 0)) {
        error = "Ursprüngliches 3MF-Archiv konnte für den verlustarmen Export nicht geöffnet werden.";
        return false;
    }
    uint64_t preservedUncompressed = 0;
    bool ok = true;
    const mz_uint count = mz_zip_reader_get_num_files(&source);
    for (mz_uint index = 0; index < count && ok; ++index) {
        if (cancellationRequested(cancelRequested)) {
            error = "3MF-Export abgebrochen.";
            ok = false;
            break;
        }
        mz_zip_archive_file_stat entry{};
        if (!mz_zip_reader_file_stat(&source, index, &entry) || entry.m_is_directory) continue;
        std::string pathError;
        const std::string path = canonicalArchivePath(entry.m_filename ? entry.m_filename : "", pathError);
        if (path.empty() || !pathError.empty()) continue;
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });

        // Preserve only user-authored auxiliary files. Content types are
        // regenerated below because the original overrides can reference
        // removed geometry, thumbnails, slices or G-code.
        // Old geometry, plate previews, slice metadata and any G-code must never
        // be carried into an archive whose meshes have changed.
        const bool isAuxiliary = lower.rfind("auxiliaries/", 0) == 0;
        if (!isAuxiliary) continue;
        if (entry.m_is_encrypted || entry.m_uncomp_size > 64ull * 1024ull * 1024ull ||
            (entry.m_uncomp_size > 1024 * 1024 && entry.m_comp_size != 0 &&
             entry.m_uncomp_size / entry.m_comp_size > kMaxCompressionRatio) ||
            entry.m_uncomp_size > 128ull * 1024ull * 1024ull - preservedUncompressed) {
            error = "Ein zu erhaltender 3MF-Zusatzeintrag überschreitet das sichere Exportbudget.";
            ok = false;
            break;
        }
        if (!mz_zip_writer_add_from_zip_reader(&writer, &source, index)) {
            error = "Ein sicherer 3MF-Zusatzeintrag konnte nicht übernommen werden.";
            ok = false;
            break;
        }
        preservedUncompressed += entry.m_uncomp_size;
        (void)copied;
    }
    mz_zip_reader_end(&source);
    return ok;
}

bool parseJsonHex4(const std::string& json, size_t position, uint32_t& value) {
    if (position + 4 > json.size()) return false;
    value = 0;
    for (size_t digit = 0; digit < 4; ++digit) {
        const unsigned char character = static_cast<unsigned char>(json[position + digit]);
        value <<= 4;
        if (character >= '0' && character <= '9') value += character - '0';
        else if (character >= 'a' && character <= 'f') value += character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') value += character - 'A' + 10;
        else return false;
    }
    return true;
}

bool appendUtf8(std::string& output, uint32_t codepoint) {
    if (codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) return false;
    if (codepoint <= 0x7Fu) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7FFu) {
        output.push_back(static_cast<char>(0xC0u | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        output.push_back(static_cast<char>(0xE0u | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        output.push_back(static_cast<char>(0xF0u | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        output.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    return true;
}

std::vector<std::string> jsonStringArray(const std::string& json, const char* key) {
    std::vector<std::string> values;
    const std::string marker = std::string("\"") + key + "\"";
    size_t position = json.find(marker);
    if (position == std::string::npos) return values;
    position = json.find('[', position + marker.size());
    if (position == std::string::npos) return values;
    ++position;
    while (position < json.size()) {
        while (position < json.size() &&
               (std::isspace(static_cast<unsigned char>(json[position])) || json[position] == ',')) ++position;
        if (position >= json.size() || json[position] == ']') break;
        if (json[position] != '"') return {};
        ++position;
        std::string value;
        bool closed = false;
        while (position < json.size()) {
            char character = json[position++];
            if (character == '"') { closed = true; break; }
            if (character != '\\') { value.push_back(character); continue; }
            if (position >= json.size()) return {};
            const char escaped = json[position++];
            switch (escaped) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'u': {
                    uint32_t codepoint = 0;
                    if (!parseJsonHex4(json, position, codepoint)) return {};
                    position += 4;
                    if (codepoint >= 0xD800u && codepoint <= 0xDBFFu) {
                        if (position + 6 > json.size() || json[position] != '\\' ||
                            json[position + 1] != 'u') return {};
                        uint32_t low = 0;
                        if (!parseJsonHex4(json, position + 2, low) ||
                            low < 0xDC00u || low > 0xDFFFu) return {};
                        position += 6;
                        codepoint = 0x10000u + ((codepoint - 0xD800u) << 10u) +
                                    (low - 0xDC00u);
                    }
                    if (!appendUtf8(value, codepoint)) return {};
                    break;
                }
                default: return {};
            }
        }
        if (!closed) return {};
        values.push_back(std::move(value));
        if (values.size() >= kMaxMaterials) break;
    }
    return values;
}

void loadProjectMaterialPalette(ParseContext& context, const std::string& projectSettings) {
    const std::vector<std::string> colors = jsonStringArray(projectSettings, "filament_colour");
    const std::vector<std::string> profiles = jsonStringArray(projectSettings, "filament_settings_id");
    const std::vector<std::string> displayNames = jsonStringArray(projectSettings, "partsplice_material_names");
    context.materials.reserve(std::max({colors.size(), profiles.size(), displayNames.size()}));
    const size_t count = std::min(kMaxMaterials,
        std::max({colors.size(), profiles.size(), displayNames.size()}));
    context.projectPaletteLoaded = count > 0;
    for (size_t index = 0; index < count; ++index) {
        MeshMaterial material;
        material.name = index < displayNames.size() && !displayNames[index].empty()
            ? displayNames[index]
            : index < profiles.size() && !profiles[index].empty()
                ? profiles[index] : "Filament " + std::to_string(index + 1);
        material.color = normalizedColor(index < colors.size() ? colors[index] : "#BCBCBC");
        context.materials.push_back(std::move(material));
    }
}

void ensurePaletteSize(std::vector<MeshMaterial>& materials, size_t required) {
    static constexpr const char* fallbackColors[] = {
        "#00AE42", "#F4D03F", "#3498DB", "#E74C3C", "#9B59B6", "#E67E22",
        "#1ABC9C", "#ECF0F1", "#34495E", "#FF69B4", "#8E6E53", "#7F8C8D"
    };
    required = std::min(required, kMaxMaterials);
    while (materials.size() < required) {
        const size_t index = materials.size();
        materials.push_back({"Filament " + std::to_string(index + 1),
                             fallbackColors[index % std::size(fallbackColors)]});
    }
}

} // namespace

ThreeMfLoadResult loadThreeMf(const std::filesystem::path& path,
                               const std::atomic_bool* cancelRequested) {
    ThreeMfLoadResult result;
    try {
        std::string error;
        result.originalFileData = readFileLimited(path, error, cancelRequested);
        if (result.originalFileData.empty()) { result.message = error; return result; }
        if (!preflightZipEntryCount(result.originalFileData, error)) {
            result.message = error;
            return result;
        }
        ArchiveReader archive(result.originalFileData, cancelRequested);
        if (!archive.initialized()) { result.message = "Ungültiger 3MF/ZIP-Container."; return result; }
        if (archive.fileCount() == 0 || archive.fileCount() > kMaxArchiveEntries) {
            result.message = "3MF enthält keine oder zu viele ZIP-Einträge.";
            return result;
        }
        std::string rootModel;
        if (!archive.findFirstModel(rootModel, error)) { result.message = error; return result; }

        const int projectSettingsIndex = archive.locate("Metadata/project_settings.config", 0);
        if (projectSettingsIndex >= 0) {
            if (!archive.extractText(projectSettingsIndex, kMaxMetadataBytes, result.projectSettings, error)) {
                result.message = "3MF-Projekteinstellungen konnten nicht sicher gelesen werden: " + error;
                return result;
            }
        }

        ParseContext context{archive, cancelRequested};
        loadProjectMaterialPalette(context, result.projectSettings);
        if (!parseDocument(context, rootModel, true)) { result.message = context.error; return result; }

        std::unordered_map<ResourceKey, int, ResourceKeyHash> objectPlates;
        std::unordered_map<ResourceKey, uint32_t, ResourceKeyHash> objectMaterials;
        throwIfCanceled(cancelRequested);
        applyModelSettings(context, objectPlates, objectMaterials);
        for (const auto& [key, slot] : objectMaterials) {
            const auto resource = context.resources.find(key);
            if (resource != context.resources.end())
                resource->second.mesh.defaultMaterial = std::max(slot, 1u);
        }

        size_t ordinal = 1;
        size_t totalOutputVertices = 0;
        size_t totalOutputTriangles = 0;
        auto addObject = [&](const ResourceKey& key, const Transform& transform, int plateIndex) -> bool {
            ThreeMfObject output;
            const auto resource = context.resources.find(key);
            output.name = resource != context.resources.end() && !resource->second.name.empty()
                ? resource->second.name : "Objekt " + std::to_string(ordinal);
            output.plateIndex = std::max(1, plateIndex);
            std::unordered_set<ResourceKey, ResourceKeyHash> visiting;
            if (!flattenResource(context, key, transform, output.mesh, visiting, 0, error)) return false;
            const auto materialFound = objectMaterials.find(key);
            if (materialFound != objectMaterials.end())
                output.mesh.defaultMaterial = std::max(materialFound->second, 1u);
            if (!output.mesh.triangles.empty()) {
                if (output.mesh.vertices.size() > kMaxOutputVertices - totalOutputVertices ||
                    output.mesh.triangles.size() > kMaxOutputTriangles - totalOutputTriangles) {
                    error = "3MF-Ausgabegeometrie überschreitet das sichere Gesamtbudget.";
                    return false;
                }
                totalOutputVertices += output.mesh.vertices.size();
                totalOutputTriangles += output.mesh.triangles.size();
                result.objects.push_back(std::move(output));
                ++ordinal;
            }
            return true;
        };

        std::unordered_set<ResourceKey, ResourceKeyHash> instantiatedResources;
        for (const BuildItem& item : context.buildItems) {
            throwIfCanceled(cancelRequested);
            if (!instantiatedResources.insert(item.target).second)
                context.compatibilityWarnings.insert(
                    "Wiederholte 3MF-Instanzen werden als eigenständige Objekte exportiert; ihre Instanzbeziehung geht verloren.");
            const auto plateFound = objectPlates.find(item.target);
            const int plate = plateFound != objectPlates.end() ? plateFound->second : 1;
            if (!addObject(item.target, item.transform, plate)) { result.message = error; return result; }
        }

        if (result.objects.empty()) {
            std::vector<ResourceKey> rootKeys;
            for (const auto& [key, resource] : context.resources)
                if (key.document == context.rootDocument &&
                    (!resource.mesh.triangles.empty() || !resource.components.empty())) rootKeys.push_back(key);
            std::sort(rootKeys.begin(), rootKeys.end(), [](const ResourceKey& first, const ResourceKey& second) {
                return first.id < second.id;
            });
            for (const ResourceKey& key : rootKeys) {
                throwIfCanceled(cancelRequested);
                const auto plateFound = objectPlates.find(key);
                const int plate = plateFound != objectPlates.end() ? plateFound->second : 1;
                if (!addObject(key, identityTransform(), plate)) { result.message = error; return result; }
            }
        }

        result.ok = !result.objects.empty();
        size_t requiredMaterials = context.materials.size();
        for (const ThreeMfObject& object : result.objects) {
            requiredMaterials = std::max(requiredMaterials,
                static_cast<size_t>(std::max(object.mesh.defaultMaterial, 1u)));
            for (const uint32_t material : object.mesh.triangleMaterials)
                requiredMaterials = std::max(requiredMaterials, static_cast<size_t>(material));
        }
        ensurePaletteSize(context.materials, requiredMaterials);
        result.materials = std::move(context.materials);
        result.compatibilityWarnings.assign(context.compatibilityWarnings.begin(),
                                             context.compatibilityWarnings.end());
        result.message = result.ok
            ? std::to_string(result.objects.size()) + " Objekt(e) aus 3MF geladen und geprüft; " +
              std::to_string(result.materials.size()) + " Filament/Farbslot(s) erkannt."
            : "3MF enthält keine verwendbaren Dreiecksobjekte.";
        if (!result.compatibilityWarnings.empty())
            result.message += " Hinweise: " + std::to_string(result.compatibilityWarnings.size()) + ".";
    } catch (const OperationCanceled&) {
        result.message = "Ladevorgang abgebrochen.";
    } catch (const std::bad_alloc&) {
        result.message = "Nicht genügend Arbeitsspeicher zum Laden der 3MF-Datei.";
    } catch (const std::length_error&) {
        result.message = "3MF-Geometrie überschreitet zulässige Größen.";
    } catch (const std::exception& exception) {
        result.message = std::string("Fehler beim Laden der 3MF-Datei: ") + exception.what();
    }
    return result;
}

bool saveThreeMf(const std::filesystem::path& path,
                 const std::vector<ThreeMfObject>& objects,
                 std::string& error,
                 const std::string& preservedProjectSettings,
                 const std::string& fallbackPrinterSettingsId,
                 const std::string& fallbackPrintSettingsId,
                 const std::string& fallbackFilamentSettingsId,
                 const std::atomic_bool* cancelRequested,
                 const std::vector<uint8_t>* preservedSourceArchive,
                 const std::vector<MeshMaterial>* materials) {
    void* archiveMemory = nullptr;
    std::filesystem::path temporary;
    try {
        throwIfCanceled(cancelRequested);
        if (!validateExportObjects(objects, error, cancelRequested)) return false;
        if (preservedProjectSettings.size() > kMaxMetadataBytes) {
            error = "Erhaltene 3MF-Projekteinstellungen sind zu groß.";
            return false;
        }
        std::vector<MeshMaterial> palette = materials ? *materials : std::vector<MeshMaterial>{};
        if (palette.size() > kMaxMaterials) {
            error = "Zu viele Farb-/Filamentslots für den 3MF-Export.";
            return false;
        }
        size_t requiredMaterials = palette.size();
        for (const ThreeMfObject& object : objects) {
            const TriangleMesh& mesh = object.meshData();
            requiredMaterials = std::max(requiredMaterials,
                static_cast<size_t>(std::max(mesh.defaultMaterial, 1u)));
            for (const uint32_t slot : mesh.triangleMaterials)
                requiredMaterials = std::max(requiredMaterials, static_cast<size_t>(slot));
        }
        ensurePaletteSize(palette, std::max<size_t>(requiredMaterials, 1));
        const std::string contentTypes =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
             "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>"
             "<Default Extension=\"png\" ContentType=\"image/png\"/>"
             "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/>"
             "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/>"
             "<Default Extension=\"svg\" ContentType=\"image/svg+xml\"/>"
             "<Default Extension=\"pdf\" ContentType=\"application/pdf\"/>"
             "<Default Extension=\"txt\" ContentType=\"text/plain\"/>"
             "<Default Extension=\"json\" ContentType=\"application/json\"/>"
             "</Types>\n";
        const std::string relationships =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>"
            "</Relationships>\n";
        const std::string model = modelXml(objects, palette, cancelRequested);
        if (model.size() > kMaxModelEntryBytes) {
            error = "Die tatsächlich erzeugte 3MF-Modell-XML überschreitet die zulässige Größe.";
            return false;
        }
        const std::string modelSettings = modelSettingsXml(objects, cancelRequested);
        const std::string printerSettingsId = fallbackPrinterSettingsId.empty()
            ? "Bambu Lab P1S 0.4 nozzle" : fallbackPrinterSettingsId;
        const std::string printSettingsId = fallbackPrintSettingsId.empty()
            ? "0.20mm Standard @BBL X1C" : fallbackPrintSettingsId;
        const std::string filamentSettingsId = fallbackFilamentSettingsId.empty()
            ? "Bambu PLA Basic @BBL P1S 0.4 nozzle" : fallbackFilamentSettingsId;
        std::ostringstream filamentProfiles;
        std::ostringstream filamentColors;
        std::ostringstream materialNames;
        filamentProfiles << '[';
        filamentColors << '[';
        materialNames << '[';
        for (size_t index = 0; index < palette.size(); ++index) {
            if (index != 0) { filamentProfiles << ','; filamentColors << ','; materialNames << ','; }
            filamentProfiles << '\"' << jsonEscape(filamentSettingsId) << '\"';
            filamentColors << '\"' << jsonEscape(normalizedColor(palette[index].color)) << '\"';
            materialNames << '\"' << jsonEscape(palette[index].name) << '\"';
        }
        filamentProfiles << ']';
        filamentColors << ']';
        materialNames << ']';
        const std::string fallbackProjectSettings =
            "{\n"
            "  \"printer_settings_id\": \"" + jsonEscape(printerSettingsId) + "\",\n"
            "  \"print_settings_id\": \"" + jsonEscape(printSettingsId) + "\",\n"
            "  \"filament_settings_id\": " + filamentProfiles.str() + ",\n"
            "  \"filament_colour\": " + filamentColors.str() + ",\n"
            "  \"partsplice_material_names\": " + materialNames.str() + "\n"
            "}\n";
        const std::string& projectSettings = preservedProjectSettings.empty()
            ? fallbackProjectSettings : preservedProjectSettings;
        const std::string sliceInfo =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<config><header>"
            "<header_item key=\"X-BBL-Client-Type\" value=\"slicer\"/>"
            "<header_item key=\"X-BBL-Client-Version\" value=\"02.00.00.00\"/>"
            "</header></config>\n";
        std::set<int> plateIndices;
        for (const ThreeMfObject& object : objects) {
            throwIfCanceled(cancelRequested);
            plateIndices.insert(std::max(1, object.plateIndex));
        }
        std::ostringstream sequence;
        sequence << "{";
        bool firstPlate = true;
        for (const int plate : plateIndices) {
            throwIfCanceled(cancelRequested);
            if (!firstPlate) sequence << ',';
            firstPlate = false;
            sequence << "\"plate_" << plate
                     << "\":{\"nozzle_sequence\":[],\"optimal_assignment\":[],\"sequence\":[]}";
        }
        sequence << "}\n";
        const std::string filamentSequence = sequence.str();

        throwIfCanceled(cancelRequested);
        mz_zip_archive archive{};
        size_t archiveSize = 0;
        if (!mz_zip_writer_init_heap(&archive, 0, 0)) {
            error = "3MF-Archiv konnte nicht begonnen werden.";
            return false;
        }
        auto add = [&](const char* name, const std::string& data, mz_uint level) {
            return mz_zip_writer_add_mem(&archive, name, data.data(), data.size(), level) != 0;
        };
        PreservedPackageEntries preserved;
        const bool copied = copySafePackageEntries(archive, preservedSourceArchive, preserved,
                                                   error, cancelRequested);
        const bool ok = copied &&
                        add("[Content_Types].xml", contentTypes, MZ_BEST_COMPRESSION) &&
                        add("_rels/.rels", relationships, MZ_BEST_COMPRESSION) &&
                        add("3D/3dmodel.model", model, MZ_BEST_SPEED) &&
                        add("Metadata/model_settings.config", modelSettings, MZ_BEST_COMPRESSION) &&
                        add("Metadata/project_settings.config", projectSettings, MZ_BEST_COMPRESSION) &&
                        add("Metadata/slice_info.config", sliceInfo, MZ_BEST_COMPRESSION) &&
                        add("Metadata/filament_sequence.json", filamentSequence, MZ_BEST_COMPRESSION) &&
                        mz_zip_writer_finalize_heap_archive(&archive, &archiveMemory, &archiveSize);
        mz_zip_writer_end(&archive);
        throwIfCanceled(cancelRequested);
        if (!ok || !archiveMemory) {
            if (archiveMemory) { mz_free(archiveMemory); archiveMemory = nullptr; }
            error = "3MF-Archiv konnte nicht fertiggestellt werden.";
            return false;
        }
        if (archiveSize > kMaxArchiveBytes || archiveSize > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
            mz_free(archiveMemory); archiveMemory = nullptr;
            error = "Erzeugtes 3MF-Archiv ist zu groß.";
            return false;
        }

        temporary = reserveSecureTemporarySibling(path);
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (output) {
            constexpr size_t chunkSize = 4u * 1024u * 1024u;
            size_t offset = 0;
            while (offset < archiveSize) {
                throwIfCanceled(cancelRequested);
                const size_t chunk = std::min(chunkSize, archiveSize - offset);
                output.write(static_cast<const char*>(archiveMemory) + offset,
                             static_cast<std::streamsize>(chunk));
                if (!output) break;
                offset += chunk;
            }
        }
        output.flush();
        mz_free(archiveMemory); archiveMemory = nullptr;
        if (!output) {
            error = "3MF-Datei konnte nicht vollständig geschrieben werden.";
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        output.close();
        return replaceAtomically(temporary, path, error);
    } catch (const OperationCanceled&) {
        if (archiveMemory) mz_free(archiveMemory);
        if (!temporary.empty()) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
        error = "3MF-Export abgebrochen.";
    } catch (const std::bad_alloc&) {
        if (archiveMemory) mz_free(archiveMemory);
        if (!temporary.empty()) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
        error = "Nicht genügend Arbeitsspeicher zum Erzeugen der 3MF-Datei.";
    } catch (const std::exception& exception) {
        if (archiveMemory) mz_free(archiveMemory);
        if (!temporary.empty()) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
        error = std::string("3MF-Export fehlgeschlagen: ") + exception.what();
    }
    return false;
}
