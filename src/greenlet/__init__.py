# -*- coding: utf-8 -*-
"""
The root of the greenlet package.
"""
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

# TODO: Define a correct __all__
# pylint:disable=unused-import
# TODO: Move the definition of __version__ here, instead of the
# C code. zest.releaser will find it here, but not in C.

from ._greenlet import * # pylint:disable=wildcard-import
from ._greenlet import __version__
from ._greenlet import _C_API # pylint:disable=no-name-in-module
