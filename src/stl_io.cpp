#include "stl_io.h"
#include "safe_temp.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr uint64_t kMaxStlFileBytes = 512ull * 1024ull * 1024ull;
constexpr uint32_t kMaxStlTriangles = 10'000'000u;
constexpr double kMaxCoordinateMagnitude = 1'000'000'000.0;

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

bool finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

bool coordinateInRange(Vec3 p) {
    return finite(p) && std::abs(p.x) <= kMaxCoordinateMagnitude &&
           std::abs(p.y) <= kMaxCoordinateMagnitude &&
           std::abs(p.z) <= kMaxCoordinateMagnitude;
}

struct QuantKey {
    int64_t x, y, z;
    bool operator==(const QuantKey&) const = default;
};

struct QuantHash {
    size_t operator()(const QuantKey& k) const noexcept {
        auto mix = [](uint64_t x) {
            x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
            x ^= x >> 27; x *= 0x94d049bb133111ebULL;
            x ^= x >> 31; return x;
        };
        return static_cast<size_t>(mix(static_cast<uint64_t>(k.x)) ^
                                   (mix(static_cast<uint64_t>(k.y)) << 1) ^
                                   (mix(static_cast<uint64_t>(k.z)) << 2));
    }
};

struct EdgeKey {
    uint32_t a, b;
    bool operator==(const EdgeKey&) const = default;
};

struct EdgeHash {
    size_t operator()(const EdgeKey& e) const noexcept {
        return static_cast<size_t>((static_cast<uint64_t>(e.a) << 32) ^ e.b);
    }
};

struct EdgeOccurrence {
    uint32_t triangle;
    uint32_t from;
    uint32_t to;
};

float readFloatLE(const char* p) {
    uint32_t u = 0;
    std::memcpy(&u, p, 4);
    if constexpr (std::endian::native == std::endian::big)
        u = ((u & 0x000000FFu) << 24) | ((u & 0x0000FF00u) << 8) |
            ((u & 0x00FF0000u) >> 8) | ((u & 0xFF000000u) >> 24);
    return std::bit_cast<float>(u);
}

uint32_t readU32LE(const char* p) {
    uint32_t u = 0;
    std::memcpy(&u, p, 4);
    if constexpr (std::endian::native == std::endian::big)
        u = ((u & 0x000000FFu) << 24) | ((u & 0x0000FF00u) << 8) |
            ((u & 0x00FF0000u) >> 8) | ((u & 0xFF000000u) >> 24);
    return u;
}

void writeU32LE(std::ostream& os, uint32_t u) {
    if constexpr (std::endian::native == std::endian::big)
        u = ((u & 0x000000FFu) << 24) | ((u & 0x0000FF00u) << 8) |
            ((u & 0x00FF0000u) >> 8) | ((u & 0xFF000000u) >> 24);
    os.write(reinterpret_cast<const char*>(&u), 4);
}

void writeU16LE(std::ostream& os, uint16_t u) {
    if constexpr (std::endian::native == std::endian::big)
        u = static_cast<uint16_t>((u << 8) | (u >> 8));
    os.write(reinterpret_cast<const char*>(&u), 2);
}

void writeFloatLE(std::ostream& os, float f) {
    writeU32LE(os, std::bit_cast<uint32_t>(f));
}

struct RawTriangle { Vec3 p[3]; };

bool validFileSize(std::ifstream& in, uint64_t& size, std::string& error) {
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0) { error = "Dateigröße konnte nicht bestimmt werden."; return false; }
    size = static_cast<uint64_t>(end);
    if (size > kMaxStlFileBytes) { error = "STL-Datei überschreitet die Sicherheitsgrenze von 512 MiB."; return false; }
    in.seekg(0, std::ios::beg);
    return static_cast<bool>(in);
}

