#include "step_io.h"
#include "safe_temp.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <ShapeFix_Shape.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

constexpr uint64_t kMaxStepFileBytes = 512ull * 1024ull * 1024ull;
constexpr size_t kMaxStepVertices = 5'000'000;
constexpr size_t kMaxStepTriangles = 10'000'000;
constexpr size_t kMaxStepBodies = 2'048;
constexpr double kMaxCoordinateMagnitude = 1'000'000'000.0;

class OperationCanceled final : public std::runtime_error {
public:
    OperationCanceled() : std::runtime_error("Vorgang abgebrochen.") {}
};

void throwIfCanceled(const std::atomic_bool* cancelRequested) {
    if (cancelRequested && cancelRequested->load(std::memory_order_relaxed))
        throw OperationCanceled();
}

bool coordinateInRange(Vec3 point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
           std::abs(point.x) <= kMaxCoordinateMagnitude &&
           std::abs(point.y) <= kMaxCoordinateMagnitude &&
           std::abs(point.z) <= kMaxCoordinateMagnitude;
}

struct QuantKey {
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;
    bool operator==(const QuantKey&) const = default;
};

struct QuantHash {
    size_t operator()(const QuantKey& key) const noexcept {
        auto mix = [](uint64_t value) {
            value ^= value >> 30;
            value *= 0xbf58476d1ce4e5b9ULL;
            value ^= value >> 27;
            value *= 0x94d049bb133111ebULL;
            value ^= value >> 31;
            return value;
        };
        return static_cast<size_t>(mix(static_cast<uint64_t>(key.x)) ^
                                   (mix(static_cast<uint64_t>(key.y)) << 1) ^
                                   (mix(static_cast<uint64_t>(key.z)) << 2));
    }
};

struct TemporaryStepCopy {
    std::filesystem::path path;
    ~TemporaryStepCopy() {
        if (path.empty()) return;
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};

#ifdef _WIN32
bool pathNeedsCompatibilityCopy(const std::filesystem::path& path) {
    for (wchar_t character : path.native()) {
        if (character < 32 || character > 126) return true;
    }
    return false;
}

std::string windowsAnsiPath(const std::filesystem::path& path) {
    std::wstring wide = path.native();
    std::vector<wchar_t> shortBuffer(32768, L'\0');
    const DWORD shortLength = GetShortPathNameW(wide.c_str(), shortBuffer.data(),
                                                static_cast<DWORD>(shortBuffer.size()));
    if (shortLength > 0 && shortLength < shortBuffer.size())
        wide.assign(shortBuffer.data(), shortLength);

    BOOL usedDefault = FALSE;
    const int bytes = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(),
                                          static_cast<int>(wide.size()), nullptr, 0,
                                          nullptr, &usedDefault);
    if (bytes <= 0 || usedDefault)
        throw std::runtime_error("Windows konnte keinen Open-CASCADE-kompatiblen Dateipfad erzeugen.");
    std::string encoded(static_cast<size_t>(bytes), '\0');
    usedDefault = FALSE;
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(),
                            static_cast<int>(wide.size()), encoded.data(), bytes,
                            nullptr, &usedDefault) != bytes || usedDefault)
        throw std::runtime_error("Windows konnte den STEP-Dateipfad nicht verlustfrei kodieren.");
    return encoded;
}
#endif

std::string prepareStepPath(const std::filesystem::path& source, TemporaryStepCopy& temporary) {
#ifdef _WIN32
    std::filesystem::path readable = source;
    if (pathNeedsCompatibilityCopy(source)) {
        std::error_code error;
        const std::filesystem::path tempDirectory = std::filesystem::temp_directory_path(error);
        if (error)
            throw std::runtime_error("Temporärer Ordner für den STEP-Import ist nicht verfügbar: " +
                                     error.message());
        const std::filesystem::path candidate = reserveSecureTemporaryFile(
            tempDirectory, L"PartSplice_STEP_", L".step");
        error.clear();
        if (!std::filesystem::copy_file(source, candidate,
                                        std::filesystem::copy_options::overwrite_existing, error)) {
            const std::string copyError = error.message();
            std::error_code ignored;
            std::filesystem::remove(candidate, ignored);
            throw std::runtime_error("STEP-Datei konnte nicht in einen kompatiblen temporären Pfad kopiert werden: " +
                                     copyError);
        }
        temporary.path = candidate;
        readable = candidate;
    }
    return windowsAnsiPath(readable);
#else
    (void)temporary;
    const std::u8string encoded = source.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
#endif
}

