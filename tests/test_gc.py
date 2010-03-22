import gc
import sys
import unittest
import weakref

import greenlet

def _live_greenlet_body():
    g = greenlet.getcurrent()
    try:
        g.parent.switch(g)
    finally:
        pass #print "live_greenlet_body dying"

class GCTests(unittest.TestCase):
    def test_circular_greenlet(self):
        class circular_greenlet(greenlet.greenlet):
            pass
        o = circular_greenlet()
        o.self = o
        o = weakref.ref(o)
        gc.collect()
        if gc.garbage:
            #print gc.garbage
            self.assertFalse(gc.garbage)
        self.assertTrue(o() is None)

    def test_dead_circular_ref(self):
        if not greenlet.GREENLET_USE_GC:
            #print >>sys.stderr, "skipped", sys._getframe().f_code.co_name
            return
        o = weakref.ref(greenlet.greenlet(greenlet.getcurrent).switch())
        gc.collect()
        if gc.garbage:
            #print gc.garbage
            self.assertFalse(gc.garbage)
        self.assertTrue(o() is None)

    def test_live_circular_ref(self):
        if not greenlet.GREENLET_USE_GC:
            #print >>sys.stderr, "skipped", sys._getframe().f_code.co_name
            return
        o = weakref.ref(greenlet.greenlet(_live_greenlet_body).switch())
        gc.collect()
        if gc.garbage:
            #print gc.garbage
            self.assertFalse(gc.garbage)
        self.assertTrue(o() is None)
