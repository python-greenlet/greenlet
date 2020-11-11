#! /usr/bin/env python

import sys
import os
import glob
import platform

# distutils is deprecated and vendored into setuptools now.
from setuptools import setup
from setuptools import Extension

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

    if sys.platform == 'win32' and os.environ.get('GREENLET_STATIC_RUNTIME') in ('1', 'yes'):
        extra_compile_args = ['/MT']
    elif hasattr(os, 'uname') and os.uname()[4] in ['ppc64el', 'ppc64le']:
        extra_compile_args = ['-fno-tree-dominator-opts']
    else:
        extra_compile_args = []

    ext_modules = [
        Extension(
            name='greenlet',
            sources=['greenlet.c'],
            extra_objects=extra_objects,
            extra_compile_args=extra_compile_args,
            depends=['greenlet.h', 'slp_platformselect.h'] + _find_platform_headers()
        ),
        # Test extensions.
        # XXX: We used to try hard to not include these in built
        # distributions. That's really not important, at least not once we have a clean
        # layout with the test directory nested inside a greenlet directory.
        # See https://github.com/python-greenlet/greenlet/issues/184 and 189
        Extension(
            '_test_extension',
            [os.path.join('tests', '_test_extension.c')],
            include_dirs=[os.path.curdir]
        ),
    ]

    if os.environ.get('GREENLET_TEST_CPP', 'yes').lower() not in ('0', 'no', 'false'):
        ext_modules.append(
            Extension(
                '_test_extension_cpp',
                [os.path.join('tests', '_test_extension_cpp.cpp')],
                language="c++",
                include_dirs=[os.path.curdir]),
        )

setup(
    name="greenlet",
    version='1.0.0.dev0',
    description='Lightweight in-process concurrent programming',
    long_description=readfile("README.rst"),
    url="https://github.com/python-greenlet/greenlet",
    project_urls={
        'Bug Tracker': 'https://github.com/python-greenlet/greenlet/issues',
        'Source Code': 'https://github.com/python-greenlet/gevent/',
        'Documentation': 'https://greenlet.readthedocs.io/',
    },
    license="MIT License",
    platforms=['any'],
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
        ]
    },
    python_requires=">=2.7,!=3.0.*,!=3.1.*,!=3.2.*,!=3.3.*,!=3.4.*",
    # XXX: This is deprecated. appveyor.yml still uses it though.
    test_suite='tests.test_collector',
    zip_safe=False,
)
