// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <cmath>

#include <Mod/Part/App/SectionCap.h>

using namespace Part::SectionCap;

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
namespace
{

/// A closed box as a triangle soup, spanning 0..size in every axis.
TriangleSoup box(double size)
{
    TriangleSoup soup;
    const double s = size;
    using V = Base::Vector3d;
    soup.points = {
        V(0, 0, 0), V(s, 0, 0), V(s, s, 0), V(0, s, 0),  // bottom
        V(0, 0, s), V(s, 0, s), V(s, s, s), V(0, s, s),  // top
    };
    // 12 triangles, wound outwards
    soup.indices = {
        0, 2, 1, 0, 3, 2,  // bottom
        4, 5, 6, 4, 6, 7,  // top
        0, 1, 5, 0, 5, 4,  // front
        1, 2, 6, 1, 6, 5,  // right
        2, 3, 7, 2, 7, 6,  // back
        3, 0, 4, 3, 4, 7,  // left
    };
    return soup;
}

const Base::Vector3d Z(0, 0, 1);
const Base::Vector3d U(1, 0, 0);
const Base::Vector3d V(0, 1, 0);

/// Strip height giving `count` strips across a region `extent` tall.
///
/// fillLoops takes a height rather than a count, so that one part of an
/// assembly does not get the same effort as the whole of it. These tests still
/// read most naturally as "this many strips across a 10 mm square", so they
/// say that and convert.
constexpr double stripsAcross(double extent, int count)
{
    return extent / count;
}
/// The box the view provider measures once at harvest time and then rejects
/// planes against, without touching the triangles again.
Base::BoundBox3d boundsOf(const TriangleSoup& soup)
{
    return Base::BoundBox3d(soup.points.data(), soup.points.size());
}

/// Total area of a triangle soup, by the cross product of each triangle.
double soupArea(const TriangleSoup& soup)
{
    double total = 0.0;
    for (std::size_t i = 0; i + 2 < soup.indices.size(); i += 3) {
        const Base::Vector3d& a = soup.points[soup.indices[i]];
        const Base::Vector3d& b = soup.points[soup.indices[i + 1]];
        const Base::Vector3d& c = soup.points[soup.indices[i + 2]];
        total += 0.5 * ((b - a).Cross(c - a)).Length();
    }
    return total;
}

}  // namespace


TEST(SectionCapSlice, testPlaneThroughABoxCrossesEightTriangles)
{
    // Act - halfway up, so it cuts the four side walls
    const auto segments = sliceTriangles(box(10), Z, 5.0);

    // Assert - each of the four walls is two triangles, and both are crossed
    EXPECT_EQ(segments.size(), 8);
}

TEST(SectionCapSlice, testPlaneAboveTheBoxCrossesNothing)
{
    EXPECT_TRUE(sliceTriangles(box(10), Z, 50.0).empty());
}

TEST(SectionCapSlice, testPlaneBelowTheBoxCrossesNothing)
{
    EXPECT_TRUE(sliceTriangles(box(10), Z, -50.0).empty());
}

TEST(SectionCapSlice, testEverySegmentLiesOnThePlane)
{
    const auto segments = sliceTriangles(box(10), Z, 3.5);

    ASSERT_FALSE(segments.empty());
    for (const auto& s : segments) {
        EXPECT_NEAR(s.start.z, 3.5, 1e-9);
        EXPECT_NEAR(s.end.z, 3.5, 1e-9);
    }
}

TEST(SectionCapSlice, testAVertexExactlyOnThePlaneDoesNotDuplicateSegments)
{
    // a single triangle with one vertex sitting exactly on z = 0
    TriangleSoup soup;
    soup.points = {Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, -5), Base::Vector3d(10, 0, 5)};
    soup.indices = {0, 1, 2};

    const auto segments = sliceTriangles(soup, Z, 0.0);

    // the half open test must yield one crossing, not two or none
    EXPECT_EQ(segments.size(), 1);
}

