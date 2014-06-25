import os
import sys
import glob
import unittest
from distutils.core import setup, Extension
from distutils.command.build_ext import build_ext

TEST_EXTENSIONS = [
    Extension('_test_extension',
              [os.path.join('tests', '_test_extension.c')],
              include_dirs=[os.path.curdir]),
]

if os.environ.get('GREENLET_TEST_CPP', 'yes').lower() not in ('0', 'no', 'false'):
    TEST_EXTENSIONS_CPP = [
        Extension('_test_extension_cpp',
                  [os.path.join('tests', '_test_extension_cpp.cpp')],
                  language="c++",
                  include_dirs=[os.path.curdir]),
    ]
else:
    TEST_EXTENSIONS_CPP = []


def test_collector(build_base=None):
    """Collect all tests under the tests directory and return a
    unittest.TestSuite
    """
    build_test_extensions(build_base)
    tests_dir = os.path.realpath(os.path.dirname(__file__))
    test_module_list = [
        'tests.%s' % os.path.splitext(os.path.basename(t))[0]
        for t in glob.glob(os.path.join(tests_dir, 'test_*.py'))]
    if not TEST_EXTENSIONS_CPP:
        test_module_list.remove('tests.test_cpp')
    return unittest.TestLoader().loadTestsFromNames(test_module_list)


class build_test_ext(build_ext):
    """Command for building test extensions

    Forces a non-inplace build and prepends library directory to sys.path.
    """
    def build_extension(self, ext):
        self.inplace = 0
        build_ext.build_extension(self, ext)
        build_lib = os.path.abspath(self.build_lib)
        if build_lib not in sys.path:
            if self.verbose:
                sys.stderr.write('Adding %s to sys.path\n' % (build_lib,))
            sys.path.insert(0, build_lib)


def build_test_extensions(build_base=None):
    """Because distutils sucks, it just copies the entire contents of the build
    results dir (e.g. build/lib.linux-i686-2.6) during installation. That means
    that we can't put any files there that we don't want to distribute.

    To deal with it, this code will compile test extensions in a separate
    directory, prepending it to sys.path afterwards. This way testing with
    multiple Python release and pydebug versions works and test extensions
    are not distributed.
    """
    if build_base is None:
        build_base = os.path.join('build', 'tests')
    setup(
        options={
            'build': {'build_base': build_base},
        },
        cmdclass=dict(build_ext=build_test_ext),
        script_args=['-q', 'build_ext', '-q'],
        ext_modules=TEST_EXTENSIONS + TEST_EXTENSIONS_CPP)
