#! /usr/bin/env python
from __future__ import print_function
import sys
import os
import glob
import platform

# distutils is deprecated and vendored into setuptools now.
from setuptools import setup
from setuptools import Extension
from setuptools import find_packages

# Extra compiler arguments passed to *all* extensions.
global_compile_args = []

# Extra compiler arguments passed to C++ extensions
cpp_compile_args = []

# Extra linker arguments passed to C++ extensions
cpp_link_args = []

# Extra compiler arguments passed to the main extension
main_compile_args = []

# workaround segfaults on openbsd and RHEL 3 / CentOS 3 . see
# https://bitbucket.org/ambroff/greenlet/issue/11/segfault-on-openbsd-i386
# https://github.com/python-greenlet/greenlet/issues/4
# https://github.com/python-greenlet/greenlet/issues/94
# pylint:disable=too-many-boolean-expressions
is_linux = sys.platform.startswith('linux') # could be linux or linux2
if ((sys.platform == "openbsd4" and os.uname()[-1] == "i386")
    or ("-with-redhat-3." in platform.platform() and platform.machine() == 'i686')
    or (sys.platform == "sunos5" and os.uname()[-1] == "sun4v")
    or ("SunOS" in platform.platform() and platform.machine() == "sun4v")
    or (is_linux and platform.machine() == "ppc")):
    global_compile_args.append("-Os")


if sys.platform == 'darwin':
    # The clang compiler doesn't use --std=c++11 by default
    cpp_compile_args.append("--std=gnu++11")
elif sys.platform == 'win32' and "MSC" in platform.python_compiler():
    # Older versions of MSVC (Python 2.7) don't handle C++ exceptions
    # correctly by default. While newer versions do handle exceptions by default,
    # they don't do it fully correctly. So we need an argument on all versions.
    #"/EH" == exception handling.
    #    "s" == standard C++,
    #    "c" == extern C functions don't throw
    # OR
    #   "a" == standard C++, and Windows SEH; anything may throw, compiler optimizations
    #          around try blocks are less aggressive.
    # /EHsc is suggested, as /EHa isn't supposed to be linked to other things not built
    # with it.
    # See https://docs.microsoft.com/en-us/cpp/build/reference/eh-exception-handling-model?view=msvc-160
    handler = "/EHsc"
    cpp_compile_args.append(handler)
    # To disable most optimizations:
    #cpp_compile_args.append('/Od')

    # To enable assertions:
    #cpp_compile_args.append('/UNDEBUG')

    # To enable more compile-time warnings (/Wall produces a mountain of output).
    #cpp_compile_args.append('/W4')

    # To link with the debug C runtime...except we can't because we need
    # the Python debug lib too, and they're not around by default
    # cpp_compile_args.append('/MDd')

    # Support fiber-safe thread-local storage: "the compiler mustn't
    # cache the address of the TLS array, or optimize it as a common
    # subexpression across a function call." This would probably solve
    # some of the issues we had with MSVC caching the thread local
    # variables on the stack, leading to having to split some
    # functions up. Revisit those.
    cpp_compile_args.append("/GT")

def readfile(filename):
    with open(filename, 'r') as f: # pylint:disable=unspecified-encoding
        return f.read()

GREENLET_SRC_DIR = 'src/greenlet/'
GREENLET_HEADER_DIR = GREENLET_SRC_DIR
GREENLET_HEADER = GREENLET_HEADER_DIR + 'greenlet.h'
GREENLET_TEST_DIR = 'src/greenlet/tests/'
# The location of the platform specific assembly files
# for switching.
GREENLET_PLATFORM_DIR = GREENLET_SRC_DIR + 'platform/'

def _find_platform_headers():
    return glob.glob(GREENLET_PLATFORM_DIR + "switch_*.h")

def _find_impl_headers():
    return glob.glob(GREENLET_SRC_DIR + "*.hpp")

if hasattr(sys, "pypy_version_info"):
    ext_modules = []
    headers = []
