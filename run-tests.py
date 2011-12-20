#! /usr/bin/env python

import sys, os, getopt, unittest
from distutils.spawn import spawn

build = True
verbosity = 2


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
    sys.path.remove(os.path.dirname(os.path.abspath(__file__)))

import greenlet

if not build:
    sys.path[:] = oldpath

sys.stdout.write("python %s using greenlet %s from %s\n" %
                 (sys.version.split()[0], greenlet.__version__, greenlet.__file__))


# -- run tests
from tests import test_collector
suite = test_collector()
unittest.TextTestRunner(verbosity=verbosity).run(suite)
