#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
inline Vec2 operator/(Vec2 a, double s) { return {a.x / s, a.y / s}; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator/(Vec3 a, double s) { return {a.x / s, a.y / s, a.z / s}; }

inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec2 normalized(Vec2 a) { const double l = length(a); return l > 1e-12 ? a / l : Vec2{}; }
inline Vec3 normalized(Vec3 a) { const double l = length(a); return l > 1e-12 ? a / l : Vec3{}; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
inline Vec2 leftNormal(Vec2 a) { return {-a.y, a.x}; }

struct AABB {
    Vec3 min{ std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity(),
              std::numeric_limits<double>::infinity() };
    Vec3 max{ -std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity() };

    void expand(Vec3 p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
    Vec3 center() const { return (min + max) * 0.5; }
    Vec3 size() const { return max - min; }
    double diagonal() const { return length(size()); }
    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
};

inline double signedArea(const std::vector<Vec2>& p) {
    double a = 0.0;
    for (size_t i = 0; i < p.size(); ++i) {
        const Vec2& u = p[i];
        const Vec2& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return a * 0.5;
}
