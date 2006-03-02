import gc
import greenlet
import weakref
import sys


def _live_greenlet_body():
    g = greenlet.getcurrent()
    try:
        g.parent.switch(g)
    finally:
        print "live_greenlet_body dying"

def test_circular_greenlet():
    class circular_greenlet(greenlet.greenlet):
        pass
    o = circular_greenlet()
    o.self = o
    o = weakref.ref(o)
    gc.collect()
    if gc.garbage:
        print gc.garbage
        assert not gc.garbage
    assert o() is None

def test_dead_circular_ref():
    if not greenlet.GREENLET_USE_GC:
        print >>sys.stderr, "skipped", sys._getframe().f_code.co_name
        return
    o = weakref.ref(greenlet.greenlet(greenlet.getcurrent).switch())
    gc.collect()
    if gc.garbage:
        print gc.garbage
        assert not gc.garbage
    assert o() is None

def test_live_circular_ref():
    if not greenlet.GREENLET_USE_GC:
        print >>sys.stderr, "skipped", sys._getframe().f_code.co_name
        return
    o = weakref.ref(greenlet.greenlet(_live_greenlet_body).switch())
    gc.collect()
    if gc.garbage:
        print gc.garbage
        assert not gc.garbage
    assert o() is None

if __name__ == '__main__':
    import sys
    mod = sys.modules[__name__]
    if not greenlet.GREENLET_USE_GC:
        print >>sys.stderr, "SKIPPING GC TESTS"
    else:
        for name, fn in sorted((name, getattr(mod, name)) for name in dir(mod) if name.startswith('test_')):
            print fn.__name__
            fn()
            print ''
