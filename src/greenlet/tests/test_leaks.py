import unittest
import sys
import gc

import time
import weakref
import threading

import greenlet

class TestLeaks(unittest.TestCase):

    def test_arg_refs(self):
        args = ('a', 'b', 'c')
        refcount_before = sys.getrefcount(args)
        # pylint:disable=unnecessary-lambda
        g = greenlet.greenlet(
            lambda *args: greenlet.getcurrent().parent.switch(*args))
        for _ in range(100):
            g.switch(*args)
        self.assertEqual(sys.getrefcount(args), refcount_before)

    def test_kwarg_refs(self):
        kwargs = {}
        # pylint:disable=unnecessary-lambda
        g = greenlet.greenlet(
            lambda **kwargs: greenlet.getcurrent().parent.switch(**kwargs))
        for _ in range(100):
            g.switch(**kwargs)
        self.assertEqual(sys.getrefcount(kwargs), 2)

    assert greenlet.GREENLET_USE_GC # Option to disable this was removed in 1.0

    def recycle_threads(self):
        # By introducing a thread that does sleep we allow other threads,
        # that have triggered their __block condition, but did not have a
        # chance to deallocate their thread state yet, to finally do so.
        # The way it works is by requiring a GIL switch (different thread),
        # which does a GIL release (sleep), which might do a GIL switch
        # to finished threads and allow them to clean up.
        def worker():
            time.sleep(0.001)
        t = threading.Thread(target=worker)
        t.start()
        time.sleep(0.001)
        t.join()

    def test_threaded_leak(self):
        gg = []
        def worker():
            # only main greenlet present
            gg.append(weakref.ref(greenlet.getcurrent()))
        for _ in range(2):
            t = threading.Thread(target=worker)
            t.start()
            t.join()
            del t
        greenlet.getcurrent() # update ts_current
        self.recycle_threads()
        greenlet.getcurrent() # update ts_current
        gc.collect()
        greenlet.getcurrent() # update ts_current
        for g in gg:
            self.assertIsNone(g())

    def test_threaded_adv_leak(self):
        gg = []
        def worker():
            # main and additional *finished* greenlets
            ll = greenlet.getcurrent().ll = []
            def additional():
                ll.append(greenlet.getcurrent())
            for _ in range(2):
                greenlet.greenlet(additional).switch()
            gg.append(weakref.ref(greenlet.getcurrent()))
        for _ in range(2):
            t = threading.Thread(target=worker)
            t.start()
            t.join()
            del t
        greenlet.getcurrent() # update ts_current
        self.recycle_threads()
        greenlet.getcurrent() # update ts_current
        gc.collect()
        greenlet.getcurrent() # update ts_current
        for g in gg:
            self.assertIsNone(g())

    def test_issue251_killing_cross_thread_leaks_list(self):
        # See https://github.com/python-greenlet/greenlet/issues/251
        # Killing a greenlet (probably not the main one)
        # in one thread from another thread would
        # result in leaking a list (the ts_delkey list).

        # For the test to be valid, even empty lists have to be tracked by the
        # GC
        assert gc.is_tracked([])

        def count_lists():
            # pylint:disable=unidiomatic-typecheck
            # Collect the garbage.
            for _ in range(3):
                gc.collect()
            gc.collect()
            return sum(
                1
                for x in gc.get_objects()
                if type(x) is list
            )

        background_glet_running = threading.Event()
        background_glet_killed = threading.Event()
        background_greenlets = []
        def background_greenlet():
            # Throw control back to the main greenlet.
            greenlet.getcurrent().parent.switch()

        def background_thread():
            glet = greenlet.greenlet(background_greenlet)
            background_greenlets.append(glet)
            glet.switch() # Be sure it's active.
            # Control is ours again.
            del glet # Delete one reference from the thread it runs in.
            background_glet_running.set()
            background_glet_killed.wait()
            # To trigger the background collection of the dead greenlet,
            # we need to run some APIs. See issue 252.
            greenlet.getcurrent()


        t = threading.Thread(target=background_thread)
        t.start()
        background_glet_running.wait()

        lists_before = count_lists()

        assert len(background_greenlets) == 1
        self.assertFalse(background_greenlets[0].dead)
        # Delete the last reference to the background greenlet
        # from a different thread. This puts it in the background thread's
        # ts_delkey list.
        del background_greenlets[:]
        background_glet_killed.set()

        # Now wait for the background thread to die.
        t.join(10)

        lists_after = count_lists()
        self.assertEqual(lists_before, lists_after)