double shapeDiagonal(const TopoDS_Shape& shape) {
    Bnd_Box box;
    BRepBndLib::Add(shape, box, false);
    if (box.IsVoid()) return 100.0;
    Standard_Real xmin = 0.0, ymin = 0.0, zmin = 0.0;
    Standard_Real xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const Vec3 minimum{xmin, ymin, zmin};
    const Vec3 maximum{xmax, ymax, zmax};
    if (!coordinateInRange(minimum) || !coordinateInRange(maximum))
        throw std::runtime_error("STEP enthält ungültige oder extrem große Modellgrenzen.");
    const long double dx = static_cast<long double>(xmax) - xmin;
    const long double dy = static_cast<long double>(ymax) - ymin;
    const long double dz = static_cast<long double>(zmax) - zmin;
    const long double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(static_cast<double>(diagonal)) || diagonal > kMaxCoordinateMagnitude * 4.0L)
        throw std::runtime_error("STEP-Modellausdehnung ist unzulässig groß.");
    return static_cast<double>(diagonal);
}

TriangleMesh triangulateShape(const TopoDS_Shape& shape, size_t& removedDegenerate,
                              const std::atomic_bool* cancelRequested) {
    throwIfCanceled(cancelRequested);
    TriangleMesh mesh;
    const double diagonal = std::max(shapeDiagonal(shape), 1.0);
    const double linearDeflection = std::clamp(diagonal * 0.001, 0.02, 0.25);
    BRepMesh_IncrementalMesh mesher(shape, linearDeflection, false, 0.35, true);
    mesher.Perform();
    throwIfCanceled(cancelRequested);
    if (!mesher.IsDone()) return mesh;

    const double weldTolerance = std::max(diagonal * 1e-8, 1e-7);
    const double weldTolerance2 = weldTolerance * weldTolerance;
    std::unordered_map<QuantKey, std::vector<uint32_t>, QuantHash> welded;
    auto vertexIndex = [&](const gp_Pnt& point) {
        const Vec3 p{point.X(), point.Y(), point.Z()};
        if (!coordinateInRange(p))
            throw std::runtime_error("STEP enthält ungültige oder extrem große Koordinaten.");
        const QuantKey key{static_cast<int64_t>(std::floor(p.x / weldTolerance)),
                           static_cast<int64_t>(std::floor(p.y / weldTolerance)),
                           static_cast<int64_t>(std::floor(p.z / weldTolerance))};
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    const auto found = welded.find({key.x + dx, key.y + dy, key.z + dz});
                    if (found == welded.end()) continue;
                    for (uint32_t candidate : found->second) {
                        const Vec3 delta = mesh.vertices[candidate] - p;
                        if (dot(delta, delta) <= weldTolerance2) return candidate;
                    }
                }
            }
        }
        if (mesh.vertices.size() >= std::numeric_limits<uint32_t>::max() ||
            mesh.vertices.size() >= kMaxStepVertices)
            throw std::length_error("STEP enthält zu viele Punkte.");
        const uint32_t index = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(p);
        mesh.bounds.expand(p);
        welded[key].push_back(index);
        return index;
    };

    size_t faceOrdinal = 0;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        if ((++faceOrdinal & 0xFFu) == 0) throwIfCanceled(cancelRequested);
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) continue;
        const gp_Trsf transform = location.Transformation();
        for (Standard_Integer index = 1; index <= triangulation->NbTriangles(); ++index) {
            if ((static_cast<uint64_t>(index) & 0x3FFFu) == 0) throwIfCanceled(cancelRequested);
            if (mesh.triangles.size() >= kMaxStepTriangles)
                throw std::length_error("STEP enthält zu viele Dreiecke.");
            Standard_Integer a = 0, b = 0, c = 0;
            triangulation->Triangle(index).Get(a, b, c);
            if (face.Orientation() == TopAbs_REVERSED) std::swap(b, c);
            const uint32_t ia = vertexIndex(triangulation->Node(a).Transformed(transform));
            const uint32_t ib = vertexIndex(triangulation->Node(b).Transformed(transform));
            const uint32_t ic = vertexIndex(triangulation->Node(c).Transformed(transform));
            if (ia == ib || ib == ic || ic == ia) {
                ++removedDegenerate;
                continue;
            }
            const Vec3& va = mesh.vertices[ia];
            const Vec3& vb = mesh.vertices[ib];
            const Vec3& vc = mesh.vertices[ic];
            if (length(cross(vb - va, vc - va)) <= weldTolerance * weldTolerance) {
                ++removedDegenerate;
                continue;
            }
            mesh.triangles.push_back({ia, ib, ic});
        }
    }
    size_t nonManifoldEdges = 0;
    bool orientationConflict = false;
    orientMeshConsistently(mesh, nonManifoldEdges, orientationConflict, cancelRequested);
    return mesh;
}

} // namespace