bool readBinary(const std::filesystem::path& path, std::vector<RawTriangle>& out, std::string& error,
                const std::atomic_bool* cancelRequested) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "Datei konnte nicht geöffnet werden."; return false; }
    uint64_t size = 0;
    if (!validFileSize(in, size, error)) return false;
    if (size < 84) { error = "Datei ist zu klein für ein binäres STL."; return false; }
    char header[84];
    in.read(header, 84);
    if (!in) { error = "STL-Kopf konnte nicht gelesen werden."; return false; }
    const uint32_t count = readU32LE(header + 80);
    if (count > kMaxStlTriangles) { error = "STL enthält zu viele Dreiecke."; return false; }
    const uint64_t expected = 84ULL + 50ULL * count;
    if (size < expected) { error = "Binäre STL endet vor dem angekündigten Dreiecksblock."; return false; }
    out.reserve(count);
    char rec[50];
    for (uint32_t i = 0; i < count; ++i) {
        if ((i & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
        in.read(rec, 50);
        if (!in) { error = "STL endet unerwartet."; return false; }
        RawTriangle t{};
        for (int v = 0; v < 3; ++v) {
            const char* p = rec + 12 + v * 12;
            t.p[v] = {readFloatLE(p), readFloatLE(p + 4), readFloatLE(p + 8)};
            if (!coordinateInRange(t.p[v])) {
                error = "STL enthält ungültige oder extrem große Koordinaten.";
                return false;
            }
        }
        out.push_back(t);
    }
    return true;
}

bool readAscii(const std::filesystem::path& path, std::vector<RawTriangle>& out, std::string& error,
               const std::atomic_bool* cancelRequested) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "Datei konnte nicht geöffnet werden."; return false; }
    uint64_t size = 0;
    if (!validFileSize(in, size, error)) return false;
    std::string token;
    std::vector<Vec3> verts;
    verts.reserve(static_cast<size_t>(std::min<uint64_t>(size / 24, 3ull * kMaxStlTriangles)));
    size_t tokenCount = 0;
    while (in >> token) {
        if ((++tokenCount & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
        if (token != "vertex") continue;
        Vec3 v;
        if (!(in >> v.x >> v.y >> v.z)) { error = "Ungültige ASCII-STL-Vertexzeile."; return false; }
        if (!coordinateInRange(v)) { error = "STL enthält ungültige oder extrem große Koordinaten."; return false; }
        if (verts.size() >= static_cast<size_t>(3ull * kMaxStlTriangles)) {
            error = "STL enthält zu viele Dreiecke.";
            return false;
        }
        verts.push_back(v);
    }
    if (verts.size() < 3 || verts.size() % 3 != 0) {
        error = "ASCII-STL enthält keine vollständigen Dreiecke.";
        return false;
    }
    out.reserve(verts.size() / 3);
    for (size_t i = 0; i < verts.size(); i += 3)
        out.push_back({verts[i], verts[i + 1], verts[i + 2]});
    return true;
}

TriangleMesh weld(const std::vector<RawTriangle>& raw, size_t& removed,
                  const std::atomic_bool* cancelRequested) {
    AABB rawBox;
    for (size_t triangleIndex = 0; triangleIndex < raw.size(); ++triangleIndex) {
        if ((triangleIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
        for (const Vec3& p : raw[triangleIndex].p) rawBox.expand(p);
    }
    const double eps = std::max(1e-7 * std::max(rawBox.diagonal(), 1.0), 1e-6);
    const double eps2 = eps * eps;
    std::unordered_map<QuantKey, std::vector<uint32_t>, QuantHash> cells;
    cells.reserve(raw.size());
    TriangleMesh mesh;
    mesh.vertices.reserve(raw.size());
    mesh.triangles.reserve(raw.size());

    auto indexFor = [&](Vec3 p) -> uint32_t {
        const QuantKey base{static_cast<int64_t>(std::floor(p.x / eps)),
                            static_cast<int64_t>(std::floor(p.y / eps)),
                            static_cast<int64_t>(std::floor(p.z / eps))};
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const auto found = cells.find({base.x + dx, base.y + dy, base.z + dz});
                    if (found == cells.end()) continue;
                    for (uint32_t candidate : found->second) {
                        const Vec3 delta = mesh.vertices[candidate] - p;
                        if (dot(delta, delta) <= eps2) return candidate;
                    }
                }
            }
        }
        if (mesh.vertices.size() >= std::numeric_limits<uint32_t>::max())
            throw std::length_error("Zu viele STL-Punkte.");
        const uint32_t index = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(p);
        mesh.bounds.expand(p);
        cells[base].push_back(index);
        return index;
    };

    for (size_t triangleIndex = 0; triangleIndex < raw.size(); ++triangleIndex) {
        if ((triangleIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
        const auto& t = raw[triangleIndex];
        std::array<uint32_t, 3> tri{indexFor(t.p[0]), indexFor(t.p[1]), indexFor(t.p[2])};
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]) { ++removed; continue; }
        const Vec3 a = mesh.vertices[tri[0]], b = mesh.vertices[tri[1]], c = mesh.vertices[tri[2]];
        if (length(cross(b - a, c - a)) <= eps2) { ++removed; continue; }
        mesh.triangles.push_back(tri);
    }
    return mesh;
}

