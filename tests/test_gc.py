import gc
import sys
import unittest
import weakref

import greenlet

def _dump_obj(obj):
    if obj is None:
        return
    for ref in gc.get_referrers(obj):
        print "0x%08x: referred by 0x%08x %r" % (id(obj), id(ref), ref)

def _live_greenlet_body():
    g = greenlet.getcurrent()
    try:
        g.parent.switch(g)
    finally:
        pass #print "live_greenlet_body dying"

def _live_stub_body(g):
    try:
        g.parent.switch(g)
    finally:
        pass #print "live_stub_body dying"

class _live_throw_exc(Exception):
    def __init__(self, g):
        self.greenlet = g

def _live_throw_body(g):
    try:
        g.parent.throw(_live_throw_exc(g))
    finally:
        pass #print "live_throw_body dying"

def _live_cluster_body(g):
    o = weakref.ref(greenlet.greenlet(_live_greenlet_body).switch())
    gc.collect()
    #o = greenlet.greenlet(_live_greenlet_body).switch()
    #_dump_obj(o())
    try:
        g.parent.switch(g)
    finally:
        pass #print "live_cluster_body dying"

def _make_green_weakref(body, kw=False):
    g = greenlet.greenlet(body)
    try:
        if kw:
            g = g.switch(g=g)
        else:
            g = g.switch(g)
    except _live_throw_exc:
        pass
    return weakref.ref(g)

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

    def test_stub_circular_ref(self):
        if not greenlet.GREENLET_USE_GC:
            return
        o = _make_green_weakref(_live_stub_body)
        gc.collect()
        if gc.garbage:
            #print gc.garbage
            self.assertFalse(gc.garbage)
        self.assertTrue(o() is None)

    def __disabled_test_stub_circular_ref_kw(self):
        if not greenlet.GREENLET_USE_GC:
            #print >>sys.stderr, "skipped", sys._getframe().f_code.co_name
            return
        o = _make_green_weakref(_live_stub_body, kw=True)
        gc.collect()
        if gc.garbage:
            #print gc.garbage
            self.assertFalse(gc.garbage)
        self.assertTrue(o() is None)

    def test_stub_throw_ref(self):
        if not greenlet.GREENLET_USE_GC:
            #print >>sys.stderr, "skipped", sys._getframe().f_code.co_name
            return
        o = _make_green_weakref(_live_throw_body)
        gc.collect()
        if gc.garbage:
            #print gc.garbage
            self.assertFalse(gc.garbage)
        self.assertTrue(o() is None)

    def test_stub_cluster_ref(self):
        o = _make_green_weakref(_live_cluster_body)
        gc.collect()
        #_dump_obj(o())
        if gc.garbage:
            self.assertFalse(gc.garbage)
        self.assertTrue(o() is None)
