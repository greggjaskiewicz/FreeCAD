# SPDX-License-Identifier: LGPL-2.1-or-later

import FreeCAD as App
import Part

import unittest


class TestFaceCutHoles(unittest.TestCase):
    """
    Face.cutHoles() must subtract the hole whichever way round the hole wire
    runs. OCCT's BRepBuilderAPI_MakeFace::Add() only subtracts a wire that is
    oriented opposite to the outer wire; given a wire running the same way it
    adds the area instead, producing an invalid face.

    Reported as FreeCAD/FreeCAD#29543.
    """

    # outer 30 x 20 = 600, hole 20 x 10 = 200, so a correct cut leaves 400
    OUTER = [
        App.Vector(0, 0, 0),
        App.Vector(30, 0, 0),
        App.Vector(30, 20, 0),
        App.Vector(0, 20, 0),
    ]
    HOLE = [
        App.Vector(5, 5, 0),
        App.Vector(25, 5, 0),
        App.Vector(25, 15, 0),
        App.Vector(5, 15, 0),
    ]
    EXPECTED_AREA = 400.0

    def _faceWithHole(self, hole_points):
        outer = Part.makePolygon(self.OUTER + [self.OUTER[0]])
        hole = Part.makePolygon(hole_points + [hole_points[0]])
        face = Part.Face(outer)
        face.cutHoles([hole])
        return face

    def testCutHolesWithOppositeWindingSubtractsTheHole(self):
        # the hole wire runs opposite to the outer wire, which OCCT accepts
        face = self._faceWithHole(list(reversed(self.HOLE)))

        self.assertTrue(face.isValid())
        self.assertAlmostEqual(face.Area, self.EXPECTED_AREA, places=6)

    def testCutHolesWithSameWindingSubtractsTheHole(self):
        # the hole wire runs the same way as the outer wire. cutHoles() should
        # still subtract it rather than adding its area.
        face = self._faceWithHole(self.HOLE)

        self.assertTrue(face.isValid(), "cutHoles produced an invalid face")
        self.assertAlmostEqual(face.Area, self.EXPECTED_AREA, places=6)

    def testCutHolesGivesTheSameResultEitherWinding(self):
        # the winding of the hole wire is not something a caller should have to
        # know about, so both directions must agree
        same = self._faceWithHole(self.HOLE)
        opposite = self._faceWithHole(list(reversed(self.HOLE)))

        self.assertAlmostEqual(same.Area, opposite.Area, places=6)