TEST(SectionCapSlice, testATriangleTouchingThePlaneAtOneVertexYieldsNothing)
{
    // A triangle resting a single vertex on the plane does not cross it. Both
    // "crossings" collapse onto that vertex, so a naive sign test emits a
    // zero length segment that then pollutes the chaining.
    TriangleSoup soup;
    soup.points = {Base::Vector3d(0, 0, 0), Base::Vector3d(10, 0, 5), Base::Vector3d(0, 10, 5)};
    soup.indices = {0, 1, 2};

    const auto segments = sliceTriangles(soup, Z, 0.0);

    for (const auto& s : segments) {
        EXPECT_GT(Base::Distance(s.start, s.end), 1e-9) << "zero length segment emitted";
    }
}

TEST(SectionCapSlice, testDegenerateIndicesAreIgnored)
{
    TriangleSoup soup = box(10);
    soup.indices.push_back(99);  // out of range
    soup.indices.push_back(-1);
    soup.indices.push_back(0);

    EXPECT_NO_THROW(sliceTriangles(soup, Z, 5.0));
}

TEST(SectionCapSlice, testASingleTriangleCrossingYieldsItsSegment)
{
    // the per triangle entry point the Coin traversal uses directly
    using V = Base::Vector3d;

    auto segment = planeTriangleIntersection(V(0, 0, -5), V(10, 0, -5), V(5, 0, 5), Z, 0.0);
    ASSERT_TRUE(segment.has_value());
    EXPECT_NEAR(segment.value().start.z, 0.0, 1e-9);
    EXPECT_NEAR(segment.value().end.z, 0.0, 1e-9);
}

TEST(SectionCapSlice, testATriangleClearOfThePlaneYieldsNothing)
{
    using V = Base::Vector3d;

    auto segment = planeTriangleIntersection(V(0, 0, 5), V(10, 0, 5), V(5, 0, 9), Z, 0.0);
    ASSERT_FALSE(segment.has_value());

    auto segment2 = planeTriangleIntersection(V(0, 0, -5), V(10, 0, -5), V(5, 0, -9), Z, 0.0);
    ASSERT_FALSE(segment2.has_value());
}

TEST(SectionCapSlice, testThePerTriangleAndSoupPathsAgree)
{
    // The soup version is what the tests above exercise and what the viewer
    // bypasses, so the two must not be allowed to drift apart.
    const TriangleSoup soup = box(10);
    const auto viaSoup = sliceTriangles(soup, Z, 5.0);

    std::vector<Segment> viaTriangle;
    for (std::size_t i = 0; i + 2 < soup.indices.size(); i += 3) {
        auto segment = planeTriangleIntersection(soup.points[soup.indices[i]],
                                                soup.points[soup.indices[i + 1]],
                                                soup.points[soup.indices[i + 2]],
                                                Z,
                                                5.0);
        if (segment.has_value()) {
            viaTriangle.push_back(segment.value());
        }
    }

    ASSERT_EQ(viaSoup.size(), viaTriangle.size());
    for (std::size_t i = 0; i < viaSoup.size(); ++i) {
        EXPECT_NEAR(Base::Distance(viaSoup[i].start, viaTriangle[i].start), 0.0, 1e-12);
        EXPECT_NEAR(Base::Distance(viaSoup[i].end, viaTriangle[i].end), 0.0, 1e-12);
    }
}

TEST(SectionCapChain, testBoxSectionChainsIntoOneClosedLoop)
{
    // Arrange
    const auto segments = sliceTriangles(box(10), Z, 5.0);

    // Act
    const auto loops = chainLoops(segments, 1e-7);

    // Assert - the outline of a box is a single closed rectangle
    ASSERT_EQ(loops.size(), 1);
    EXPECT_TRUE(isClosed(loops[0], 1e-7));
}

TEST(SectionCapChain, testTheLoopEnclosesTheCrossSectionArea)
{
    const auto loops = chainLoops(sliceTriangles(box(10), Z, 5.0), 1e-7);

    ASSERT_EQ(loops.size(), 1);
    EXPECT_NEAR(soupArea(fillLoops(loops, U, V, stripsAcross(10.0, 400))), 100.0, 1e-3);
}

