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

#pragma once

#include <vector>

#include <Base/Vector3D.h>

#include <Mod/Part/PartGlobal.h>


namespace Part
{

/// Building a section cap from tessellation rather than from the exact solid.
///
/// Cutting a real assembly with OCCT costs minutes: one 92 face solid in a
/// customer model took 103 s inside BRepAlgoAPI_Section alone. The viewer,
/// however, already holds a triangulation of everything on screen, and slicing
/// triangles with a plane is linear and trivial. The cap that comes out is
/// exact with respect to what is drawn, since it is derived from the very
/// triangles being drawn, and only approximate with respect to the underlying
/// B-rep - which matters solely when the user asks for real geometry, and can
/// afford to wait for it.
///
/// The functions here are deliberately free of Coin and OCCT so they can be
/// tested directly.
namespace SectionCap
{

/// A single crossing of one triangle by the cutting plane.
struct Segment
{
    Base::Vector3d start;
    Base::Vector3d end;
};

/// Triangles are supplied flattened: `indices` holds 3 entries per triangle,
/// each an index into `points`.
struct TriangleSoup
{
    std::vector<Base::Vector3d> points;
    std::vector<int> indices;
};

/// Every place the plane crosses a triangle, as an unordered segment list.
///
/// Uses a half open sign test, so a triangle yields exactly zero or two
/// crossings and a vertex lying on the plane cannot produce a duplicate or a
/// dangling segment.
PartExport std::vector<Segment>
sliceTriangles(const TriangleSoup& soup, const Base::Vector3d& normal, double offset);

/// Join segments end to end into closed loops, within `tolerance`.
///
/// Tessellation seams leave endpoints that coincide only to within the mesh
/// tolerance, so joining has to be fuzzy. Chains that fail to close are still
/// returned - an open mesh has no closed outline, and a partial boundary is
/// more useful to draw than nothing.
PartExport std::vector<std::vector<Base::Vector3d>>
chainLoops(const std::vector<Segment>& segments, double tolerance);

/// True if the loop's first and last point meet within `tolerance`.
PartExport bool isClosed(const std::vector<Base::Vector3d>& loop, double tolerance);

/// Signed area of a loop projected onto the plane, using the frame's axes.
/// Positive means counter clockwise about `normal`; holes come out negative,
/// which is how outer boundaries are told from inner ones.
PartExport double signedArea(
    const std::vector<Base::Vector3d>& loop,
    const Base::Vector3d& normal
);

}  // namespace SectionCap
}  // namespace Part
