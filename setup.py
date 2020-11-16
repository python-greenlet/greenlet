#! /usr/bin/env python

import sys
import os
import glob
import platform

# distutils is deprecated and vendored into setuptools now.
from setuptools import setup
from setuptools import Extension
from setuptools import find_packages

# workaround segfaults on openbsd and RHEL 3 / CentOS 3 . see
# https://bitbucket.org/ambroff/greenlet/issue/11/segfault-on-openbsd-i386
# https://github.com/python-greenlet/greenlet/issues/4
# https://github.com/python-greenlet/greenlet/issues/94
# pylint:disable=too-many-boolean-expressions
if ((sys.platform == "openbsd4" and os.uname()[-1] == "i386")
    or ("-with-redhat-3." in platform.platform() and platform.machine() == 'i686')
    or (sys.platform == "sunos5" and os.uname()[-1] == "sun4v")
    or ("SunOS" in platform.platform() and platform.machine() == "sun4v")
    or (sys.platform == "linux" and platform.machine() == "ppc")):
    os.environ["CFLAGS"] = ("%s %s" % (os.environ.get("CFLAGS", ""), "-Os")).lstrip()


def readfile(filename):
    with open(filename, 'r') as f:
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

if hasattr(sys, "pypy_version_info"):
    ext_modules = []
    headers = []
else:

    headers = [GREENLET_HEADER]

    if sys.platform == 'win32' and '64 bit' in sys.version:
        # this works when building with msvc, not with 64 bit gcc
        # switch_x64_masm.obj can be created with setup_switch_x64_masm.cmd
        extra_objects = [GREENLET_PLATFORM_DIR + 'switch_x64_masm.obj']
    else:
        extra_objects = []

    if sys.platform == 'win32' and os.environ.get('GREENLET_STATIC_RUNTIME') in ('1', 'yes'):
        extra_compile_args = ['/MT']
    elif hasattr(os, 'uname') and os.uname()[4] in ['ppc64el', 'ppc64le']:
        extra_compile_args = ['-fno-tree-dominator-opts']
    else:
        extra_compile_args = []

    ext_modules = [
        Extension(
            name='greenlet._greenlet',
            sources=[GREENLET_SRC_DIR + 'greenlet.c'],
            extra_objects=extra_objects,
            extra_compile_args=extra_compile_args,
            depends=[
                GREENLET_HEADER,
                GREENLET_SRC_DIR + 'slp_platformselect.h',
            ] + _find_platform_headers()
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
            include_dirs=[GREENLET_HEADER_DIR]
        ),
    ]

    if os.environ.get('GREENLET_TEST_CPP', 'yes').lower() not in ('0', 'no', 'false'):
        ext_modules.append(
            Extension(
                name='greenlet.tests._test_extension_cpp',
                sources=[GREENLET_TEST_DIR + '_test_extension_cpp.cpp'],
                language="c++",
                include_dirs=[GREENLET_HEADER_DIR]),
        )


def get_greenlet_version():
    with open('src/greenlet/__init__.py') as f:
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
    project_urls={
        'Bug Tracker': 'https://github.com/python-greenlet/greenlet/issues',
        'Source Code': 'https://github.com/python-greenlet/gevent/',
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
        'Operating System :: OS Independent',
        'Topic :: Software Development :: Libraries :: Python Modules'
    ],
    extras_require={
        'docs': [
            'Sphinx',
        ],
        'test': [
        ],
    },
    python_requires=">=2.7,!=3.0.*,!=3.1.*,!=3.2.*,!=3.3.*,!=3.4.*",
    zip_safe=False,
)