TEST(SectionCapChain, testTwoSeparateBodiesGiveTwoLoops)
{
    // Arrange - two boxes side by side, sliced together
    TriangleSoup soup = box(10);
    TriangleSoup other = box(10);
    const int base = static_cast<int>(soup.points.size());
    for (auto& p : other.points) {
        p.x += 100;
        soup.points.push_back(p);
    }
    for (int idx : other.indices) {
        soup.indices.push_back(idx + base);
    }

    // Act
    const auto loops = chainLoops(sliceTriangles(soup, Z, 5.0), 1e-7);

    // Assert
    ASSERT_EQ(loops.size(), 2);
    EXPECT_TRUE(isClosed(loops[0], 1e-7));
    EXPECT_TRUE(isClosed(loops[1], 1e-7));
}

TEST(SectionCapChain, testAnOpenOutlineIsStillReturned)
{
    // Arrange - a single wall, so the crossing cannot close
    TriangleSoup soup;
    soup.points = {Base::Vector3d(0, 0, -5), Base::Vector3d(10, 0, -5),
                   Base::Vector3d(10, 0, 5), Base::Vector3d(0, 0, 5)};
    soup.indices = {0, 1, 2, 0, 2, 3};

    // Act
    const auto loops = chainLoops(sliceTriangles(soup, Z, 0.0), 1e-7);

    // Assert - a partial boundary is more use to draw than nothing at all
    ASSERT_EQ(loops.size(), 1);
    EXPECT_FALSE(isClosed(loops[0], 1e-7));
}

TEST(SectionCapChain, testEndpointsWithinToleranceAreJoined)
{
    // Arrange - a triangle whose corners miss each other by 1e-9
    using V = Base::Vector3d;
    std::vector<Segment> segments = {
        Segment {V(0, 0, 0), V(10, 0, 0)},
        Segment {V(10, 0, 1e-9), V(10, 10, 0)},
        Segment {V(10, 10, 0), V(0, 0, -1e-9)},
    };

    // Act
    const auto loops = chainLoops(segments, 1e-6);

    // Assert - tessellation seams must not break the chain
    ASSERT_EQ(loops.size(), 1);
    EXPECT_TRUE(isClosed(loops[0], 1e-6));
}

TEST(SectionCapChain, testNoSegmentsGivesNoLoops)
{
    EXPECT_TRUE(chainLoops({}, 1e-7).empty());
}

namespace
{


/// An axis aligned square loop on z = 0, wound counter clockwise.
std::vector<Base::Vector3d> square(double x0, double y0, double side)
{
    using Vec = Base::Vector3d;
    return {Vec(x0, y0, 0),
            Vec(x0 + side, y0, 0),
            Vec(x0 + side, y0 + side, 0),
            Vec(x0, y0 + side, 0)};
}

double totalLength(const std::vector<Segment>& segments)
{
    double sum = 0.0;
    for (const auto& s : segments) {
        sum += Base::Distance(s.start, s.end);
    }
    return sum;
}

}  // namespace

TEST(SectionCapHatch, testASquareIsFilledWithEvenlySpacedLines)
{
    // Arrange - a 10 x 10 square, hatched horizontally every 1 mm
    const std::vector<std::vector<Base::Vector3d>> loops = {square(0, 0, 10)};

    // Act
    const auto hatch = hatchLoops(loops, U, V, 1.0, 0.0);

    // Assert - the half open test puts a scanline on the lower boundary but not
    // the upper, so y runs 0..9: 10 lines, each spanning the full 10 mm width
    ASSERT_EQ(hatch.size(), 10);
    for (const auto& s : hatch) {
        EXPECT_NEAR(Base::Distance(s.start, s.end), 10.0, 1e-9);
    }
}

TEST(SectionCapHatch, testAHoleIsLeftUnhatched)
{
    // Arrange - a 10 x 10 square with a 4 x 4 hole in the middle
    const std::vector<std::vector<Base::Vector3d>> loops = {square(0, 0, 10), square(3, 3, 4)};

    // Act
    const auto hatch = hatchLoops(loops, U, V, 1.0, 0.0);

    // Assert - of the 10 scanlines the square gets, the 4 at y = 3..6 run
    // through the hole and give up 4 mm each
    EXPECT_NEAR(totalLength(hatch), 10 * 10.0 - 4 * 4.0, 1e-9);
}

