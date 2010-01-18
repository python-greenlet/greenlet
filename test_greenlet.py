from greenlet import greenlet
import sys, threading

def raises(exc, fn, *args, **kw):
    try:
        fn(*args, **kw)
    except exc:
        return
    assert False, "did not raise " + exc.__name__

def test_simple():
    lst = []
    def f():
        lst.append(1)
        greenlet.getcurrent().parent.switch()
        lst.append(3)
    g = greenlet(f)
    lst.append(0)
    g.switch()
    lst.append(2)
    g.switch()
    lst.append(4)
    assert lst == list(range(5))

def test_threads():
    success = []
    def f():
        test_simple()
        success.append(True)
    ths = [threading.Thread(target=f) for i in range(10)]
    for th in ths:
        th.start()
    for th in ths:
        th.join()
    assert len(success) == len(ths)


class SomeError(Exception):
    pass

def fmain(seen):
    try:
        greenlet.getcurrent().parent.switch()
    except:
        seen.append(sys.exc_info()[0])
        raise
    raise SomeError

def test_exception():
    seen = []
    g1 = greenlet(fmain)
    g2 = greenlet(fmain)
    g1.switch(seen)
    g2.switch(seen)
    g2.parent = g1
    assert seen == []
    raises(SomeError, g2.switch)
    assert seen == [SomeError]
    g2.switch()
    assert seen == [SomeError]

def send_exception(g, exc):
    def crasher(exc):
        raise exc
    g1 = greenlet(crasher, parent=g)
    g1.switch(exc)

def test_send_exception():
    seen = []
    g1 = greenlet(fmain)
    g1.switch(seen)
    raises(KeyError, send_exception, g1, KeyError)
    assert seen == [KeyError]

def test_greenlet_throw_instance():
    def f():
        greenlet.getcurrent().parent.throw(SomeError("handle this!"))
    g = greenlet(f)
    seen = []
    try:
        g.switch()
    except SomeError:
        seen.append(SomeError)
    assert seen == [SomeError]

def test_greenlet_throw_exception():
    def f():
        greenlet.getcurrent().parent.throw(SomeError, "handle this!")
    g = greenlet(f)
    seen = []
    try:
        g.switch()
    except SomeError:
        seen.append(SomeError)
    assert seen == [SomeError]

def test_dealloc():
    seen = []
    g1 = greenlet(fmain)
    g2 = greenlet(fmain)
    g1.switch(seen)
    g2.switch(seen)
    assert seen == []
    del g1
    assert seen == [greenlet.GreenletExit]
    del g2
    assert seen == [greenlet.GreenletExit, greenlet.GreenletExit]

def test_dealloc_other_thread():
    seen = []
    someref = []
    lock = threading.Lock()
    lock.acquire()
    lock2 = threading.Lock()
    lock2.acquire()
    def f():
        g1 = greenlet(fmain)
        g1.switch(seen)
        someref.append(g1)
        del g1
        lock.release()
        lock2.acquire()
        greenlet()   # trigger release
        lock.release()
        lock2.acquire()
    t = threading.Thread(target=f)
    t.start()
    lock.acquire()
    assert seen == []
    assert len(someref) == 1
    del someref[:]
    # g1 is not released immediately because it's from another thread
    assert seen == []
    lock2.release()
    lock.acquire()
    assert seen == [greenlet.GreenletExit]
    lock2.release()
    t.join()

def test_frame():
    def f1():
        f = sys._getframe(0)
        assert f.f_back is None
        greenlet.getcurrent().parent.switch(f)
        return "meaning of life"
    g = greenlet(f1)
    frame = g.switch()
    assert frame is g.gr_frame
    assert g
    next = g.switch()
    assert not g
    assert next == "meaning of life"
    assert g.gr_frame is None
