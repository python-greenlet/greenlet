#! /usr/bin/env python

import sys, os, getopt, unittest
from distutils.spawn import spawn

build = True
verbosity = 2
here = os.path.dirname(os.path.abspath(__file__))
os.chdir(here)


def bits():
    """determine if running on a 32 bit or 64 bit platform
    just looking at sys.maxint isn't enough
    x64 windows has sys.maxint == (1<<31) - 1
    """

    if sys.version_info[:2] < (2, 5):
        if sys.maxint == 2147483647:  # (1 << 31) - 1
            return 32
        else:
            return 64

    class X(object):
        def __len__(self):
            return 1 << 31
    try:
        len(X())
        return 64
    except OverflowError:
        return 32

# -- parse options
try:
    opts, args = getopt.getopt(sys.argv[1:], "nq")
    if args:
        raise getopt.GetoptError("too many arguments")
except getopt.GetoptError:
    sys.exit("run-tests.py: error: %s" % sys.exc_info()[1])

for o, a in opts:
    if o == "-q":
        verbosity = 0
    elif o == "-n":
        build = False

# -- build greenlet
if build:
    if verbosity == 0:
        cmd = [sys.executable, "setup.py", "-q", "build_ext", "-q"]
    else:
        cmd = [sys.executable, "setup.py", "build_ext"]

    spawn(cmd, search_path=0)


# -- find greenlet but skip the one in "."
if not build:
    oldpath = sys.path[:]
    sys.path.remove(here)

import greenlet

if not build:
    sys.path[:] = oldpath

sys.stdout.write("python %s (%s bit) using greenlet %s from %s\n" %
                 (sys.version.split()[0], bits(), greenlet.__version__, greenlet.__file__))


# -- run tests
from tests import test_collector
suite = test_collector()
unittest.TextTestRunner(verbosity=verbosity).run(suite)
