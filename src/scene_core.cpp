#include "scene_core.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <GL/gl.h>

#include <cmath>
#include <algorithm>
#include <iterator>
#include <map>
#include <utility>

namespace {
constexpr double kScenePi = 3.14159265358979323846;

bool validTriangleIndices(const TriangleMesh& mesh,
                          const std::array<uint32_t, 3>& triangle) {
    return triangle[0] < mesh.vertices.size() && triangle[1] < mesh.vertices.size() &&
           triangle[2] < mesh.vertices.size() && triangle[0] != triangle[1] &&
           triangle[1] != triangle[2] && triangle[0] != triangle[2];
}

uint32_t resolvedMaterial(const TriangleMesh& mesh, size_t triangleIndex) {
    if (mesh.triangleMaterials.empty()) return std::max(mesh.defaultMaterial, 1u);
    const uint32_t material = mesh.triangleMaterials[triangleIndex];
    return material == 0 ? std::max(mesh.defaultMaterial, 1u) : material;
}

bool parseHexColor(const std::string& value, float& red, float& green, float& blue) {
    if (value.size() < 7 || value[0] != '#') return false;
    auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    const int values[] = {nibble(value[1]), nibble(value[2]), nibble(value[3]),
                          nibble(value[4]), nibble(value[5]), nibble(value[6])};
    if (std::any_of(std::begin(values), std::end(values), [](int item) { return item < 0; }))
        return false;
    red = static_cast<float>(values[0] * 16 + values[1]) / 255.0f;
    green = static_cast<float>(values[2] * 16 + values[3]) / 255.0f;
    blue = static_cast<float>(values[4] * 16 + values[5]) / 255.0f;
    return true;
}
}

Mat4 perspective(double fovDegrees, double aspect, double nearPlane, double farPlane) {
    Mat4 result{};
    const double factor = 1.0 / std::tan(fovDegrees * kScenePi / 360.0);
    result.m[0] = factor / aspect;
    result.m[5] = factor;
    result.m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    result.m[11] = -1.0;
    result.m[14] = (2.0 * farPlane * nearPlane) / (nearPlane - farPlane);
    return result;
}

Mat4 ortho(double left, double right, double bottom, double top,
           double nearPlane, double farPlane) {
    Mat4 result{};
    result.m[0] = 2.0 / (right - left);
    result.m[5] = 2.0 / (top - bottom);
    result.m[10] = -2.0 / (farPlane - nearPlane);
    result.m[12] = -(right + left) / (right - left);
    result.m[13] = -(top + bottom) / (top - bottom);
    result.m[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    result.m[15] = 1.0;
    return result;
}

Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    const Vec3 forward = normalized(center - eye);
    const Vec3 side = normalized(cross(forward, up));
    const Vec3 correctedUp = cross(side, forward);
    Mat4 result{};
    result.m[0] = side.x; result.m[4] = side.y; result.m[8] = side.z;
    result.m[1] = correctedUp.x; result.m[5] = correctedUp.y; result.m[9] = correctedUp.z;
    result.m[2] = -forward.x; result.m[6] = -forward.y; result.m[10] = -forward.z;
    result.m[12] = -dot(side, eye);
    result.m[13] = -dot(correctedUp, eye);
    result.m[14] = dot(forward, eye);
    result.m[15] = 1.0;
    return result;
}

MeshDisplayList::MeshDisplayList(MeshDisplayList&& other) noexcept
    : lists_(std::move(other.lists_)) {
    other.lists_.clear();
}

MeshDisplayList& MeshDisplayList::operator=(MeshDisplayList&& other) noexcept {
    if (this != &other) {
        clear();
        lists_ = std::move(other.lists_);
        other.lists_.clear();
    }
    return *this;
}

MeshDisplayList::~MeshDisplayList() { clear(); }

void MeshDisplayList::clear() {
    for (const MaterialList& list : lists_)
        if (list.id != 0) glDeleteLists(list.id, 1);
    lists_.clear();
}

void MeshDisplayList::set(const TriangleMesh& mesh) {
    clear();
    if (mesh.triangles.empty()) return;
    std::map<uint32_t, std::vector<size_t>> groups;
    for (size_t index = 0; index < mesh.triangles.size(); ++index)
        groups[resolvedMaterial(mesh, index)].push_back(index);
    lists_.reserve(groups.size());
    for (const auto& [material, triangleIndices] : groups) {
        const unsigned int id = glGenLists(1);
        if (id == 0) continue;
        glNewList(id, GL_COMPILE);
        glBegin(GL_TRIANGLES);
        for (size_t triangleIndex : triangleIndices) {
            const auto& triangle = mesh.triangles[triangleIndex];
            if (!validTriangleIndices(mesh, triangle)) continue;
            const Vec3& a = mesh.vertices[triangle[0]];
            const Vec3& b = mesh.vertices[triangle[1]];
            const Vec3& c = mesh.vertices[triangle[2]];
            const Vec3 normal = normalized(cross(b - a, c - a));
            glNormal3d(normal.x, normal.y, normal.z);
            glVertex3d(a.x, a.y, a.z);
            glVertex3d(b.x, b.y, b.z);
            glVertex3d(c.x, c.y, c.z);
        }
        glEnd();
        glEndList();
        lists_.push_back({material, id});
    }
}

void MeshDisplayList::draw() const {
    for (const MaterialList& list : lists_)
        if (list.id != 0) glCallList(list.id);
}

void MeshDisplayList::drawMaterials(const std::vector<MeshMaterial>& materials,
                                    float alpha, float brightness) const {
    for (const MaterialList& list : lists_) {
        float red = 0.74f, green = 0.74f, blue = 0.74f;
        if (list.material > 0 && list.material <= materials.size())
            parseHexColor(materials[list.material - 1].color, red, green, blue);
        glColor4f(std::min(red * brightness, 1.0f),
                  std::min(green * brightness, 1.0f),
                  std::min(blue * brightness, 1.0f), alpha);
        if (list.id != 0) glCallList(list.id);
    }
}
