#! /usr/bin/env python

import sys, os, re, unittest, greenlet


class VersionTests(unittest.TestCase):
    def test_version(self):
        upfile = lambda p: os.path.join(os.path.dirname(__file__), "..", p)
        hversion, = re.findall('GREENLET_VERSION "(.*)"', open(upfile("greenlet.h")).read())
        sversion = os.popen("%s %s --version" % (sys.executable, upfile("setup.py"))).read().strip()
        self.failIf(sversion != hversion)
        self.failIf(sversion != greenlet.__version__)
