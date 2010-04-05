import unittest
import sys

import greenlet

class ArgRefcountTests(unittest.TestCase):
    def test_arg_refs(self):
        args = ('a', 'b', 'c')
        g = greenlet.greenlet(
            lambda *args: greenlet.getcurrent().parent.switch(*args))
        for i in range(100):
            g.switch(*args)
        self.assertEquals(sys.getrefcount(args), 3)

    def test_kwarg_refs(self):
        kwargs = {}
        g = greenlet.greenlet(
            lambda **kwargs: greenlet.getcurrent().parent.switch(**kwargs))
        for i in range(100):
            g.switch(**kwargs)
        self.assertEquals(sys.getrefcount(kwargs), 2)