StepLoadResult loadStep(const std::filesystem::path& path,
                        const std::atomic_bool* cancelRequested) {
    StepLoadResult result;
    try {
        throwIfCanceled(cancelRequested);
        std::error_code fileError;
        const uintmax_t fileBytes = std::filesystem::file_size(path, fileError);
        if (fileError) { result.message = "STEP-Dateigröße konnte nicht gelesen werden: " + fileError.message(); return result; }
        if (fileBytes == 0 || fileBytes > kMaxStepFileBytes) {
            result.message = fileBytes == 0 ? "STEP-Datei ist leer." :
                "STEP-Datei überschreitet die Sicherheitsgrenze von 512 MiB.";
            return result;
        }
        Interface_Static::SetCVal("xstep.cascade.unit", "MM");
        STEPControl_Reader reader;
        TemporaryStepCopy temporaryCopy;
        const std::string nativePath = prepareStepPath(path, temporaryCopy);
        if (reader.ReadFile(nativePath.c_str()) != IFSelect_RetDone) {
            result.message = "STEP-Datei konnte nicht gelesen werden.";
            return result;
        }
        throwIfCanceled(cancelRequested);
        if (reader.TransferRoots() <= 0) {
            result.message = "STEP-Datei enthält keinen übertragbaren Körper.";
            return result;
        }
        throwIfCanceled(cancelRequested);
        const TopoDS_Shape root = reader.OneShape();
        if (root.IsNull()) {
            result.message = "STEP-Datei enthält keine Geometrie.";
            return result;
        }

        std::vector<TopoDS_Shape> bodies;
        for (TopExp_Explorer explorer(root, TopAbs_SOLID); explorer.More(); explorer.Next()) {
            throwIfCanceled(cancelRequested);
            bodies.push_back(explorer.Current());
            if (bodies.size() > kMaxStepBodies) throw std::length_error("STEP enthält zu viele Körper.");
        }
        if (bodies.empty()) bodies.push_back(root);

        size_t skippedBodies = 0;
        size_t totalVertices = 0;
        size_t totalTriangles = 0;
        for (size_t index = 0; index < bodies.size(); ++index) {
            throwIfCanceled(cancelRequested);
            TopoDS_Shape body = bodies[index];
            if (!BRepCheck_Analyzer(body).IsValid()) {
                ShapeFix_Shape fixer(body);
                fixer.Perform();
                throwIfCanceled(cancelRequested);
                TopoDS_Shape repaired = fixer.Shape();
                if (!repaired.IsNull() && BRepCheck_Analyzer(repaired).IsValid()) {
                    body = repaired;
                    ++result.repairedBodies;
                } else {
                    ++result.failedRepairBodies;
                    ++skippedBodies;
                    continue;
                }
            }
            TriangleMesh mesh = triangulateShape(body, result.removedDegenerate, cancelRequested);
            if (mesh.vertices.empty() || mesh.triangles.empty()) {
                ++skippedBodies;
                continue;
            }
            if (mesh.vertices.size() > kMaxStepVertices - totalVertices ||
                mesh.triangles.size() > kMaxStepTriangles - totalTriangles) {
                throw std::length_error("STEP-Gesamtgeometrie überschreitet das sichere Importbudget.");
            }
            totalVertices += mesh.vertices.size();
            totalTriangles += mesh.triangles.size();
            const std::u8string stemUtf8 = path.stem().u8string();
            const std::string name = bodies.size() == 1
                ? std::string(reinterpret_cast<const char*>(stemUtf8.data()), stemUtf8.size())
                : "STEP-Körper " + std::to_string(index + 1);
            result.objects.push_back({name, std::move(mesh), 1});
        }
        if (result.objects.empty()) {
            result.message = "STEP-Geometrie konnte nicht in ein bearbeitbares Dreiecksmodell umgewandelt werden.";
            return result;
        }

        result.ok = true;
        std::ostringstream message;
        message << result.objects.size() << " STEP-Körper, " << totalTriangles << " Dreiecke";
        if (result.repairedBodies > 0)
            message << ", " << result.repairedBodies << " CAD-Körper beim Import korrigiert";
        if (result.failedRepairBodies > 0)
            message << ", " << result.failedRepairBodies << " nicht reparierbare CAD-Körper übersprungen";
        if (result.removedDegenerate > 0)
            message << ", " << result.removedDegenerate << " degenerierte Dreiecke entfernt";
        if (skippedBodies > 0)
            message << ", " << skippedBodies << " leere Körper übersprungen";
        result.message = message.str();
        return result;
    } catch (const OperationCanceled&) {
        result.message = "Ladevorgang abgebrochen.";
        return result;
    } catch (const Standard_Failure& failure) {
        const char* message = failure.GetMessageString();
        result.message = std::string("Open-CASCADE-Fehler beim STEP-Import: ") +
                         (message ? message : "unbekannter Open-CASCADE-Fehler");
        return result;
    } catch (const std::bad_alloc&) {
        result.message = "Nicht genügend Arbeitsspeicher zum Laden der STEP-Datei.";
        return result;
    } catch (const std::length_error& exception) {
        result.message = std::string("STEP-Datei überschreitet eine Sicherheitsgrenze: ") + exception.what();
        return result;
    } catch (const std::exception& exception) {
        result.message = std::string("Fehler beim STEP-Import: ") + exception.what();
        return result;
    }
}