else:

    headers = [GREENLET_HEADER]

    if sys.platform == 'win32' and '64 bit' in sys.version:
        # this works when building with msvc, not with 64 bit gcc
        # switch_<platform>_masm.obj can be created with setup_switch_<platform>_masm.cmd
        obj_fn = 'switch_arm64_masm.obj' if platform.machine() == 'ARM64' else 'switch_x64_masm.obj'
        extra_objects = [os.path.join(GREENLET_PLATFORM_DIR, obj_fn)]
    else:
        extra_objects = []

    if sys.platform == 'win32' and os.environ.get('GREENLET_STATIC_RUNTIME') in ('1', 'yes'):
        main_compile_args.append('/MT')
    elif hasattr(os, 'uname') and os.uname()[4] in ['ppc64el', 'ppc64le']:
        main_compile_args.append('-fno-tree-dominator-opts')

    ext_modules = [
        Extension(
            name='greenlet._greenlet',
            sources=[
                GREENLET_SRC_DIR + 'greenlet.cpp',
            ],
            language='c++',
            extra_objects=extra_objects,
            extra_compile_args=global_compile_args + main_compile_args + cpp_compile_args,
            extra_link_args=cpp_link_args,
            depends=[
                GREENLET_HEADER,
                GREENLET_SRC_DIR + 'slp_platformselect.h',
            ] + _find_platform_headers() + _find_impl_headers()
        ),
        # Test extensions.
        #
        # We used to try hard to not include these in built
        # distributions, because we only distributed ``greenlet.so``.
        # That's really not important, now we have a clean layout with
        # the test directory nested inside a greenlet directory. See
        # https://github.com/python-greenlet/greenlet/issues/184 and
        # 189
        Extension(
            name='greenlet.tests._test_extension',
            sources=[GREENLET_TEST_DIR + '_test_extension.c'],
            include_dirs=[GREENLET_HEADER_DIR],
            extra_compile_args=global_compile_args,
        ),
        Extension(
            name='greenlet.tests._test_extension_cpp',
            sources=[GREENLET_TEST_DIR + '_test_extension_cpp.cpp'],
            language="c++",
            include_dirs=[GREENLET_HEADER_DIR],
            extra_compile_args=global_compile_args + cpp_compile_args,
            extra_link_args=cpp_link_args,
        ),
    ]


def get_greenlet_version():
    with open('src/greenlet/__init__.py') as f: # pylint:disable=unspecified-encoding
        looking_for = '__version__ = \''
        for line in f:
            if line.startswith(looking_for):
                version = line[len(looking_for):-2]
                return version
    raise ValueError("Unable to find version")


setup(
    name="greenlet",
    version=get_greenlet_version(),
    description='Lightweight in-process concurrent programming',
    long_description=readfile("README.rst"),
    long_description_content_type="text/x-rst",
    url="https://greenlet.readthedocs.io/",
    keywords="greenlet coroutine concurrency threads cooperative",
    author="Alexey Borzenkov",
    author_email="snaury@gmail.com",
    maintainer='Jason Madden',
    maintainer_email='jason@nextthought.com',
    project_urls={
        'Bug Tracker': 'https://github.com/python-greenlet/greenlet/issues',
        'Source Code': 'https://github.com/python-greenlet/greenlet/',
        'Documentation': 'https://greenlet.readthedocs.io/',
    },
    license="MIT License",
    platforms=['any'],
    package_dir={'': 'src'},
    packages=find_packages('src'),
    include_package_data=True,
    headers=headers,
    ext_modules=ext_modules,
    classifiers=[
        "Development Status :: 5 - Production/Stable",
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Natural Language :: English',
        'Programming Language :: C',
        'Programming Language :: Python',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Operating System :: OS Independent',
        'Topic :: Software Development :: Libraries :: Python Modules'
    ],
    extras_require={
        'docs': [
            'Sphinx',
            # 0.18b1 breaks sphinx 1.8.5 which is the latest version that runs
            # on Python 2. The version pin sphinx itself contains isn't specific enough.
            'docutils < 0.18; python_version < "3"',
        ],
        'test': [
            'objgraph',
             'faulthandler; python_version == "2.7" and platform_python_implementation == "CPython"',
        ],
    },
    python_requires=">=2.7,!=3.0.*,!=3.1.*,!=3.2.*,!=3.3.*,!=3.4.*",
    zip_safe=False,
)
