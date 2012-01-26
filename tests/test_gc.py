import gc
import sys
import unittest
import weakref

import greenlet


class GCTests(unittest.TestCase):
    def test_circular_greenlet(self):
        class circular_greenlet(greenlet.greenlet):
            pass
        o = circular_greenlet()
        o.self = o
        o = weakref.ref(o)
        gc.collect()
        self.assertTrue(o() is None)
        self.assertFalse(gc.garbage, gc.garbage)

    def test_dead_circular_ref(self):
        o = weakref.ref(greenlet.greenlet(greenlet.getcurrent).switch())
        gc.collect()
        self.assertTrue(o() is None)
        self.assertFalse(gc.garbage, gc.garbage)

    if greenlet.GREENLET_USE_GC:
        # These only work with greenlet gc support

        def test_inactive_ref(self):
            class inactive_greenlet(greenlet.greenlet):
                def __init__(self):
                    greenlet.greenlet.__init__(self, run=self.run)

                def run(self):
                    pass
            o = inactive_greenlet()
            o = weakref.ref(o)
            gc.collect()
            self.assertTrue(o() is None)
            self.assertFalse(gc.garbage, gc.garbage)
