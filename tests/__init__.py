import glob
import os
import unittest
import sys

from distutils import dist
from distutils import sysconfig
from distutils import file_util
from distutils import util
from distutils.core import Extension
from distutils.ccompiler import new_compiler
from distutils.command import build_ext

TEST_EXTENSIONS = [
    Extension('_test_extension',
              [os.path.join('tests', '_test_extension.c')],
              include_dirs=[os.path.curdir, sysconfig.get_python_inc()]),
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
    test_list = unittest.TestLoader().loadTestsFromNames(test_module_list)
    suite = unittest.TestSuite()
    suite.addTests(test_list)
    return suite

def build_test_extensions():
    """Because distutils sucks, it just copies the entire contents of the build
    results dir (e.g. build/lib.linux-i686-2.6) during installation. That means
    that we can't put any files there that we don't want to distribute. </3.

    To deal with that, this code will compile the test extension and place the
    object files in the normal temp directory using the same logic as distutils,
    but linked shared library will go directly into the tests directory.
    """
    build_temp_dir = os.path.join(
        'build', 'temp.%s-%s' % (util.get_platform(), sys.version[0:3]))
    compiler = new_compiler()
    distribution = dist.Distribution()
    build_ext_cmd = build_ext.build_ext(distribution)
    build_ext_cmd.finalize_options()
    compiler.set_library_dirs(build_ext_cmd.library_dirs)
    sysconfig.customize_compiler(compiler)
    def build_and_copy(extension):
        """compile sources, link shared library, and copy it into CWD"""
        objects = compiler.compile(
            extension.sources,
            output_dir=build_temp_dir,
            include_dirs=extension.include_dirs,
            debug=False,
            depends=extension.depends)
        output_file = os.path.join(
            build_temp_dir, build_ext_cmd.get_ext_filename(extension.name))
        compiler.link_shared_object(
            objects,
            output_file,
            libraries=build_ext_cmd.get_libraries(extension),
            library_dirs=extension.library_dirs,
            runtime_library_dirs=extension.runtime_library_dirs,
            export_symbols=build_ext_cmd.get_export_symbols(extension),
            debug=False,
            build_temp=build_temp_dir,
            target_lang=compiler.detect_language(extension.sources))
        file_util.copy_file(output_file, os.path.curdir)
    for extension in TEST_EXTENSIONS:
        build_and_copy(extension)
