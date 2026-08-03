#pragma once

#include "stl_io.h"

#include <cstdint>
#include <vector>

struct Mat4 {
    double m[16]{};
};

Mat4 perspective(double fovDegrees, double aspect, double nearPlane, double farPlane);
Mat4 ortho(double left, double right, double bottom, double top,
           double nearPlane, double farPlane);
Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up);

class MeshDisplayList {
public:
    MeshDisplayList() = default;
    MeshDisplayList(const MeshDisplayList&) = delete;
    MeshDisplayList& operator=(const MeshDisplayList&) = delete;
    MeshDisplayList(MeshDisplayList&& other) noexcept;
    MeshDisplayList& operator=(MeshDisplayList&& other) noexcept;
    ~MeshDisplayList();

    void clear();
    void set(const TriangleMesh& mesh);
    void draw() const;
    void drawMaterials(const std::vector<MeshMaterial>& materials,
                       float alpha, float brightness = 1.0f) const;

private:
    struct MaterialList {
        uint32_t material = 1;
        unsigned int id = 0;
    };
    std::vector<MaterialList> lists_;
};

struct Camera {
    bool top = true;
    double yaw = 42.0;
    double pitch = 55.0;
    double orbitDistance = 100.0;
    double topHeight = 100.0;
    Vec2 pan{};
};
