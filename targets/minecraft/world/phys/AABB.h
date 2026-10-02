#pragma once

#include <algorithm>
#include <string>
#include <optional>

class Vec3;
class HitResult;

class AABB {
public:
    double x0, y0, z0;
    double x1, y1, z1;

    inline AABB() : x0(0), y0(0), z0(0), x1(0), y1(0), z1(0) {}
    inline AABB(double x0, double y0, double z0, double x1, double y1, double z1)
        : x0(x0), y0(y0), z0(z0), x1(x1), y1(y1), z1(z1) {}

    // INLINE ULTRA-RÁPIDO: Intersección sin llamadas a función
    inline bool intersects(const AABB& c) const noexcept {
        return (c.x1 > x0 && c.x0 < x1 &&
                c.y1 > y0 && c.y0 < y1 &&
                c.z1 > z0 && c.z0 < z1);
    }

    inline bool intersects(double x02, double y02, double z02,
                           double x12, double y12, double z12) const noexcept {
        return (x12 > x0 && x02 < x1 &&
                y12 > y0 && y02 < y1 &&
                z12 > z0 && z02 < z1);
    }

    // RESOLUCIÓN DE COLISIONES DIRECTA EN REGISTROS DE CPU
    inline double clipXCollide(const AABB& c, double xa) const noexcept {
        if (c.y1 <= y0 || c.y0 >= y1) return xa;
        if (c.z1 <= z0 || c.z0 >= z1) return xa;

        if (xa > 0.0 && c.x1 <= x0) {
            double max = x0 - c.x1;
            if (max < xa) xa = max;
        }
        if (xa < 0.0 && c.x0 >= x1) {
            double max = x1 - c.x0;
            if (max > xa) xa = max;
        }
        return xa;
    }

    inline double clipYCollide(const AABB& c, double ya) const noexcept {
        if (c.x1 <= x0 || c.x0 >= x1) return ya;
        if (c.z1 <= z0 || c.z0 >= z1) return ya;

        if (ya > 0.0 && c.y1 <= y0) {
            double max = y0 - c.y1;
            if (max < ya) ya = max;
        }
        if (ya < 0.0 && c.y0 >= y1) {
            double max = y1 - c.y0;
            if (max > ya) ya = max;
        }
        return ya;
    }

    inline double clipZCollide(const AABB& c, double za) const noexcept {
        if (c.x1 <= x0 || c.x0 >= x1) return za;
        if (c.y1 <= y0 || c.y0 >= y1) return za;

        if (za > 0.0 && c.z1 <= z0) {
            double max = z0 - c.z1;
            if (max < za) za = max;
        }
        if (za < 0.0 && c.z0 >= z1) {
            double max = z1 - c.z0;
            if (max > za) za = max;
        }
        return za;
    }

    inline AABB move(double xa, double ya, double za) const noexcept {
        return {x0 + xa, y0 + ya, z0 + za, x1 + xa, y1 + ya, z1 + za};
    }

    inline AABB expand(double xa, double ya, double za) const noexcept {
        double _x0 = (xa < 0.0) ? x0 + xa : x0;
        double _x1 = (xa > 0.0) ? x1 + xa : x1;
        double _y0 = (ya < 0.0) ? y0 + ya : y0;
        double _y1 = (ya > 0.0) ? y1 + ya : y1;
        double _z0 = (za < 0.0) ? z0 + za : z0;
        double _z1 = (za > 0.0) ? z1 + za : z1;
        return {_x0, _y0, _z0, _x1, _y1, _z1};
    }

    inline AABB grow(double xa, double ya, double za) const noexcept {
        return {x0 - xa, y0 - ya, z0 - za, x1 + xa, y1 + ya, z1 + za};
    }

    inline AABB shrink(double xa, double ya, double za) const noexcept {
        return {x0 + xa, y0 + ya, z0 + za, x1 - xa, y1 - ya, z1 - za};
    }

    inline AABB minmax(const AABB& other) const noexcept {
        return {
            std::min(x0, other.x0), std::min(y0, other.y0), std::min(z0, other.z0),
            std::max(x1, other.x1), std::max(y1, other.y1), std::max(z1, other.z1)
        };
    }

    
    bool contains(const Vec3& p) const;
    bool containsIncludingLowerBound(const Vec3& p) const;
    inline double getSize() const noexcept {
        return ((x1 - x0) + (y1 - y0) + (z1 - z0)) / 3.0;
    }

    HitResult* clip(const Vec3& a, const Vec3& b) const;
    std::string toString() const;

private:
    bool containsX(const std::optional<Vec3>& v) const;
    bool containsY(const std::optional<Vec3>& v) const;
    bool containsZ(const std::optional<Vec3>& v) const;
};