TEST(SectionCapHatch, testNoHatchLineEntersTheHole)
{
    const std::vector<std::vector<Base::Vector3d>> loops = {square(0, 0, 10), square(3, 3, 4)};

    const auto hatch = hatchLoops(loops, U, V, 1.0, 0.0);

    // a segment spanning the hole would have to start left of it and end right
    for (const auto& s : hatch) {
        const bool spansHole = std::min(s.start.x, s.end.x) < 3.0
            && std::max(s.start.x, s.end.x) > 7.0 && s.start.y >= 3.0 && s.start.y < 7.0;
        EXPECT_FALSE(spansHole) << "hatch crossed the hole at y = " << s.start.y;
    }
}

TEST(SectionCapHatch, testTheAngleRotatesTheLines)
{
    const std::vector<std::vector<Base::Vector3d>> loops = {square(0, 0, 10)};

    const auto flat = hatchLoops(loops, U, V, 2.0, 0.0);
    const auto tilted = hatchLoops(loops, U, V, 2.0, M_PI / 4.0);

    // Assert - horizontal lines stay at constant y, 45 deg ones do not.
    // This is what lets two bodies be told apart by their hatch direction.
    ASSERT_FALSE(flat.empty());
    ASSERT_FALSE(tilted.empty());
    for (const auto& s : flat) {
        EXPECT_NEAR(s.start.y, s.end.y, 1e-9);
    }
    bool anySlanted = false;
    for (const auto& s : tilted) {
        anySlanted = anySlanted || std::abs(s.start.y - s.end.y) > 1e-6;
    }
    EXPECT_TRUE(anySlanted);
}

TEST(SectionCapHatch, testHatchStaysOnTheLoopsOwnPlane)
{
    // Arrange - the same square, but lifted to z = 7
    auto loop = square(0, 0, 10);
    for (auto& p : loop) {
        p.z = 7.0;
    }

    // Act
    const auto hatch = hatchLoops({loop}, U, V, 1.0, 0.0);

    // Assert - the in-plane axes say nothing about the offset along the normal,
    // so it has to be carried across explicitly or the hatch lands at z = 0
    ASSERT_FALSE(hatch.empty());
    for (const auto& s : hatch) {
        EXPECT_NEAR(s.start.z, 7.0, 1e-9);
        EXPECT_NEAR(s.end.z, 7.0, 1e-9);
    }
}

TEST(SectionCapHatch, testHatchingRunsFromTheSlicedGeometry)
{
    // Arrange - the whole chain, exactly as the view provider drives it
    const auto loops = chainLoops(sliceTriangles(box(10), Z, 5.0), 1e-7);
    ASSERT_EQ(loops.size(), 1);

    // Act
    const auto hatch = hatchLoops(loops, U, V, 1.0, M_PI / 4.0);

    // Assert - a 10 x 10 cross section hatched at 1 mm has to produce something
    EXPECT_FALSE(hatch.empty());
    EXPECT_GT(totalLength(hatch), 0.0);
}

TEST(SectionCapHatch, testNonsenseSpacingIsRefusedRatherThanExhaustingMemory)
{
    const std::vector<std::vector<Base::Vector3d>> loops = {square(0, 0, 10)};

    EXPECT_TRUE(hatchLoops(loops, U, V, 0.0, 0.0).empty());
    EXPECT_TRUE(hatchLoops(loops, U, V, -1.0, 0.0).empty());
    EXPECT_TRUE(hatchLoops(loops, U, V, std::nan(""), 0.0).empty());
    EXPECT_TRUE(hatchLoops(loops, U, V, 1.0, std::nan("")).empty());
    // 10 mm of section at 1e-9 spacing is ten billion lines - refuse it
    EXPECT_TRUE(hatchLoops(loops, U, V, 1e-9, 0.0).empty());
}