bool replaceAtomically(const std::filesystem::path& temporary,
                       const std::filesystem::path& target,
                       std::string& error) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Zieldatei konnte nicht atomar ersetzt werden (Windows-Fehler " +
                std::to_string(GetLastError()) + ").";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        error = "Zieldatei konnte nicht atomar ersetzt werden: " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
#endif
    return true;
}

} // namespace

namespace {

void orientMeshConsistentlyImpl(TriangleMesh& mesh, size_t& nonManifoldEdges, bool& conflict,
                                const std::atomic_bool* cancelRequested) {
    std::unordered_map<EdgeKey, std::vector<EdgeOccurrence>, EdgeHash> edges;
    edges.reserve(mesh.triangles.size() * 3);
    for (uint32_t ti = 0; ti < mesh.triangles.size(); ++ti) {
        if ((ti & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
        const auto& t = mesh.triangles[ti];
        if (t[0] >= mesh.vertices.size() || t[1] >= mesh.vertices.size() || t[2] >= mesh.vertices.size()) {
            conflict = true;
            continue;
        }
        for (int e = 0; e < 3; ++e) {
            const uint32_t from = t[e], to = t[(e + 1) % 3];
            edges[{std::min(from, to), std::max(from, to)}].push_back({ti, from, to});
        }
    }
    for (const auto& [_, occurrences] : edges)
        if (occurrences.size() != 2) ++nonManifoldEdges;

    std::vector<int8_t> flip(mesh.triangles.size(), -1);
    std::vector<std::vector<uint32_t>> components;
    for (uint32_t seed = 0; seed < mesh.triangles.size(); ++seed) {
        if ((seed & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
        if (flip[seed] != -1) continue;
        components.emplace_back();
        auto& component = components.back();
        flip[seed] = 0;
        std::queue<uint32_t> q;
        q.push(seed);
        while (!q.empty()) {
            if ((component.size() & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
            const uint32_t ti = q.front(); q.pop();
            component.push_back(ti);
            const auto t = mesh.triangles[ti];
            for (int e = 0; e < 3; ++e) {
                uint32_t from = t[e], to = t[(e + 1) % 3];
                if (flip[ti]) std::swap(from, to);
                const auto found = edges.find({std::min(from, to), std::max(from, to)});
                if (found == edges.end() || found->second.size() != 2) continue;
                const auto& first = found->second[0];
                const auto& second = found->second[1];
                const auto& other = (first.triangle == ti) ? second : first;
                const int8_t required = (other.from == from && other.to == to) ? 1 : 0;
                if (flip[other.triangle] == -1) {
                    flip[other.triangle] = required;
                    q.push(other.triangle);
                } else if (flip[other.triangle] != required) {
                    conflict = true;
                }
            }
        }
    }

    for (size_t i = 0; i < mesh.triangles.size(); ++i)
        if (flip[i] == 1) std::swap(mesh.triangles[i][1], mesh.triangles[i][2]);

    for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
        throwIfCanceled(cancelRequested);
        const auto& component = components[componentIndex];
        long double volume6 = 0.0;
        for (uint32_t triangleIndex : component) {
            const auto& triangle = mesh.triangles[triangleIndex];
            if (triangle[0] >= mesh.vertices.size() || triangle[1] >= mesh.vertices.size() ||
                triangle[2] >= mesh.vertices.size()) continue;
            const Vec3& a = mesh.vertices[triangle[0]];
            const Vec3& b = mesh.vertices[triangle[1]];
            const Vec3& c = mesh.vertices[triangle[2]];
            volume6 += static_cast<long double>(dot(a, cross(b, c)));
        }
        if (volume6 < 0.0)
            for (uint32_t triangleIndex : component)
                std::swap(mesh.triangles[triangleIndex][1], mesh.triangles[triangleIndex][2]);
    }
}

} // namespace

void orientMeshConsistently(TriangleMesh& mesh, size_t& nonManifoldEdges, bool& conflict,
                            const std::atomic_bool* cancelRequested) {
    orientMeshConsistentlyImpl(mesh, nonManifoldEdges, conflict, cancelRequested);
}

StlLoadResult loadStl(const std::filesystem::path& path, const std::atomic_bool* cancelRequested) {
    StlLoadResult result;
    try {
        throwIfCanceled(cancelRequested);
        std::vector<RawTriangle> raw;
        std::string error;
        bool binary = false;
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) { result.message = "Datei konnte nicht geöffnet werden."; return result; }
            uint64_t size = 0;
            if (!validFileSize(in, size, error)) { result.message = error; return result; }
            if (size >= 84) {
                in.seekg(80);
                char bytes[4];
                in.read(bytes, 4);
                if (in) {
                    const uint32_t count = readU32LE(bytes);
                    binary = count <= kMaxStlTriangles && size >= 84ULL + 50ULL * count;
                }
            }
        }
        if (binary) {
            if (!readBinary(path, raw, error, cancelRequested)) { result.message = error; return result; }
        } else {
            if (!readAscii(path, raw, error, cancelRequested)) { result.message = error; return result; }
        }
        result.mesh = weld(raw, result.removedDegenerate, cancelRequested);
        if (result.mesh.triangles.empty()) {
            result.message = "STL enthält nach Bereinigung keine gültigen Dreiecke.";
            return result;
        }
        orientMeshConsistentlyImpl(result.mesh, result.nonManifoldEdges, result.orientationConflict, cancelRequested);
        result.ok = true;
        std::ostringstream message;
        message << result.mesh.triangles.size() << " Dreiecke, "
                << result.mesh.vertices.size() << " verschweißte Punkte";
        if (result.removedDegenerate)
            message << ", " << result.removedDegenerate << " degenerierte Dreiecke entfernt";
        if (result.nonManifoldEdges)
            message << ", " << result.nonManifoldEdges << " offene/nicht-manifolde Kanten";
        if (result.orientationConflict) message << ", Orientierungskonflikt erkannt";
        result.message = message.str();
    } catch (const OperationCanceled&) {
        result.message = "Ladevorgang abgebrochen.";
    } catch (const std::bad_alloc&) {
        result.message = "Nicht genügend Arbeitsspeicher zum Laden der STL-Datei.";
    } catch (const std::length_error& exception) {
        result.message = exception.what();
    } catch (const std::exception& exception) {
        result.message = std::string("Fehler beim Laden der STL-Datei: ") + exception.what();
    }
    return result;
}

bool saveBinaryStl(const std::filesystem::path& path, const TriangleMesh& mesh, std::string& error,
                   const std::atomic_bool* cancelRequested) {
    std::filesystem::path temporary;
    try {
        throwIfCanceled(cancelRequested);
        if (mesh.triangles.empty() || mesh.vertices.empty()) {
            error = "Das Mesh enthält keine exportierbaren Dreiecke.";
            return false;
        }
        if (mesh.triangles.size() > std::numeric_limits<uint32_t>::max()) {
            error = "Zu viele Dreiecke für binäres STL.";
            return false;
        }
        for (size_t vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            if ((vertexIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
            const Vec3& vertex = mesh.vertices[vertexIndex];
            if (!coordinateInRange(vertex)) { error = "Das Mesh enthält ungültige oder extrem große Koordinaten."; return false; }
            if (std::abs(vertex.x) > std::numeric_limits<float>::max() ||
                std::abs(vertex.y) > std::numeric_limits<float>::max() ||
                std::abs(vertex.z) > std::numeric_limits<float>::max()) {
                error = "Mindestens eine Koordinate liegt außerhalb des STL-Floatbereichs.";
                return false;
            }
        }
        for (size_t triangleIndex = 0; triangleIndex < mesh.triangles.size(); ++triangleIndex) {
            if ((triangleIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
            const auto& triangle = mesh.triangles[triangleIndex];
            if (triangle[0] >= mesh.vertices.size() || triangle[1] >= mesh.vertices.size() ||
                triangle[2] >= mesh.vertices.size() || triangle[0] == triangle[1] ||
                triangle[1] == triangle[2] || triangle[2] == triangle[0]) {
                error = "Das Mesh enthält einen ungültigen oder degenerierten Dreiecksindex.";
                return false;
            }
            const Vec3& a = mesh.vertices[triangle[0]];
            const Vec3& b = mesh.vertices[triangle[1]];
            const Vec3& c = mesh.vertices[triangle[2]];
            const double scale = std::max(mesh.bounds.diagonal(), 1.0);
            if (length(cross(b - a, c - a)) <= scale * scale * 1e-14) {
                error = "Das Mesh enthält ein geometrisch degeneriertes Dreieck.";
                return false;
            }
        }

        temporary = reserveSecureTemporarySibling(path);
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Zieldatei konnte nicht geöffnet werden."; return false; }
        char header[80]{};
        const std::string name = "PartSplice 3D";
        std::memcpy(header, name.data(), std::min(name.size(), sizeof(header)));
        out.write(header, sizeof(header));
        writeU32LE(out, static_cast<uint32_t>(mesh.triangles.size()));
        for (size_t triangleIndex = 0; triangleIndex < mesh.triangles.size(); ++triangleIndex) {
            if ((triangleIndex & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
            const auto& triangle = mesh.triangles[triangleIndex];
            const Vec3& a = mesh.vertices[triangle[0]];
            const Vec3& b = mesh.vertices[triangle[1]];
            const Vec3& c = mesh.vertices[triangle[2]];
            const Vec3 normal = normalized(cross(b - a, c - a));
            for (double value : {normal.x, normal.y, normal.z,
                                 a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z})
                writeFloatLE(out, static_cast<float>(value));
            writeU16LE(out, 0);
        }
        out.flush();
        if (!out) {
            error = "Fehler beim Schreiben der STL-Datei.";
            out.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
        out.close();
        return replaceAtomically(temporary, path, error);
    } catch (const OperationCanceled&) {
        error = "STL-Export abgebrochen.";
        if (!temporary.empty()) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
    } catch (const std::bad_alloc&) {
        if (!temporary.empty()) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
        error = "Nicht genügend Arbeitsspeicher zum Exportieren der STL-Datei.";
    } catch (const std::exception& exception) {
        if (!temporary.empty()) { std::error_code ignored; std::filesystem::remove(temporary, ignored); }
        error = std::string("STL-Export fehlgeschlagen: ") + exception.what();
    }
    return false;
}
