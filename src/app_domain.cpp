#include "app_domain.h"

#include <array>

uint64_t meshStorageBytes(const TriangleMesh& mesh) noexcept {
    return static_cast<uint64_t>(mesh.vertices.size()) * sizeof(Vec3) +
           static_cast<uint64_t>(mesh.triangles.size()) * sizeof(std::array<uint32_t, 3>) +
           static_cast<uint64_t>(mesh.triangleMaterials.size()) * sizeof(uint32_t);
}
