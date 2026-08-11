# SPDX-License-Identifier: LGPL-2.1-or-later

import FreeCAD as App
import Part
import Sketcher

import unittest


NAN = float("nan")
INF = float("inf")


class TestNonFiniteConstraint(unittest.TestCase):
    """
    setDatum() refuses a datum that is not a finite number, but addConstraint()
    did not, so a NaN reached the solver. The solve then failed and left the
    sketch with a null shape, which surfaced much later as an OCCError about a
    NULL shape - a long way from the cause.

    Both routes to a datum should behave the same.
    """

    def setUp(self):
        self.doc = App.newDocument("NonFiniteConstraint")
        self.sketch = self.doc.addObject("Sketcher::SketchObject", "Sketch")
        self.sketch.addGeometry(
            Part.LineSegment(App.Vector(0, 0, 0), App.Vector(10, 0, 0)), False
        )

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def testAddConstraintRejectsNotANumber(self):
        self.assertRaises(
            ValueError, self.sketch.addConstraint, Sketcher.Constraint("DistanceX", 0, NAN)
        )

    def testAddConstraintRejectsInfinity(self):
        self.assertRaises(
            ValueError, self.sketch.addConstraint, Sketcher.Constraint("DistanceX", 0, INF)
        )

    def testAddConstraintRejectsNotANumberAngle(self):
        self.assertRaises(
            ValueError, self.sketch.addConstraint, Sketcher.Constraint("Angle", 0, NAN)
        )

    def testSetDatumRejectsNotANumber(self):
        # the behaviour addConstraint now matches
        self.sketch.addConstraint(Sketcher.Constraint("DistanceX", 0, 5.0))
        self.assertRaises(ValueError, self.sketch.setDatum, 0, NAN)

    def testRejectedConstraintLeavesTheSketchUsable(self):
        # the point of rejecting early: the sketch must not be left broken
        try:
            self.sketch.addConstraint(Sketcher.Constraint("DistanceX", 0, NAN))
        except ValueError:
            pass
        self.doc.recompute()
        self.assertTrue(self.sketch.Shape.isValid())
        self.assertEqual(len(self.sketch.Constraints), 0)

    def testValidConstraintStillWorks(self):
        self.sketch.addConstraint(Sketcher.Constraint("DistanceX", 0, 7.5))
        self.doc.recompute()
        self.assertTrue(self.sketch.Shape.isValid())
        self.assertAlmostEqual(self.sketch.Shape.Edges[0].Length, 7.5, places=6)