TEST(SectionCapHatch, testNoLoopsGiveNoHatch)
{
    EXPECT_TRUE(hatchLoops({}, U, V, 1.0, 0.0).empty());
}

TEST(SectionCapFill, testASquareIsFilledWithItsOwnArea)
{
    // Arrange - a 10 x 10 square
    const std::vector<std::vector<Base::Vector3d>> loops = {square(0, 0, 10)};

    // Act
    const auto soup = fillLoops(loops, U, V, stripsAcross(10.0, 200));

    // Assert - the strips tile the square exactly, so the areas agree
    EXPECT_NEAR(soupArea(soup), 100.0, 1e-6);
}

TEST(SectionCapFill, testAHoleIsNotFilled)
{
    // Arrange - 10 x 10 square with a 4 x 4 hole
    const std::vector<std::vector<Base::Vector3d>> loops = {square(0, 0, 10), square(3, 3, 4)};

    // Act
    const auto soup = fillLoops(loops, U, V, stripsAcross(10.0, 400));

    // Assert - the hole's 16 mm2 is missing. Filling it would put a surface
    // across a bore, which is exactly what the section is meant to reveal.
    EXPECT_NEAR(soupArea(soup), 100.0 - 16.0, 0.5);
}

TEST(SectionCapFill, testTheFillStaysOnTheLoopsOwnPlane)
{
    auto loop = square(0, 0, 10);
    for (auto& p : loop) {
        p.z = 7.0;
    }

    const auto soup = fillLoops({loop}, U, V, stripsAcross(10.0, 50));

    ASSERT_FALSE(soup.points.empty());
    for (const auto& p : soup.points) {
        EXPECT_NEAR(p.z, 7.0, 1e-9);
    }
}

TEST(SectionCapFill, testEveryTriangleIndexIsInRange)
{
    const auto soup = fillLoops({square(0, 0, 10), square(3, 3, 4)}, U, V, stripsAcross(10.0, 64));

    ASSERT_FALSE(soup.indices.empty());
    EXPECT_EQ(soup.indices.size() % 3, 0);
    for (int index : soup.indices) {
        EXPECT_GE(index, 0);
        EXPECT_LT(static_cast<std::size_t>(index), soup.points.size());
    }
}

TEST(SectionCapFill, testFillRunsFromTheSlicedGeometry)
{
    // the whole chain, as the view provider drives it
    const auto loops = chainLoops(sliceTriangles(box(10), Z, 5.0), 1e-7);
    ASSERT_EQ(loops.size(), 1);

    const auto soup = fillLoops(loops, U, V, stripsAcross(10.0, 128));

    EXPECT_NEAR(soupArea(soup), 100.0, 1e-6);
}

TEST(SectionCapFill, testNonsenseInputIsRefused)
{
    EXPECT_TRUE(fillLoops({}, U, V, stripsAcross(10.0, 100)).indices.empty());
    EXPECT_TRUE(fillLoops({square(0, 0, 10)}, U, V, 0.0).indices.empty());
    EXPECT_TRUE(fillLoops({square(0, 0, 10)}, U, V, -5.0).indices.empty());
}
// --- gaps found by a coverage run ----------------------------------------

TEST(SectionCapExtent, testTheExtentSpansTheBodyAlongTheNormal)
{
    // This is what lets a body the plane misses be skipped without visiting a
    // single triangle, so it had better report the right range.
    double lo = 0.0;
    double hi = 0.0;

    ASSERT_TRUE(extentAlong(boundsOf(box(10)), Z, lo, hi));
    EXPECT_NEAR(lo, 0.0, 1e-12);
    EXPECT_NEAR(hi, 10.0, 1e-12);
}

TEST(SectionCapExtent, testTheExtentFollowsTheNormalGiven)
{
    // Measured along the direction asked for, not along z by habit
    double lo = 0.0;
    double hi = 0.0;
    const Base::Vector3d diagonal = Base::Vector3d(1, 1, 0).Normalize();

    ASSERT_TRUE(extentAlong(boundsOf(box(10)), diagonal, lo, hi));
    EXPECT_NEAR(lo, 0.0, 1e-12);
    EXPECT_NEAR(hi, 10.0 * std::sqrt(2.0), 1e-9);
}

