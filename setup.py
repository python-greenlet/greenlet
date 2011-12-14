#! /usr/bin/env python

import os
try:
    from setuptools import setup, Extension
    setuptools_args = dict(test_suite='tests.test_collector')
except ImportError:
    from distutils.core import setup, Extension
    setuptools_args = dict()

def _find_platform_headers():
    return [os.path.join('platform', name)
            for name in os.listdir('platform')
            if name.startswith('switch_') and name.endswith('.h')]

extension = Extension(
    name='greenlet',
    sources=['greenlet.c'],
    depends=['greenlet.h', 'slp_platformselect.h'] + _find_platform_headers())

setup(
    name="greenlet",
    version='0.3.2',
    description='Lightweight in-process concurrent programming',
    long_description=open(
        os.path.join(os.path.dirname(__file__), 'README'), 'r').read(),
    maintainer="Kyle Ambroff",
    maintainer_email="kyle@ambroff.com",
    url="http://bitbucket.org/ambroff/greenlet",
    license="MIT License",
    platforms=['any'],
    headers=['greenlet.h'],
    ext_modules=[extension],
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
