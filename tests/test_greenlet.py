from greenlet import greenlet
import gc
import sys
import tests
import time
import threading

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
    tests.raises(SomeError, g2.switch)
    assert seen == [SomeError]
    g2.switch()
    assert seen == [SomeError]

def send_exception(g, exc):
    # note: send_exception(g, exc)  can be now done with  g.throw(exc).
    # the purpose of this test is to explicitely check the propagation rules.
    def crasher(exc):
        raise exc
    g1 = greenlet(crasher, parent=g)
    g1.switch(exc)

def test_send_exception():
    seen = []
    g1 = greenlet(fmain)
    g1.switch(seen)
    tests.raises(KeyError, send_exception, g1, KeyError)
    assert seen == [KeyError]

def test_dealloc():
    seen = []
    g1 = greenlet(fmain)
    g2 = greenlet(fmain)
    g1.switch(seen)
    g2.switch(seen)
    assert seen == []
    del g1
    gc.collect()
    assert seen == [greenlet.GreenletExit]
    del g2
    gc.collect()
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
        gc.collect()
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
    gc.collect()
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

def test_thread_bug():
    def runner(x):
        g = greenlet(lambda: time.sleep(x))
        g.switch()
    t1 = threading.Thread(target=runner, args=(0.2,))
    t2 = threading.Thread(target=runner, args=(0.3,))
    t1.start()
    t2.start()
    t1.join()
    t2.join()

def test_switch_kwargs():
    def foo(a, b):
        assert a == 4
        assert b == 2
    greenlet(foo).switch(a=4, b=2)

def test_switch_kwargs_to_parent():
    def foo(x):
        greenlet.getcurrent().parent.switch(x=x)
        greenlet.getcurrent().parent.switch(2, x=3)
        return x, x ** 2
    g = greenlet(foo)
    assert {'x': 3} == g.switch(3)
    assert ((2,), {'x': 3}) == g.switch()
    assert (3, 9) == g.switch()

def test_switch_to_another_thread():
    data = {}
    error = None
    created_event = threading.Event()
    done_event = threading.Event()
    def foo():
        data['g'] = greenlet(lambda: None)
        created_event.set()
        done_event.wait()
    thread = threading.Thread(target=foo)
    thread.start()
    created_event.wait()
    try:
        data['g'].switch()
    except greenlet.error:
        error = sys.exc_info()[1]
    assert error != None, "greenlet.error was not raised!"
    done_event.set()
    thread.join()

