# SPDX-License-Identifier: LGPL-2.1-or-later

import FreeCAD as App
import Part

import unittest


NAN = float("nan")
INF = float("inf")


class TestNonFiniteGuards(unittest.TestCase):
    """
    Guards written as `value < tolerance` do not reject NaN, because every
    comparison against NaN is false. The value then reaches OCCT, which either
    builds nonsense or, for Shape.scale(), never returns at all.

    Each test names the guard it covers.
    """

    def testScaleRejectsNotANumber(self):
        # TopoShapePy::scale guards with fabs(factor) < Precision::Confusion().
        # fabs(NaN) is NaN, so NaN reaches gp_Trsf::SetScale and
        # BRepBuilderAPI_Transform never returns - this hangs FreeCAD outright.
        shape = Part.makeBox(1, 1, 1)
        self.assertRaises(ValueError, shape.scale, NAN)

    def testScaleRejectsInfinity(self):
        shape = Part.makeBox(1, 1, 1)
        self.assertRaises(ValueError, shape.scale, INF)

    def testScaleStillRejectsZero(self):
        # the behaviour the guard was written for must not regress
        shape = Part.makeBox(1, 1, 1)
        self.assertRaises(ValueError, shape.scale, 0.0)

    def testScaleStillAcceptsAValidFactor(self):
        shape = Part.makeBox(1, 1, 1)
        shape.scale(2.0)
        self.assertTrue(shape.isValid())
        self.assertAlmostEqual(shape.Volume, 8.0, places=6)

    def testTranslateRejectsNotANumber(self):
        shape = Part.makeBox(1, 1, 1)
        self.assertRaises(ValueError, shape.translate, App.Vector(NAN, 0, 0))

    def testRotateRejectsNotANumberAngle(self):
        shape = Part.makeBox(1, 1, 1)
        self.assertRaises(
            ValueError, shape.rotate, App.Vector(0, 0, 0), App.Vector(0, 0, 1), NAN
        )

    def testCircleRejectsNotANumberRadius(self):
        self.assertRaises(
            ValueError, Part.Circle, App.Vector(0, 0, 0), App.Vector(0, 0, 1), NAN
        )

    def testSphereRadiusRejectsNotANumber(self):
        sphere = Part.Sphere()
        with self.assertRaises(ValueError):
            sphere.Radius = NAN

    def testCylinderRadiusRejectsNotANumber(self):
        cylinder = Part.Cylinder()
        with self.assertRaises(ValueError):
            cylinder.Radius = NAN

    def testBezierSetWeightRejectsNotANumber(self):
        # guarded with weight < 0.0, which NaN slips past
        curve = Part.BezierCurve()
        curve.setPoles([App.Vector(0, 0, 0), App.Vector(1, 0, 0), App.Vector(1, 1, 0)])
        self.assertRaises(ValueError, curve.setWeight, 1, NAN)

    def testBSplineSetWeightRejectsNotANumber(self):
        curve = Part.BSplineCurve()
        curve.interpolate([App.Vector(0, 0, 0), App.Vector(1, 0, 0), App.Vector(2, 1, 0)])
        self.assertRaises(ValueError, curve.setWeight, 1, NAN)
