import unittest
import sys
import gc

import weakref
import greenlet
import threading


class ArgRefcountTests(unittest.TestCase):
    def test_arg_refs(self):
        args = ('a', 'b', 'c')
        refcount_before = sys.getrefcount(args)
        g = greenlet.greenlet(
            lambda *args: greenlet.getcurrent().parent.switch(*args))
        for i in range(100):
            g.switch(*args)
        self.assertEqual(sys.getrefcount(args), refcount_before)

    def test_kwarg_refs(self):
        kwargs = {}
        g = greenlet.greenlet(
            lambda **kwargs: greenlet.getcurrent().parent.switch(**kwargs))
        for i in range(100):
            g.switch(**kwargs)
        self.assertEqual(sys.getrefcount(kwargs), 2)

    def test_threaded_leak(self):
        gg = []
        def worker():
            # only main greenlet present
            gg.append(weakref.ref(greenlet.getcurrent()))
        for i in xrange(2):
            t = threading.Thread(target=worker)
            t.start()
            t.join()
        greenlet.getcurrent()
        gc.collect()
        for g in gg:
            self.assertTrue(g() is None)

    def test_threaded_adv_leak(self):
        gg = []
        ll = []
        def worker():
            # main and additional *finished* greenlets
            def additional():
                ll.append(greenlet.getcurrent())
            for i in xrange(2):
                greenlet.greenlet(additional).switch()
            gg.append(weakref.ref(greenlet.getcurrent()))
        for i in xrange(3):
            t = threading.Thread(target=worker)
            t.start()
            t.join()
            del ll[:]
        greenlet.getcurrent()
        gc.collect()
        for g in gg:
            self.assertTrue(g() is None)
