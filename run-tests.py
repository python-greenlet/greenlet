#! /usr/bin/env python
from __future__ import print_function

import sys
import os
import struct
import unittest


verbosity = 2
here = os.path.dirname(os.path.abspath(__file__))
os.chdir(here)


def bits():
    """determine if running on a 32 bit or 64 bit platform
    """
    return struct.calcsize("P") * 8



# -- find greenlet but skip the one in "."
oldpath = sys.path[:]
sys.path.remove(here)

import greenlet

sys.path[:] = oldpath

print("python %s (%s bit) using greenlet %s from %s\n" %
      (sys.version.split()[0], bits(), greenlet.__version__, greenlet.__file__))

# -- run tests
from tests import test_collector
result = unittest.TextTestRunner(verbosity=verbosity).run(test_collector())
if result.failures or result.errors:
    sys.exit(1)
