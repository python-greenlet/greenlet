=====================
 Development Process
=====================

This document is a collection of notes about how greenlet is
developed.

Github
======

The primary development location for greenlet is GitHub:
https://github.com/python-greenlet/greenlet/

Releases
========

greenlet uses Semantic Versions; this includes changes to the ABI
(breaking the ABI is considered a major change).

Releases are made using `zest.releaser
<https://zestreleaser.readthedocs.io/en/latest/>`_.

.. code-block:: shell

   $ pip install zest.releaser[recommended]
   $ fullrelease

Binary wheels are created and uploaded to PyPI for Windows, macOS, and
Linux (x86_64 and aarch64) when a tag is pushed to the repository.
The above command does this.
