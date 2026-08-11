# SPDX-License-Identifier: LGPL-2.1-or-later

import FreeCAD as App

from CompoundTools import CompoundFilter

import unittest


class TestCompoundFilterPlacement(unittest.TestCase):
    """
    Explode Compound is implemented as a CompoundFilter per item, so a filtered
    item has to keep the placement it had inside the compound rather than
    snapping back to the origin.

    Reported as FreeCAD/FreeCAD#29413. That report does not reproduce with
    primitives, but the behaviour is worth pinning either way.
    """

    def setUp(self):
        self.doc = App.newDocument("CompoundFilterPlacement")

    def tearDown(self):
        App.closeDocument(self.doc.Name)

    def _filteredSecondItem(self, placement):
        first = self.doc.addObject("Part::Box", "First")
        second = self.doc.addObject("Part::Box", "Second")
        second.Placement = placement
        self.doc.recompute()

        compound = self.doc.addObject("Part::Compound", "Compound")
        compound.Links = [first, second]
        self.doc.recompute()

        filt = CompoundFilter.makeCompoundFilter(name="Filter")
        filt.Base = compound
        filt.FilterType = "specific items"
        filt.items = "1"
        self.doc.recompute()
        return filt

    def testFilteredItemKeepsItsTranslation(self):
        filt = self._filteredSecondItem(App.Placement(App.Vector(50, 0, 0), App.Rotation()))

        # the box is 10 wide and sat at x = 50, so it must still span 50..60
        self.assertAlmostEqual(filt.Shape.BoundBox.XMin, 50.0, places=6)
        self.assertAlmostEqual(filt.Shape.BoundBox.XMax, 60.0, places=6)

    def testFilteredItemKeepsItsRotation(self):
        turned = App.Placement(App.Vector(50, 0, 0), App.Rotation(App.Vector(0, 0, 1), 45))
        filt = self._filteredSecondItem(turned)

        # a 10 x 10 box turned 45 degrees about its corner reaches back to
        # 50 - 10/sqrt(2), so a dropped rotation would show up here
        self.assertAlmostEqual(filt.Shape.BoundBox.XMin, 50.0 - 10.0 / (2**0.5), places=3)

    def testFilteredItemMatchesTheSourceVolume(self):
        filt = self._filteredSecondItem(App.Placement(App.Vector(50, 0, 0), App.Rotation()))

        # placement handling must not deform the shape
        self.assertAlmostEqual(filt.Shape.Volume, 1000.0, places=6)
