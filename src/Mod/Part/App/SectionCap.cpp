// SPDX-License-Identifier: LGPL-2.1-or-later
// SPDX-FileCopyrightText: 2026 Gregg Jaskiewicz
// SPDX-FileNotice: Part of the FreeCAD project.

/******************************************************************************
 *                                                                            *
 *   FreeCAD is free software: you can redistribute it and/or modify          *
 *   it under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1            *
 *   of the License, or (at your option) any later version.                   *
 *                                                                            *
 *   FreeCAD is distributed in the hope that it will be useful,               *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty              *
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 *   See the GNU Lesser General Public License for more details.              *
 *                                                                            *
 *   You should have received a copy of the GNU Lesser General Public         *
 *   License along with FreeCAD. If not, see https://www.gnu.org/licenses     *
 *                                                                            *
 ******************************************************************************/

#include "PreCompiled.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>

#include <Base/Console.h>

#include "SectionCap.h"


using namespace Part;

namespace
{

/// Quantised point key, so endpoints that agree only to within the mesh
/// tolerance still land in the same bucket.
struct GridKey
{
    long long x = 0;
    long long y = 0;
    long long z = 0;

    bool operator==(const GridKey& other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct GridKeyHash
{
    std::size_t operator()(const GridKey& k) const noexcept
    {
        // three way mix, adequate for the handful of points a section produces
        std::size_t h = std::hash<long long> {}(k.x);
        h ^= std::hash<long long> {}(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<long long> {}(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

GridKey keyOf(const Base::Vector3d& p, double tolerance)
{
    const double inv = 1.0 / tolerance;
    return GridKey {static_cast<long long>(std::llround(p.x * inv)),
                    static_cast<long long>(std::llround(p.y * inv)),
                    static_cast<long long>(std::llround(p.z * inv))};
}

/// The eight cells around a point, so a lookup still finds a partner that
/// quantised to an adjacent bucket.
void forEachNeighbourKey(const GridKey& k, const std::function<void(const GridKey&)>& fn)
{
    for (long long dx = -1; dx <= 1; ++dx) {
        for (long long dy = -1; dy <= 1; ++dy) {
            for (long long dz = -1; dz <= 1; ++dz) {
                fn(GridKey {k.x + dx, k.y + dy, k.z + dz});
            }
        }
    }
}

}  // namespace


std::optional<Part::SectionCap::Segment> SectionCap::planeTriangleIntersection(
    const Base::Vector3d& a,
    const Base::Vector3d& b,
    const Base::Vector3d& c,
    const Base::Vector3d& normal,
    double offset
)
{
    const Base::Vector3d* p[3] = {&a, &b, &c};

    // signed distance from the plane
    const double s[3] = {a * normal - offset, b * normal - offset, c * normal - offset};

    // Half open test: exactly zero or two edges cross, so a vertex sitting
    // on the plane cannot yield a duplicate or a dangling segment.
    const bool above[3] = {s[0] > 0.0, s[1] > 0.0, s[2] > 0.0};
    if (above[0] == above[1] && above[1] == above[2]) {
        return std::nullopt;  // all on one side, or all on the plane
    }

    Base::Vector3d hit[2];
    int hits = 0;
    for (int e = 0; e < 3 && hits < 2; ++e) {
        const int i = e;
        const int j = (e + 1) % 3;
        if (above[i] == above[j]) {
            continue;
        }
        const double t = s[i] / (s[i] - s[j]);
        hit[hits++] = *p[i] + (*p[j] - *p[i]) * t;
    }
    // A triangle resting one vertex on the plane produces two crossings that
    // collapse onto that vertex. It does not cross the plane, and the zero
    // length segment would only confuse the chaining below.
    if (hits != 2 || Base::DistanceP2(hit[0], hit[1]) <= 0.0) {
        return std::nullopt;
    }

    return SectionCap::Segment {hit[0], hit[1]};
}


std::vector<SectionCap::Segment> SectionCap::sliceTriangles(
    const TriangleSoup& soup,
    const Base::Vector3d& normal,
    double offset
)
{
    std::vector<Segment> segments;
    if (soup.indices.size() < 3 || soup.points.empty()) {
        return segments;
    }

    const std::size_t pointCount = soup.points.size();
    segments.reserve(soup.indices.size() / 6);

    for (std::size_t i = 0; i + 2 < soup.indices.size(); i += 3) {
        const int ia = soup.indices[i];
        const int ib = soup.indices[i + 1];
        const int ic = soup.indices[i + 2];
        if (ia < 0 || ib < 0 || ic < 0 || static_cast<std::size_t>(ia) >= pointCount
            || static_cast<std::size_t>(ib) >= pointCount
            || static_cast<std::size_t>(ic) >= pointCount) {
            continue;
        }

        auto segment = planeTriangleIntersection(soup.points[ia],
                                                 soup.points[ib],
                                                 soup.points[ic],
                                                 normal,
                                                 offset);
        if (segment.has_value()) {
            segments.push_back(segment.value());
        }
    }

    return segments;
}


std::vector<std::vector<Base::Vector3d>>
SectionCap::chainLoops(const std::vector<Segment>& segments, double tolerance)
{
    std::vector<std::vector<Base::Vector3d>> loops;
    if (segments.empty() || tolerance <= 0.0) {
        return loops;
    }

    // endpoint bucket -> segments touching it, so growing a chain is a lookup
    // rather than a scan over everything still unused
    std::unordered_map<GridKey, std::vector<std::size_t>, GridKeyHash> buckets;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        buckets[keyOf(segments[i].start, tolerance)].push_back(i);
        buckets[keyOf(segments[i].end, tolerance)].push_back(i);
    }

    std::vector<bool> used(segments.size(), false);
    const double tolSq = tolerance * tolerance;

    auto findNext = [&](const Base::Vector3d& from, std::size_t skip) -> std::size_t {
        std::size_t found = segments.size();
        forEachNeighbourKey(keyOf(from, tolerance), [&](const GridKey& k) {
            if (found != segments.size()) {
                return;
            }
            auto it = buckets.find(k);
            if (it == buckets.end()) {
                return;
            }
            for (std::size_t idx : it->second) {
                if (idx == skip || used[idx]) {
                    continue;
                }
                if (Base::DistanceP2(segments[idx].start, from) <= tolSq
                    || Base::DistanceP2(segments[idx].end, from) <= tolSq) {
                    found = idx;
                    return;
                }
            }
        });
        return found;
    };

    for (std::size_t seed = 0; seed < segments.size(); ++seed) {
        if (used[seed]) {
            continue;
        }
        used[seed] = true;

        std::vector<Base::Vector3d> loop {segments[seed].start, segments[seed].end};
        Base::Vector3d tail = segments[seed].end;

        while (true) {
            const std::size_t next = findNext(tail, segments.size());
            if (next >= segments.size()) {
                break;
            }
            used[next] = true;
            // walk on from whichever end of the found segment is further away
            const bool startMatches = Base::DistanceP2(segments[next].start, tail) <= tolSq;
            tail = startMatches ? segments[next].end : segments[next].start;
            loop.push_back(tail);

            if (Base::DistanceP2(tail, loop.front()) <= tolSq) {
                break;  // closed
            }
        }

        if (loop.size() >= 3) {
            loops.push_back(std::move(loop));
        }
    }

    return loops;
}


SectionCap::TriangleSoup SectionCap::fillLoops(
    const std::vector<std::vector<Base::Vector3d>>& loops,
    const Base::Vector3d& u,
    const Base::Vector3d& v,
    int steps
)
{
    TriangleSoup soup;
    if (loops.empty() || steps < 1) {
        return soup;
    }

    // Same projection as the hatching, without the rotation: the fill has no
    // direction of its own.
    Base::Vector3d offsetFromOrigin(0, 0, 0);
    for (const auto& loop : loops) {
        if (!loop.empty()) {
            const Base::Vector3d& p = loop.front();
            offsetFromOrigin = p - u * (p * u) - v * (p * v);
            break;
        }
    }

    struct Edge
    {
        double a0, b0, a1, b1;
    };
    std::vector<Edge> edges;
    double bMin = std::numeric_limits<double>::max();
    double bMax = std::numeric_limits<double>::lowest();
    for (const auto& loop : loops) {
        if (loop.size() < 3) {
            continue;
        }
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Base::Vector3d& p = loop[i];
            const Base::Vector3d& q = loop[(i + 1) % loop.size()];
            const Edge e {p * u, p * v, q * u, q * v};
            if (!std::isfinite(e.a0) || !std::isfinite(e.b0) || !std::isfinite(e.a1)
                || !std::isfinite(e.b1)) {
                continue;
            }
            edges.push_back(e);
            bMin = std::min({bMin, e.b0, e.b1});
            bMax = std::max({bMax, e.b0, e.b1});
        }
    }
    if (edges.empty() || !(bMax > bMin)) {
        return soup;
    }

    const double height = (bMax - bMin) / static_cast<double>(steps);
    std::vector<double> crossings;

    for (int k = 0; k < steps; ++k) {
        const double lower = bMin + height * k;
        const double upper = lower + height;
        // Sampled mid strip, so a strip is filled according to what the region
        // does across it rather than exactly on its boundary.
        const double middle = lower + height * 0.5;

        crossings.clear();
        for (const auto& e : edges) {
            if ((e.b0 > middle) == (e.b1 > middle)) {
                continue;
            }
            const double t = (middle - e.b0) / (e.b1 - e.b0);
            crossings.push_back(e.a0 + (e.a1 - e.a0) * t);
        }
        if (crossings.size() < 2) {
            continue;
        }
        std::sort(crossings.begin(), crossings.end());

        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            const double left = crossings[i];
            const double right = crossings[i + 1];
            if (!(right > left)) {
                continue;
            }
            const int base = static_cast<int>(soup.points.size());
            soup.points.push_back(offsetFromOrigin + u * left + v * lower);
            soup.points.push_back(offsetFromOrigin + u * right + v * lower);
            soup.points.push_back(offsetFromOrigin + u * right + v * upper);
            soup.points.push_back(offsetFromOrigin + u * left + v * upper);
            soup.indices.insert(
                soup.indices.end(),
                {base, base + 1, base + 2, base, base + 2, base + 3}
            );
        }
    }

    return soup;
}


bool SectionCap::extentAlong(
    const Base::BoundBox3d& bounds,
    const Base::Vector3d& normal,
    double& lo,
    double& hi
)
{
    if (!bounds.IsValid()) {
        return false;
    }

    // The corner furthest along the normal is the one picked axis by axis, so
    // the whole extent falls out of the centre plus a support radius. No need to
    // enumerate the eight corners, let alone the points inside them.
    const Base::Vector3d centre = bounds.GetCenter();
    const double reach = 0.5
        * (bounds.LengthX() * std::abs(normal.x) + bounds.LengthY() * std::abs(normal.y)
           + bounds.LengthZ() * std::abs(normal.z));

    const double middle = centre * normal;
    lo = middle - reach;
    hi = middle + reach;
    return true;
}


bool SectionCap::isClosed(const std::vector<Base::Vector3d>& loop, double tolerance)
{
    if (loop.size() < 3) {
        return false;
    }
    return Base::DistanceP2(loop.front(), loop.back()) <= tolerance * tolerance;
}


std::vector<SectionCap::Segment> SectionCap::hatchLoops(
    const std::vector<std::vector<Base::Vector3d>>& loops,
    const Base::Vector3d& u,
    const Base::Vector3d& v,
    double spacing,
    double angleRad
)
{
    std::vector<Segment> hatch;
    if (loops.empty() || !std::isfinite(spacing) || spacing <= 0.0 || !std::isfinite(angleRad)) {
        return hatch;
    }

    // Rotate the in plane frame so the hatch lines come out horizontal; a
    // scanline is then simply a constant `b`, and the fill is one dimensional.
    const double c = std::cos(angleRad);
    const double s = std::sin(angleRad);
    const Base::Vector3d along = u * c + v * s;
    const Base::Vector3d across = u * -s + v * c;

    // `along` and `across` span the plane but say nothing about how far along
    // its normal it sits, so that component is taken off a real point and added
    // back when the hatch is lifted into 3D.
    Base::Vector3d offsetFromOrigin(0, 0, 0);
    for (const auto& loop : loops) {
        if (!loop.empty()) {
            const Base::Vector3d& p = loop.front();
            offsetFromOrigin = p - along * (p * along) - across * (p * across);
            break;
        }
    }

    // Project once; the scanline sweep below revisits every edge per level.
    struct Edge
    {
        double a0, b0, a1, b1;
    };
    std::vector<Edge> edges;
    double bMin = std::numeric_limits<double>::max();
    double bMax = std::numeric_limits<double>::lowest();
    for (const auto& loop : loops) {
        if (loop.size() < 3) {
            continue;
        }
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Base::Vector3d& p = loop[i];
            const Base::Vector3d& q = loop[(i + 1) % loop.size()];
            const Edge e {p * along, p * across, q * along, q * across};
            if (!std::isfinite(e.a0) || !std::isfinite(e.b0) || !std::isfinite(e.a1)
                || !std::isfinite(e.b1)) {
                continue;
            }
            edges.push_back(e);
            bMin = std::min({bMin, e.b0, e.b1});
            bMax = std::max({bMax, e.b0, e.b1});
        }
    }
    if (edges.empty() || bMin > bMax) {
        return hatch;
    }

    // A spacing far finer than the section is a mistake, not a request; bound
    // the sweep while still in floating point, where overflow only saturates.
    constexpr double maxLines = 200000.0;
    const double kMinD = std::ceil(bMin / spacing);
    const double kMaxD = std::floor(bMax / spacing);
    if (!(kMaxD - kMinD <= maxLines)) {
        // Refused rather than attempted, and said out loud: returning an empty
        // hatch silently looks identical to a section that genuinely has none.
        Base::Console().warning(
            "SectionAnalysis: hatch spacing of %g would need over %g lines, skipping.\n",
            spacing,
            maxLines
        );
        return hatch;
    }

    std::vector<double> crossings;
    for (auto k = static_cast<std::int64_t>(kMinD); k <= static_cast<std::int64_t>(kMaxD); ++k) {
        const double level = static_cast<double>(k) * spacing;

        crossings.clear();
        for (const auto& e : edges) {
            // Half open test again, so a vertex exactly on the scanline is
            // counted once. Counting it twice would flip parity back and leave
            // the span beyond it unfilled.
            if ((e.b0 > level) == (e.b1 > level)) {
                continue;
            }
            const double t = (level - e.b0) / (e.b1 - e.b0);
            crossings.push_back(e.a0 + (e.a1 - e.a0) * t);
        }
        if (crossings.size() < 2) {
            continue;
        }

        std::sort(crossings.begin(), crossings.end());
        // Even odd rule: the material lies between the 1st and 2nd crossing,
        // the 3rd and 4th, and so on, which is what steps over the holes.
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            if (crossings[i + 1] - crossings[i] <= 0.0) {
                continue;
            }
            hatch.push_back(
                Segment {offsetFromOrigin + along * crossings[i] + across * level,
                         offsetFromOrigin + along * crossings[i + 1] + across * level}
            );
        }
    }

    return hatch;
}


