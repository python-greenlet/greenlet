import gc
import greenlet
import weakref
import sys

def _live_greenlet_body():
    g = greenlet.getcurrent()
    g.parent.switch(g)

def test_circular_ref():
    if not greenlet.GREENLET_USE_GC:
        print >>sys.stderr, "skipped", sys._getframe().f_code.co_name
        return
    o = weakref.ref(greenlet.greenlet(_live_greenlet_body).switch())
    gc.collect()
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
