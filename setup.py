import os
from setuptools import Extension
from setuptools import setup

setup(
    name="greenlet",
    version='0.3.1',
    description='Lightweight in-process concurrent programming',
    long_description=open(
        os.path.join(os.path.dirname(__file__), 'README'), 'r').read(),
    maintainer="Kyle Ambroff",
    maintainer_email="kyle@ambroff.com",
    url="http://bitbucket.org/ambroff/greenlet",
    repository='http://bitbucket.org/ambroff/greenlet/',
    license="MIT License",
    platforms=['any'],
    test_suite='tests.test_collector',
    headers=['greenlet.h'],
    ext_modules=[Extension(name='greenlet', sources=['greenlet.c'])],
    classifiers=[
        'Intended Audience :: Developers',
        'License :: OSI Approved :: MIT License',
        'Natural Language :: English',
        'Programming Language :: Python',
        'Operating System :: OS Independent',
        'Topic :: Software Development :: Libraries :: Python Modules'])
