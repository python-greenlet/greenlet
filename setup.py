#!/usr/bin/env python

from setuptools import Extension
from setuptools import setup
from setuptools.command.build_ext import build_ext

VERSION = '0.3'
DESCRIPTION = 'Lightweight in-process concurrent programming'
LONG_DESCRIPTION = """
(This is the py.magic.greenlet module from the py lib
<http://codespeak.net/py/>)

The "greenlet" package is a spin-off of Stackless, a version of CPython that
supports micro-threads called "tasklets".  Tasklets run pseudo-concurrently
(typically in a single or a few OS-level threads) and are synchronized with
data exchanges on "channels".

A "greenlet", on the other hand, is a still more primitive notion of
micro-thread with no implicit scheduling; coroutines, in other words.
This is useful when you want to control exactly when your code runs.
You can build custom scheduled micro-threads on top of greenlet; however, it
seems that greenlets are useful on their own as a way to make advanced control
flow structures.  For example, we can recreate generators; the difference with
Python's own generators is that our generators can call nested functions and
the nested functions can yield values too.  Additionally, you don't need a
"yield" keyword. See the example in test_generator.py.

Greenlets are provided as a C extension module for the regular unmodified
interpreter.
"""

CLASSIFIERS = filter(None, map(str.strip,
"""                 
Intended Audience :: Developers
License :: OSI Approved :: MIT License
Natural Language :: English
Programming Language :: Python
Operating System :: OS Independent
Topic :: Software Development :: Libraries :: Python Modules
""".splitlines()))

REPOSITORY="http://bitbucket.org/ambroff/greenlet/"

# Modules that are distributed.
EXT_MODULES = [
    Extension(name='greenlet', sources=['greenlet.c']),]

# These modules are built only for testing and are not distributed.
TEST_EXT_MODULES = [
    Extension(name='_test_extension', sources=['tests/_test_extension.c']),]

class custom_build_ext(build_ext):
    """HACK: Need to build a C Extension that isn't distruted. This is used for
    testing the C API."""
    def finalize_options(self):
        backup_extensions = None
        try:
            backup_ext_modules = self.distribution.ext_modules
            self.distribution.ext_modules = self.distribution.ext_modules + \
                                            TEST_EXT_MODULES
            build_ext.finalize_options(self)
        finally:
            if not backup_extensions is None:
                self.distribution.ext_modules = backup_ext_modules

setup(
    name="greenlet",
    version=VERSION,
    description=DESCRIPTION,
    long_description=LONG_DESCRIPTION,
    classifiers=CLASSIFIERS,
    maintainer="Kyle Ambroff",
    maintainer_email="kyle@ambroff.com",
    url="http://bitbucket.org/ambroff/greenlet",
    license="MIT License",
    platforms=['any'],
    test_suite='nose.collector',
    headers=['greenlet.h'],
    cmdclass={'build_ext': custom_build_ext},
    ext_modules=EXT_MODULES)
