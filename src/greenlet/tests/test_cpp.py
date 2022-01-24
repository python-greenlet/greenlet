from __future__ import print_function
from __future__ import absolute_import

import signal
from multiprocessing import Process

import greenlet
from . import _test_extension_cpp
from . import TestCase

def run_unhandled_exception_in_greenlet_aborts():
    # This is used in multiprocessing.Process and must be picklable
    # so it needs to be global.
    def _():
        _test_extension_cpp.test_exception_switch_and_do_in_g2(
            _test_extension_cpp.test_exception_throw
        )
    g1 = greenlet.greenlet(_)
    g1.switch()

class CPPTests(TestCase):
    def test_exception_switch(self):
        greenlets = []
        for i in range(4):
            g = greenlet.greenlet(_test_extension_cpp.test_exception_switch)
            g.switch(i)
            greenlets.append(g)
        for i, g in enumerate(greenlets):
            self.assertEqual(g.switch(), i)

    def test_unhandled_exception_aborts(self):
        # verify that plain unhandled throw aborts

        # TODO: On some versions of Python with some settings, this
        # spews a lot of garbage to stderr. It would be nice to capture and ignore that.
        p = Process(target=_test_extension_cpp.test_exception_throw)
        p.start()
        p.join(10)
        self.assertEqual(p.exitcode, - signal.SIGABRT)

    def test_unhandled_exception_in_greenlet_aborts(self):
        # verify that unhandled throw called in greenlet aborts too
        # TODO: See test_unhandled_exception_aborts
        p = Process(target=run_unhandled_exception_in_greenlet_aborts)
        p.start()
        p.join(10)
        self.assertEqual(p.exitcode, - signal.SIGABRT)
