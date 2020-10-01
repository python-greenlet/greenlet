#! /usr/bin/env python

import sys
import os
import glob
import platform

# distutils is deprecated and vendored into setuptools now.
from setuptools import setup
from setuptools import Extension

# XXX: This uses distutils directly and is probably
# unnecessary with setuptools.
from my_build_ext import build_ext

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
    # The with statement is from Python 2.6, meaning we definitely no longer
    # support 2.4 or 2.5. I strongly suspect that's not a problem; we'll be dropping
    # our claim to support them very soon anyway.
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

    ext_modules = [Extension(
        name='greenlet',
        sources=['greenlet.c'],
        extra_objects=extra_objects,
        extra_compile_args=extra_compile_args,
        depends=['greenlet.h', 'slp_platformselect.h'] + _find_platform_headers())]

setup(
    name="greenlet",
    version='0.4.17',
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
    cmdclass=dict(build_ext=build_ext),
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
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
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
    # XXX: This is deprecated. appveyor.yml still uses it though.
    test_suite='tests.test_collector',
    zip_safe=False,
)
