import gc
import sys
import time
import threading
import unittest

from greenlet import greenlet


class SomeError(Exception):
    pass


def fmain(seen):
    try:
        greenlet.getcurrent().parent.switch()
    except:
        seen.append(sys.exc_info()[0])
        raise
    raise SomeError


def send_exception(g, exc):
    # note: send_exception(g, exc)  can be now done with  g.throw(exc).
    # the purpose of this test is to explicitely check the propagation rules.
    def crasher(exc):
        raise exc
    g1 = greenlet(crasher, parent=g)
    g1.switch(exc)


class GreenletTests(unittest.TestCase):
    def test_simple(self):
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
        self.assertEqual(lst, list(range(5)))

    def test_parent_equals_None(self):
        g = greenlet(parent=None)

    def test_run_equals_None(self):
        g = greenlet(run=None)

    def test_two_children(self):
        lst = []

        def f():
            lst.append(1)
            greenlet.getcurrent().parent.switch()
            lst.extend([1, 1])
        g = greenlet(f)
        h = greenlet(f)
        g.switch()
        self.assertEqual(len(lst), 1)
        h.switch()
        self.assertEqual(len(lst), 2)
        h.switch()
        self.assertEqual(len(lst), 4)
        self.assertEqual(h.dead, True)
        g.switch()
        self.assertEqual(len(lst), 6)
        self.assertEqual(g.dead, True)

    def test_two_recursive_children(self):
        lst = []

        def f():
            lst.append(1)
            greenlet.getcurrent().parent.switch()

        def g():
            lst.append(1)
            g = greenlet(f)
            g.switch()
            lst.append(1)
        g = greenlet(g)
        g.switch()
        self.assertEqual(len(lst), 3)
        self.assertEqual(sys.getrefcount(g), 2)

    def test_threads(self):
        success = []

        def f():
            self.test_simple()
            success.append(True)
        ths = [threading.Thread(target=f) for i in range(10)]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
        self.assertEqual(len(success), len(ths))

    def test_exception(self):
        seen = []
        g1 = greenlet(fmain)
        g2 = greenlet(fmain)
        g1.switch(seen)
        g2.switch(seen)
        g2.parent = g1
        self.assertEqual(seen, [])
        self.assertRaises(SomeError, g2.switch)
        self.assertEqual(seen, [SomeError])
        g2.switch()
        self.assertEqual(seen, [SomeError])

    def test_send_exception(self):
        seen = []
        g1 = greenlet(fmain)
        g1.switch(seen)
        self.assertRaises(KeyError, send_exception, g1, KeyError)
        self.assertEqual(seen, [KeyError])

    def test_dealloc(self):
        seen = []
        g1 = greenlet(fmain)
        g2 = greenlet(fmain)
        g1.switch(seen)
        g2.switch(seen)
        self.assertEqual(seen, [])
        del g1
        gc.collect()
        self.assertEqual(seen, [greenlet.GreenletExit])
        del g2
        gc.collect()
        self.assertEqual(seen, [greenlet.GreenletExit, greenlet.GreenletExit])

    def test_dealloc_other_thread(self):
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
        self.assertEqual(seen, [])
        self.assertEqual(len(someref), 1)
        del someref[:]
        gc.collect()
        # g1 is not released immediately because it's from another thread
        self.assertEqual(seen, [])
        lock2.release()
        lock.acquire()
        self.assertEqual(seen, [greenlet.GreenletExit])
        lock2.release()
        t.join()

    def test_frame(self):
        def f1():
            f = sys._getframe(0)
            self.assertEqual(f.f_back, None)
            greenlet.getcurrent().parent.switch(f)
            return "meaning of life"
        g = greenlet(f1)
        frame = g.switch()
        self.assertTrue(frame is g.gr_frame)
        self.assertTrue(g)
        next = g.switch()
        self.assertFalse(g)
        self.assertEqual(next, 'meaning of life')
        self.assertEqual(g.gr_frame, None)

    def test_thread_bug(self):
        def runner(x):
            g = greenlet(lambda: time.sleep(x))
            g.switch()
        t1 = threading.Thread(target=runner, args=(0.2,))
        t2 = threading.Thread(target=runner, args=(0.3,))
        t1.start()
        t2.start()
        t1.join()
        t2.join()

    def test_switch_kwargs(self):
        def foo(a, b):
            self.assertEqual(a, 4)
            self.assertEqual(b, 2)
        greenlet(foo).switch(a=4, b=2)

    def test_switch_kwargs_to_parent(self):
        def foo(x):
            greenlet.getcurrent().parent.switch(x=x)
            greenlet.getcurrent().parent.switch(2, x=3)
            return x, x ** 2
        g = greenlet(foo)
        self.assertEqual({'x': 3}, g.switch(3))
        self.assertEqual(((2,), {'x': 3}), g.switch())
        self.assertEqual((3, 9), g.switch())

    def test_switch_to_another_thread(self):
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
        self.assertTrue(error != None, "greenlet.error was not raised!")
        done_event.set()
        thread.join()

    def test_exc_state(self):
        def f():
            try:
                raise ValueError('fun')
            except:
                exc_info = sys.exc_info()
                greenlet(h).switch()
                self.assertEqual(exc_info, sys.exc_info())

        def h():
            self.assertEqual(sys.exc_info(), (None, None, None))

        greenlet(f).switch()
