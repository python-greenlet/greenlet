import gc
import greenlet
import weakref
import unittest


class WeakRefTests(unittest.TestCase):
    def test_dead_weakref(self):
        def _dead_greenlet():
            g = greenlet.greenlet(lambda: None)
            g.switch()
            return g
        o = weakref.ref(_dead_greenlet())
        gc.collect()
        self.assertEqual(o(), None)

    def test_inactive_weakref(self):
        o = weakref.ref(greenlet.greenlet())
        gc.collect()
        self.assertEqual(o(), None)
