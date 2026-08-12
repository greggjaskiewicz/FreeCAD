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
    EXPECT_NEAR(std::abs(signedArea(loops[0], Z)), 100.0, 1e-6);
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

TEST(SectionCapArea, testWindingDeterminesTheSign)
{
    using V = Base::Vector3d;
    std::vector<Base::Vector3d> ccw = {V(0, 0, 0), V(10, 0, 0), V(10, 10, 0), V(0, 10, 0)};
    std::vector<Base::Vector3d> cw(ccw.rbegin(), ccw.rend());

    // a hole runs opposite to its outer boundary, which is how they are told apart
    EXPECT_GT(signedArea(ccw, Z), 0.0);
    EXPECT_LT(signedArea(cw, Z), 0.0);
    EXPECT_NEAR(signedArea(ccw, Z), -signedArea(cw, Z), 1e-9);
}
// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
