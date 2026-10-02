#include "minecraft/world/phys/AABB.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>

#include "HitResult.h"
#include "minecraft/world/phys/Vec3.h"

bool AABB::contains(const Vec3& p) const {
    if (p.x <= x0 || p.x >= x1) return false;
    if (p.y <= y0 || p.y >= y1) return false;
    if (p.z <= z0 || p.z >= z1) return false;
    return true;
}



bool AABB::containsIncludingLowerBound(const Vec3& p) const {
    if (p.x < x0 || p.x >= x1) return false;
    if (p.y < y0 || p.y >= y1) return false;
    if (p.z < z0 || p.z >= z1) return false;
    return true;
}

HitResult* AABB::clip(const Vec3& a, const Vec3& b) const {
    auto xh0 = a.clipX(b, x0);
    auto xh1 = a.clipX(b, x1);
    auto yh0 = a.clipY(b, y0);
    auto yh1 = a.clipY(b, y1);
    auto zh0 = a.clipZ(b, z0);
    auto zh1 = a.clipZ(b, z1);

    if (!containsX(xh0)) xh0 = std::nullopt;
    if (!containsX(xh1)) xh1 = std::nullopt;
    if (!containsY(yh0)) yh0 = std::nullopt;
    if (!containsY(yh1)) yh1 = std::nullopt;
    if (!containsZ(zh0)) zh0 = std::nullopt;
    if (!containsZ(zh1)) zh1 = std::nullopt;

    std::optional<Vec3> closest = std::nullopt;

    if (xh0.has_value() && (!closest.has_value() || a.distanceToSqr(*xh0) < a.distanceToSqr(*closest)))
        closest = xh0;
    if (xh1.has_value() && (!closest.has_value() || a.distanceToSqr(*xh1) < a.distanceToSqr(*closest)))
        closest = xh1;
    if (yh0.has_value() && (!closest.has_value() || a.distanceToSqr(*yh0) < a.distanceToSqr(*closest)))
        closest = yh0;
    if (yh1.has_value() && (!closest.has_value() || a.distanceToSqr(*yh1) < a.distanceToSqr(*closest)))
        closest = yh1;
    if (zh0.has_value() && (!closest.has_value() || a.distanceToSqr(*zh0) < a.distanceToSqr(*closest)))
        closest = zh0;
    if (zh1.has_value() && (!closest.has_value() || a.distanceToSqr(*zh1) < a.distanceToSqr(*closest)))
        closest = zh1;

    if (!closest.has_value()) return nullptr;

    int face = -1;
    if (closest == xh0) face = 4;
    else if (closest == xh1) face = 5;
    else if (closest == yh0) face = 0;
    else if (closest == yh1) face = 1;
    else if (closest == zh0) face = 2;
    else if (closest == zh1) face = 3;

    return new HitResult(0, 0, 0, face, *closest);
}

bool AABB::containsX(const std::optional<Vec3>& v) const {
    if (!v.has_value()) return false;
    return v->y >= y0 && v->y <= y1 && v->z >= z0 && v->z <= z1;
}

bool AABB::containsY(const std::optional<Vec3>& v) const {
    if (!v.has_value()) return false;
    return v->x >= x0 && v->x <= x1 && v->z >= z0 && v->z <= z1;
}

bool AABB::containsZ(const std::optional<Vec3>& v) const {
    if (!v.has_value()) return false;
    return v->x >= x0 && v->x <= x1 && v->y >= y0 && v->y <= y1;
}

std::string AABB::toString() const {
    return std::format("box[{}, {}, {}, {}, {}, {}]", x0, y0, z0, x1, y1, z1);
}