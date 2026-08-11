# SPDX-License-Identifier: LGPL-2.1-or-later

import FreeCAD as App
import Part

import unittest


NAN = float("nan")
INF = float("inf")


class TestNonFiniteInput(unittest.TestCase):
    """
    Part's factory functions reject dimensions that are zero or negative, but
    the guards are written as `value < Precision::Confusion()`. Every comparison
    against NaN is false, so a NaN dimension passes straight through into OCCT
    and yields a shape that is invalid, or worse, one that reports itself valid
    while carrying NaN coordinates.

    Zero and negative are already rejected; these tests cover the gap.
    """

    def testMakeBoxRejectsNotANumber(self):
        # the finite equivalents already raise
        self.assertRaises(ValueError, Part.makeBox, 0, 1, 1)
        self.assertRaises(ValueError, Part.makeBox, -1, 1, 1)

        self.assertRaises(ValueError, Part.makeBox, NAN, 1, 1)
        self.assertRaises(ValueError, Part.makeBox, 1, NAN, 1)
        self.assertRaises(ValueError, Part.makeBox, 1, 1, NAN)

    def testMakeBoxRejectsInfinity(self):
        self.assertRaises(ValueError, Part.makeBox, INF, 1, 1)

    def testMakeCylinderRejectsNotANumber(self):
        self.assertRaises(ValueError, Part.makeCylinder, NAN, 1)

    def testMakeSphereRejectsNotANumber(self):
        self.assertRaises(ValueError, Part.makeSphere, NAN)

    def testMakeConeRejectsNotANumber(self):
        self.assertRaises(ValueError, Part.makeCone, 1, 0.5, NAN)

    def testMakeTorusRejectsNotANumber(self):
        self.assertRaises(ValueError, Part.makeTorus, 2, NAN)

    def testMakePlaneRejectsNotANumber(self):
        self.assertRaises(ValueError, Part.makePlane, NAN, 1)

    def testMakeLineRejectsNotANumberPoint(self):
        # left unchecked this builds a shape that reports itself valid while
        # holding NaN coordinates, which poisons anything downstream
        self.assertRaises(ValueError, Part.makeLine, App.Vector(0, 0, 0), App.Vector(NAN, 0, 0))

    def testMakeCircleRejectsNotANumber(self):
        self.assertRaises(ValueError, Part.makeCircle, NAN)

    def testMakePolygonRejectsNotANumberVertex(self):
        pts = [
            App.Vector(0, 0, 0),
            App.Vector(NAN, 1, 0),
            App.Vector(1, 1, 0),
            App.Vector(0, 0, 0),
        ]
        self.assertRaises(ValueError, Part.makePolygon, pts)
