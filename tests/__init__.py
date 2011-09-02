import os
import glob
import unittest

try:
    unittest.TestCase.assertTrue
    unittest.TestCase.assertFalse
except AttributeError:
    # monkey patch for Python 2.3 compatibility
    unittest.TestCase.assertTrue = unittest.TestCase.failUnless
    unittest.TestCase.assertFalse = unittest.TestCase.failIf

from distutils.core import setup
from distutils.core import Extension

TEST_EXTENSIONS = [
    Extension('_test_extension',
              [os.path.join('tests', '_test_extension.c')],
              include_dirs=[os.path.curdir]),
]


def test_collector():
    """Collect all tests under the tests directory and return a
    unittest.TestSuite
    """
    build_test_extensions()
    tests_dir = os.path.realpath(os.path.dirname(__file__))
    test_module_list = [
        'tests.%s' % os.path.splitext(os.path.basename(t))[0]
        for t in glob.glob(os.path.join(tests_dir, 'test_*.py'))]
    return unittest.TestLoader().loadTestsFromNames(test_module_list)

def build_test_extensions():
    """Because distutils sucks, it just copies the entire contents of the build
    results dir (e.g. build/lib.linux-i686-2.6) during installation. That means
    that we can't put any files there that we don't want to distribute.

    To deal with it, this code will compile test extensions inplace, but
    will use a separate directory for build files. This way testing with
    multiple Python release and pydebug versions works and test extensions
    are not distributed.
    """
    setup(
        options={
            'build': {'build_base': os.path.join('build', 'tests')},
        },
        script_args=['build_ext', '-i'],
        ext_modules=TEST_EXTENSIONS)
