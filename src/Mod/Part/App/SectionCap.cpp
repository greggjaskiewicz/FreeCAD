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
#include <functional>
#include <map>
#include <unordered_map>

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

        const Base::Vector3d* p[3] = {&soup.points[ia], &soup.points[ib], &soup.points[ic]};
        const double s[3] = {*p[0] * normal - offset,
                             *p[1] * normal - offset,
                             *p[2] * normal - offset};

        // Half open test: exactly zero or two edges cross, so a vertex sitting
        // on the plane cannot yield a duplicate or a dangling segment.
        const bool above[3] = {s[0] > 0.0, s[1] > 0.0, s[2] > 0.0};
        if (above[0] == above[1] && above[1] == above[2]) {
            continue;
        }

        Base::Vector3d hit[2];
        int hits = 0;
        for (int e = 0; e < 3 && hits < 2; ++e) {
            const int a = e;
            const int b = (e + 1) % 3;
            if (above[a] == above[b]) {
                continue;
            }
            const double t = s[a] / (s[a] - s[b]);
            hit[hits++] = *p[a] + (*p[b] - *p[a]) * t;
        }
        // A triangle resting one vertex on the plane produces two crossings that
        // collapse onto that vertex. It does not cross the plane, and the zero
        // length segment would only confuse the chaining below.
        if (hits == 2 && Base::DistanceP2(hit[0], hit[1]) > 0.0) {
            segments.push_back(Segment {hit[0], hit[1]});
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


bool SectionCap::isClosed(const std::vector<Base::Vector3d>& loop, double tolerance)
{
    if (loop.size() < 3) {
        return false;
    }
    return Base::DistanceP2(loop.front(), loop.back()) <= tolerance * tolerance;
}


double SectionCap::signedArea(const std::vector<Base::Vector3d>& loop, const Base::Vector3d& normal)
{
    if (loop.size() < 3) {
        return 0.0;
    }

    // Newell's method: the projected area falls out of the cross product sum,
    // and taking it against the plane normal gives the sign.
    Base::Vector3d total(0, 0, 0);
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Base::Vector3d& a = loop[i];
        const Base::Vector3d& b = loop[(i + 1) % loop.size()];
        total += a.Cross(b);
    }
    return 0.5 * (total * normal);
}