TEST(SectionCapExtent, testAnEmptyBodyHasNoExtent)
{
    // False, rather than a range nothing can be rejected against
    double lo = 1.0;
    double hi = 2.0;

    EXPECT_FALSE(extentAlong(boundsOf(TriangleSoup {}), Z, lo, hi));
}

TEST(SectionCapExtent, testTheExtentIsMeasuredInConstantTime)
{
    // Taking the box rather than the soup is the whole point: a body with a
    // hundred times the triangles must report the same range, because the range
    // never depended on the triangles.
    const TriangleSoup coarse = box(10);
    TriangleSoup dense = coarse;
    for (int i = 0; i < 100; ++i) {
        dense.points.insert(dense.points.end(), coarse.points.begin(), coarse.points.end());
    }

    double coarseLo = 0.0;
    double coarseHi = 0.0;
    double denseLo = 0.0;
    double denseHi = 0.0;
    ASSERT_TRUE(extentAlong(boundsOf(coarse), Z, coarseLo, coarseHi));
    ASSERT_TRUE(extentAlong(boundsOf(dense), Z, denseLo, denseHi));

    EXPECT_NEAR(coarseLo, denseLo, 1e-12);
    EXPECT_NEAR(coarseHi, denseHi, 1e-12);
}

TEST(SectionCapSlice, testAnEmptySoupSlicesToNothing)
{
    EXPECT_TRUE(sliceTriangles(TriangleSoup {}, Z, 0.0).empty());
}

TEST(SectionCapChain, testALoopTooShortToEncloseAnythingIsNotClosed)
{
    using Vec = Base::Vector3d;
    // two coincident points are not a loop, however close the ends are
    EXPECT_FALSE(isClosed({Vec(0, 0, 0), Vec(0, 0, 0)}, 1e-6));
    EXPECT_FALSE(isClosed({}, 1e-6));
}

TEST(SectionCapFill, testDegenerateLoopsFillNothing)
{
    // Loops of fewer than three points contribute no edges. Without the guard
    // the strip height comes out zero and the fill degenerates.
    using Vec = Base::Vector3d;
    const std::vector<std::vector<Base::Vector3d>> degenerate = {
        {Vec(0, 0, 0), Vec(10, 0, 0)},
        {Vec(5, 5, 0)},
    };

    EXPECT_TRUE(fillLoops(degenerate, U, V, stripsAcross(10.0, 100)).indices.empty());
}

TEST(SectionCapHatch, testDegenerateLoopsHatchNothing)
{
    using Vec = Base::Vector3d;
    const std::vector<std::vector<Base::Vector3d>> degenerate = {
        {Vec(0, 0, 0), Vec(10, 0, 0)},
    };

    EXPECT_TRUE(hatchLoops(degenerate, U, V, 1.0, 0.0).empty());
}

TEST(SectionCapFill, testAFlatRegionFillsNothing)
{
    // Every point on one line: there is no area to fill, and the strip height
    // would be zero.
    using Vec = Base::Vector3d;
    const std::vector<std::vector<Base::Vector3d>> flat = {
        {Vec(0, 0, 0), Vec(10, 0, 0), Vec(20, 0, 0)},
    };

    EXPECT_TRUE(fillLoops(flat, U, V, stripsAcross(10.0, 100)).indices.empty());
}

TEST(SectionCapFill, testNotANumberInALoopIsSkippedRatherThanPoisoningTheFill)
{
    // One bad vertex must not take the whole cap with it
    using Vec = Base::Vector3d;
    const double nan = std::nan("");
    std::vector<Base::Vector3d> loop = square(0, 0, 10);
    loop.push_back(Vec(nan, nan, 0));

    const auto soup = fillLoops({loop}, U, V, stripsAcross(10.0, 64));

    for (const auto& p : soup.points) {
        EXPECT_TRUE(std::isfinite(p.x));
        EXPECT_TRUE(std::isfinite(p.y));
        EXPECT_TRUE(std::isfinite(p.z));
    }
}
// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
