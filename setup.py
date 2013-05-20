#! /usr/bin/env python

import sys, os, glob, platform, tempfile, shutil

# workaround segfaults on openbsd and RHEL 3 / CentOS 3 . see
# https://bitbucket.org/ambroff/greenlet/issue/11/segfault-on-openbsd-i386
# https://github.com/python-greenlet/greenlet/issues/4
if ((sys.platform == "openbsd4" and os.uname()[-1] == "i386")
    or ("-with-redhat-3." in platform.platform() and platform.machine() == 'i686')
    or (sys.platform == "sunos5" and os.uname()[-1] == "sun4v")
    or ("SunOS" in platform.platform() and platform.machine() == "sun4v")):
    os.environ["CFLAGS"] = ("%s %s" % (os.environ.get("CFLAGS", ""), "-Os")).lstrip()

try:
    if not (sys.modules.get("setuptools")
            or "develop" in sys.argv
            or "upload" in sys.argv
            or "fixup" in sys.argv
            or "bdist_egg" in sys.argv
            or "test" in sys.argv):
        raise ImportError()
    from setuptools import setup, Extension
    setuptools_args = dict(test_suite='tests.test_collector', zip_safe=False)
except ImportError:
    from distutils.core import setup, Extension
    setuptools_args = dict()

def readfile(filename):
    f = open(filename)
    try:
        return f.read()
    finally:
        f.close()

def _find_platform_headers():
    return glob.glob("platform/switch_*.h")

if hasattr(sys, "pypy_version_info"):
    ext_modules = []
    headers = []
else:
    headers = ['greenlet.h']

    if sys.platform == 'win32' and '64 bit' in sys.version:
        # this works when building with msvc, not with 64 bit gcc
        # switch_x64_masm.obj can be created with setup_switch_x64_masm.cmd
        extra_objects = ['platform/switch_x64_masm.obj']
    else:
        extra_objects = []

    ext_modules = [Extension(
        name='greenlet',
        sources=['greenlet.c'],
        extra_objects=extra_objects,
        depends=['greenlet.h', 'slp_platformselect.h'] + _find_platform_headers())]

from my_build_ext import build_ext as _build_ext
from distutils.core import Command


class build_ext(_build_ext):
    def configure_compiler(self):
        compiler = self.compiler
        if compiler.__class__.__name__ != "UnixCCompiler":
            return

        compiler.compiler_so += ["-fno-tree-dominator-opts"]
        tmpdir = tempfile.mkdtemp()

        try:
            simple_c = os.path.join(tmpdir, "simple.c")
            open(simple_c, "w").write("void foo(){}")
            compiler.compile([simple_c], output_dir=tmpdir)
        except Exception:
            del compiler.compiler_so[-1]

        shutil.rmtree(tmpdir)

    def build_extensions(self):
        self.configure_compiler()
        _build_ext.build_extensions(self)


class fixup(Command):
    user_options = []
    description = "prevent duplicate uploads and upload for the wrong architecture"

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        del self.distribution.dist_files[:-1]

setup(
    name="greenlet",
    version='0.4.1',
    description='Lightweight in-process concurrent programming',
    long_description=readfile("README.rst"),
    maintainer="Ralf Schmitt",
    maintainer_email="ralf@systemexit.de",
    url="https://github.com/python-greenlet/greenlet",
    license="MIT License",
    platforms=['any'],
    headers=headers,
    ext_modules=ext_modules,
    cmdclass=dict(build_ext=build_ext, fixup=fixup),
    classifiers=[
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Natural Language :: English',
        'Programming Language :: C',
        'Programming Language :: Python',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.4',
        'Programming Language :: Python :: 2.5',
        'Programming Language :: Python :: 2.6',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.0',
        'Programming Language :: Python :: 3.1',
        'Programming Language :: Python :: 3.2',
        'Operating System :: OS Independent',
        'Topic :: Software Development :: Libraries :: Python Modules'],
    **setuptools_args)
