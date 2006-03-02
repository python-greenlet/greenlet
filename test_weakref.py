import gc
import greenlet
import weakref

def _dead_greenlet():
    g = greenlet.greenlet(lambda:None)
    g.switch()
    return g

def test_dead_weakref():
    o = weakref.ref(_dead_greenlet())
    gc.collect()
    assert o() is None

def test_inactive_weakref():
    o = weakref.ref(greenlet.greenlet())
    gc.collect()
    assert o() is None

if __name__ == '__main__':
    import sys
    mod = sys.modules[__name__]
    for name, fn in sorted((name, getattr(mod, name)) for name in dir(mod) if name.startswith('test_')):
        print fn.__name__
        fn()
        print ''
