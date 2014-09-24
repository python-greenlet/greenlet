#! /usr/bin/env python

import sys, os, getopt, struct, unittest
from distutils.spawn import spawn

build = True
verbosity = 2
build_base = None
here = os.path.dirname(os.path.abspath(__file__))
os.chdir(here)


def bits():
    """determine if running on a 32 bit or 64 bit platform
    """
    return struct.calcsize("P") * 8

# -- parse options
try:
    opts, args = getopt.getopt(sys.argv[1:], "nqb:")
    if args:
        raise getopt.GetoptError("too many arguments")
except getopt.GetoptError:
    sys.exit("run-tests.py: error: %s" % sys.exc_info()[1])

for o, a in opts:
    if o == "-q":
        verbosity = 0
    elif o == "-n":
        build = False
    elif o == "-b":
        build_base = a

# -- build greenlet
if build:
    if verbosity == 0:
        cmd = [sys.executable, "setup.py", "-q", "build_ext", "-q", "-i"]
    else:
        cmd = [sys.executable, "setup.py", "build_ext", "-i"]

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
sys.stdout.flush()


# -- run tests
from tests import test_collector
result = unittest.TextTestRunner(verbosity=verbosity).run(test_collector(build_base))
if result.failures or result.errors:
    sys.exit(1)
