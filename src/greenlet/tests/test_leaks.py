from __future__ import print_function, absolute_import, division
import unittest
import sys
import gc

import time
import weakref
import threading

import greenlet

from . import Cleanup

class TestLeaks(Cleanup, unittest.TestCase):

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
        t.join(10)

    def test_threaded_leak(self):
        gg = []
        def worker():
            # only main greenlet present
            gg.append(weakref.ref(greenlet.getcurrent()))
        for _ in range(2):
            t = threading.Thread(target=worker)
            t.start()
            t.join(10)
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
            t.join(10)
            del t
        greenlet.getcurrent() # update ts_current
        self.recycle_threads()
        greenlet.getcurrent() # update ts_current
        gc.collect()
        greenlet.getcurrent() # update ts_current
        for g in gg:
            self.assertIsNone(g())

    def test_issue251_killing_cross_thread_leaks_list(self,
                                                      manually_collect_background=True,
                                                      explicit_reference_to_switch=False):
        # See https://github.com/python-greenlet/greenlet/issues/251
        # Killing a greenlet (probably not the main one)
        # in one thread from another thread would
        # result in leaking a list (the ts_delkey list).

        # For the test to be valid, even empty lists have to be tracked by the
        # GC
        assert gc.is_tracked([])

        def count_objects(kind=list):
            # pylint:disable=unidiomatic-typecheck
            # Collect the garbage.
            for _ in range(3):
                gc.collect()
            return sum(
                1
                for x in gc.get_objects()
                if type(x) is kind
            )

        # XXX: The main greenlet of a dead thread is only released
        # when one of the proper greenlet APIs is used from a different
        # running thread. See #252 (https://github.com/python-greenlet/greenlet/issues/252)
        greenlet.getcurrent()
        greenlets_before = count_objects(greenlet.greenlet)

        background_glet_running = threading.Event()
        background_glet_killed = threading.Event()
        background_greenlets = []
        # To toggle debugging off and on.
        #print = lambda *args, **kwargs: None
        class JustDelMe(object):
            EXTANT_INSTANCES = set()
            def __init__(self, msg):
                self.msg = msg
                self.EXTANT_INSTANCES.add(id(self))
            def __del__(self):
                print(id(self), self.msg, file=sys.stderr)
                self.EXTANT_INSTANCES.remove(id(self))
            def __repr__(self):
                return "<JustDelMe at 0x%x %r>" % (
                    id(self), self.msg
                )

        def background_greenlet():
            # Throw control back to the main greenlet.
            jd = JustDelMe("DELETING STACK OBJECT")
            print("\n\tIn bg glet", jd, file=sys.stderr)
            show_bg_main("\t")
            greenlet._greenlet.set_thread_local(
                'test_leaks_key',
                JustDelMe("DELETING THREAD STATE"))
            # Explicitly keeping 'switch' in a local variable
            # breaks this test in all versions
            if explicit_reference_to_switch:
                s = greenlet.getcurrent().parent.switch
                s([jd])
            else:
                greenlet.getcurrent().parent.switch([jd])


        bg_main_wrefs = []
        def show_bg_main(pfx=''):
            print(pfx + "BG Refcount :", sys.getrefcount(bg_main_wrefs[0]()), file=sys.stderr)
            print(pfx + "BG Main Refs:", gc.get_referrers(bg_main_wrefs[0]()), file=sys.stderr)

        def background_thread():
            print(file=sys.stderr)
            print("Begin thread", file=sys.stderr)
            print("BG Main greenlet:", greenlet.getcurrent(), file=sys.stderr)
            print("BG Refcount :", sys.getrefcount(greenlet.getcurrent()), file=sys.stderr)
            print("BG Main Refs:", gc.get_referrers(greenlet.getcurrent()), file=sys.stderr)

            glet = greenlet.greenlet(background_greenlet)
            print("Main:", glet.parent, file=sys.stderr)
            print("Secondary", glet, file=sys.stderr)
            bg_main_wrefs.append(weakref.ref(glet.parent))
            show_bg_main()

            background_greenlets.append(glet)
            glet.switch() # Be sure it's active.
            print("Allowed to finish", glet, file=sys.stderr)
            # Control is ours again.
            print("Before deleting glet", file=sys.stderr)
            show_bg_main("\t")
            del glet # Delete one reference from the thread it runs in.
            print("After deleting glet", file=sys.stderr)
            show_bg_main("\t")
            background_glet_running.set()
            background_glet_killed.wait(10)
            print("End thread", file=sys.stderr)
            show_bg_main()

            # To trigger the background collection of the dead
            # greenlet, thus clearing out the contents of the list, we
            # need to run some APIs. See issue 252.
            if manually_collect_background:
                greenlet.getcurrent()


        t = threading.Thread(target=background_thread)
        t.start()
        background_glet_running.wait(10)
        print("Waited, back in main", file=sys.stderr)
        show_bg_main("\t")
        print("Getting current", file=sys.stderr)
        greenlet.getcurrent()
        show_bg_main("\t")
        lists_before = count_objects()

        assert len(background_greenlets) == 1
        self.assertFalse(background_greenlets[0].dead)
        # Delete the last reference to the background greenlet
        # from a different thread. This puts it in the background thread's
        # ts_delkey list.
        print("\nBefore delete", file=sys.stderr)
        show_bg_main("\t")
        del background_greenlets[:]
        print("Before setting", file=sys.stderr)
        show_bg_main("\t")
        background_glet_killed.set()

        # Now wait for the background thread to die.
        t.join(10)
        del t
        # As part of the fix for 252, we need to cycle the ceval.c
        # interpreter loop to be sure it has had a chance to process
        # the pending call.
        self.wait_for_pending_cleanups()

        if bg_main_wrefs[0]() is not None:
            print("After dead", file=sys.stderr)
            gc.collect()
            show_bg_main()

        #print("checking", file=sys.stderr) Free the background main
        #greenlet by forcing greenlet to notice a difference.
        #greenlet.getcurrent()

        lists_after = count_objects()
        greenlets_after = count_objects(greenlet.greenlet)

        #print("counting", file=sys.stderr)
        #print("counted", file=sys.stderr)
        # On 2.7, we observe that lists_after is smaller than
        # lists_before. No idea what lists got cleaned up. All the
        # Python 3 versions match exactly.
        self.assertLessEqual(lists_after, lists_before)
        # On versions after 3.6, we've successfully cleaned up the greenlet references;
        # prior to that, there is a reference path through the ``greenlet.switch`` method
        # still on the stack that we can't reach to clean up. The C code goes through
        # terrific lengths to clean that up.

        if not explicit_reference_to_switch:
            self.assertEqual(greenlets_after, greenlets_before)
            if manually_collect_background:
                # TODO: Figure out how to make this work!
                # The one on the stack is still leaking somehow
                # in the non-manually-collect state.
                self.assertEqual(JustDelMe.EXTANT_INSTANCES, set())
        else:
            # The explicit reference prevents us from collecting it
            # and it isn't always found by the GC either for some
            # reason. The entire frame is leaked somehow, on some
            # platforms (e.g., MacPorts builds of Python (all
            # versions!)), but not on other platforms (the linux and
            # windows builds on GitHub actions and Appveyor). So we'd
            # like to write a test that proves that the main greenlet
            # sticks around, and we can on may machine (macOS 11.6,
            # MacPorts builds of everything) but we can't write that
            # same test on other platforms
            if greenlets_after < greenlets_before:
                for x in (y for y in gc.get_objects() if isinstance(x, greenlet.greenlet)):
                    print(x)
            pass



    def test_issue251_issue252_need_to_collect_in_background(self):
        # This still fails because the leak of the list
        # still exists when we don't call a greenlet API before exiting the
        # thread. The proximate cause is that neither of the two greenlets
        # from the background thread are actually being destroyed, even though
        # the GC is in fact visiting both objects.
        # It's not clear where that leak is? For some reason the thread-local dict
        # holding it isn't being cleaned up.
        #
        # The leak, I think, is in the CPYthon internal function that calls into
        # green_switch(). The argument tuple is still on the C stack somewhere
        # and can't be reached? That doesn't make sense, because the tuple should be
        # collectable when this object goes away.
        #
        # Note that this test sometimes spuriously passes on Linux, for some reason,
        # but I've never seen it pass on macOS.
        self.test_issue251_killing_cross_thread_leaks_list(manually_collect_background=False)

    def test_issue251_issue252_explicit_reference_not_collectable(self):
        self.test_issue251_killing_cross_thread_leaks_list(
            manually_collect_background=False,
            explicit_reference_to_switch=True)
