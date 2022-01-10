from __future__ import print_function
from __future__ import absolute_import

import unittest
import os
import signal

import greenlet
from . import _test_extension_cpp
from . import TestCase

class CPPTests(TestCase):
    def test_exception_switch(self):
        greenlets = []
        for i in range(4):
            g = greenlet.greenlet(_test_extension_cpp.test_exception_switch)
            g.switch(i)
            greenlets.append(g)
        for i, g in enumerate(greenlets):
            self.assertEqual(g.switch(), i)

    @unittest.skipUnless(hasattr(os, 'fork'),
        "test should abort when run -> need to run it in separate process")
    def test_exception_switch_and_throw(self):
        # procrun runs f in separate process
        def procrun(f): # -> (ret, sig, coredumped)
            pid = os.fork()

            # child
            if pid == 0:
                f()
                os.exit(0)

            # parent
            _, st = os.waitpid(pid, 0)
            ret = st >> 8
            sig = st & 0x7f
            coredumped = ((st & 0x80) != 0)
            return (ret, sig, coredumped)


        # verify that plain unhandled throw aborts
        # (unhandled throw -> std::terminate -> abort)
        ret, sig, _ = procrun(_test_extension_cpp.test_exception_throw)
        if not (ret == 0 and sig == signal.SIGABRT):
            self.fail("unhandled throw -> ret=%d sig=%d  ; expected 0/SIGABRT" % (ret, sig))


        # verify that unhandled throw called in greenlet aborts too
        # (does not segfaults nor is handled by try/catch on preceeding greenlet C stack)
        def _():
            def _():
                _test_extension_cpp.test_exception_switch_and_do_in_g2(
                    _test_extension_cpp.test_exception_throw
                )
            g1 = greenlet.greenlet(_)
            g1.switch()

        ret, sig, core = procrun(_)
        if not (ret == 0 and sig == signal.SIGABRT):
            self.fail("failed with ret=%d sig=%d%s" %
                      (ret, sig, " (core dumped)" if core else ""))
