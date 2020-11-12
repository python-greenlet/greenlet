#! /usr/bin/env python
from __future__ import absolute_import
from __future__ import print_function

import sys
import os
import re
import unittest

import greenlet

class VersionTests(unittest.TestCase):
    def test_version(self):
        def find_dominating_file(name):
            here = os.path.abspath(os.path.dirname(__file__))
            for i in range(10):
                up = ['..'] * i
                path = [here] + up + [name]
                fname = os.path.join(*path)
                fname = os.path.abspath(fname)
                if os.path.exists(fname):
                    return fname
            raise AssertionError("Could not find file " + name + "; last checked " + fname)

        try:
            setup_py = find_dominating_file('setup.py')
        except AssertionError as e:
            raise unittest.SkipTest("Unable to find setup.py; must be out of tree. " + str(e))

        try:
            greenlet_h = find_dominating_file('greenlet.h')
        except AssertionError as e:
            if '.tox' in os.path.abspath(os.path.dirname(__file__)):
                raise unittest.SkipTest("Unable to find greenlet.h while running in tox")
            raise

        with open(greenlet_h) as f:
            greenlet_h = f.read()

        hversion, = re.findall('GREENLET_VERSION "(.*)"', greenlet_h)



        invoke_setup = "%s %s --version" % (sys.executable, setup_py)
        with os.popen(invoke_setup) as f:
            sversion = f.read().strip()

        self.assertEqual(sversion, hversion)
        self.assertEqual(sversion, greenlet.__version__)
