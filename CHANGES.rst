=========
 Changes
=========

3.1.1 (2024-09-20)
==================

- Fix crashes on 32-bit PPC Linux. Note that there is no CI for this,
  and support is best effort; there may be other issues lurking.
  See `issue 422
  <https://github.com/python-greenlet/greenlet/issues/422>`_.
- Remove unnecessary logging sometimes during interpreter shutdown.
  See `issue 426 <https://github.com/python-greenlet/greenlet/issues/426>`_.
- Fix some crashes on 32-bit PPC MacOS. This is a very old platform,
  and is only known to be tested on beta versions of an operating
  system that was never released, using the GCC 14 only provided by
  MacPorts; it may or may not work on the final MacOS X release that
  supported 32-bit PowerPC. It has the known issue of leaking memory
  when greenlets are used in multiple threads. Help debugging this
  would be appreciated. See `PR 419
  <https://github.com/python-greenlet/greenlet/pull/419>`_.

3.1.0 (2024-09-10)
==================

.. note::

    This will be the last release to support Python 3.7 and 3.8.

- Adds support for Python 3.13.

.. note::

   greenlet will not work in no-gil (free threaded) builds of CPython.
   Internally, greenlet heavily depends on the GIL.

- Greatly reduce the chances for crashes during interpreter shutdown.
  See `issue 411
  <https://github.com/python-greenlet/greenlet/issues/411>`_.

Platform Support
----------------

Support for the following platforms was contributed by the community.
Note that they are untested by this project's continuous integration
services.

- Hitachi's `SuperH CPU <https://github.com/python-greenlet/greenlet/issues/166>`_.
- `NetBSD on PowerPC.
  <https://github.com/python-greenlet/greenlet/pull/402>`_
- RiscV 64 with `-fno-omit-frame-pointer
  <https://github.com/python-greenlet/greenlet/pull/404>`_. Note that
  there are `known test failures
  <https://github.com/python-greenlet/greenlet/issues/403>`_, so this
  platform may not work reliably at all.


3.0.3 (2023-12-21)
==================

- Python 3.12: Restore the full ability to walk the stack of a suspended
  greenlet; previously only the innermost frame was exposed. See `issue 388
  <https://github.com/python-greenlet/greenlet/issues/388>`_. Fix by
  Joshua Oreman in `PR 393
  <https://github.com/python-greenlet/greenlet/pull/393/>`_.

3.0.2 (2023-12-08)
==================

- Packaging: Add a minimal ``pyproject.toml`` to sdists.
- Packaging: Various updates to macOS wheels.
- Fix a test case on Arm32. Note that this is not a supported platform
  (there is no CI for it) and support is best effort; there may be
  other issues lurking. See `issue 385 <https://github.com/python-greenlet/greenlet/issues/385>`_


3.0.1 (2023-10-25)
==================

- Fix a potential crash on Python 3.8 at interpreter shutdown time.
  This was a regression from earlier 3.0.x releases. Reported by Matt
  Wozniski in `issue 376 <https://github.com/python-greenlet/greenlet/issues/376>`_.



3.0.0 (2023-10-02)
==================

- No changes from 3.0rc3 aside from the version number.


3.0.0rc3 (2023-09-12)
=====================

- Fix an intermittent error during process termination on some
  platforms (GCC/Linux/libstdc++).


3.0.0rc2 (2023-09-09)
=====================

- Fix some potential bugs (assertion failures and memory leaks) in
  previously-untested error handling code. In some cases, this means
  that the process will execute a controlled ``abort()`` after severe
  trouble when previously the process might have continued for some
  time with a corrupt state. It is unlikely those errors occurred in
  practice.
- Fix some assertion errors and potential bugs with re-entrant
  switches.
- Fix a potential crash when certain compilers compile greenlet with
  high levels of optimization. The symptom would be that switching to
  a greenlet for the first time immediately crashes.
- Fix a potential crash when the callable object passed to the
  greenlet constructor (or set as the ``greenlet.run`` attribute) has
  a destructor attached to it that switches. Typically, triggering
  this issue would require an unlikely subclass of
  ``greenlet.greenlet``.
- Python 3.11+: Fix rare switching errors that could occur when a
  garbage collection was triggered during the middle of a switch, and
  Python-level code in ``__del__`` or weakref callbacks switched to a
  different greenlet and ultimately switched back to the original
  greenlet. This often manifested as a ``SystemError``: "switch
  returned NULL without an exception set."

For context on the fixes, see `gevent issue #1985
<https://github.com/gevent/gevent/issues/1985>`_.

3.0.0rc1 (2023-09-01)
=====================

- Windows wheels are linked statically to the C runtime in an effort
  to prevent import errors on systems without the correct C runtime
  installed. It's not clear if this will make the situation better or
  worse, so please share your experiences in `issue 346
  <https://github.com/python-greenlet/greenlet/issues/346>`_.

  Note that this only applies to the binary wheels found on PyPI.
  Building greenlet from source defaults to the shared library. Set
  the environment variable ``GREENLET_STATIC_RUNTIME=1`` at build time
  to change that.
- Build binary wheels for Python 3.12 on macOS.
- Fix compiling greenlet on a debug build of CPython 3.12. There is
  `one known issue
  <https://github.com/python-greenlet/greenlet/issues/368>`_ that
  leads to an interpreter crash on debug builds.
- Python 3.12: Fix walking the frame stack of suspended greenlets.
  Previously accessing ``glet.gr_frame.f_back`` would crash due to
  `changes in CPython's undocumented internal frame handling <https://github.com/python/cpython/commit/1e197e63e21f77b102ff2601a549dda4b6439455>`_.

Platforms
---------
- Now, greenlet *may* compile and work on Windows ARM64 using
  llvm-mingw, but this is untested and unsupported. See `PR
  <https://github.com/python-greenlet/greenlet/pull/224>`_ by Adrian
  Vladu.
- Now, greenlet *may* compile and work on LoongArch64 Linux systems,
  but this is untested and unsupported. See `PR 257
  <https://github.com/python-greenlet/greenlet/pull/257/files>`_ by merore.

Known Issues
------------

- There may be (very) subtle issues with tracing on Python 3.12, which
  has redesigned the entire tracing infrastructure.

3.0.0a1 (2023-06-21)
====================

- Build binary wheels for S390x Linux. See `PR 358
  <https://github.com/python-greenlet/greenlet/pull/358>`_ from Steven
  Silvester.
- Fix a rare crash on shutdown seen in uWSGI deployments. See `issue
  330 <https://github.com/python-greenlet/greenlet/issues/330>`_ and `PR 356
  <https://github.com/python-greenlet/greenlet/pull/356>`_ from Andrew
  Wason.
- Make the platform-specific low-level C/assembly snippets stop using
  the ``register`` storage class. Newer versions of standards remove
  this storage class, and it has been generally ignored by many
  compilers for some time. See `PR 347
  <https://github.com/python-greenlet/greenlet/pull/347>`_ from Khem
  Raj.
- Add initial support for Python 3.12. See `issue
  <https://github.com/python-greenlet/greenlet/issues/323>`_ and `PR
  <https://github.com/python-greenlet/greenlet/pull/327>`_; thanks go
  to (at least) Michael Droettboom, Andreas Motl, Thomas A Caswell,
  raphaelauv, Hugo van Kemenade, Mark Shannon, and Petr Viktorin.
- Remove support for end-of-life Python versions, including Python
  2.7, Python 3.5 and Python 3.6.
- Require a compiler that supports ``noinline`` directives. See
  `issue 271
  <https://github.com/python-greenlet/greenlet/issues/266>`_.
- Require a compiler that supports C++11.


2.0.2 (2023-01-28)
==================

- Fix calling ``greenlet.settrace()`` with the same tracer object that
  was currently active. See `issue 332
  <https://github.com/python-greenlet/greenlet/issues/332>`_.
- Various compilation and standards conformance fixes. See #335, #336,
  #300, #302, #334.



2.0.1 (2022-11-07)
==================

- Python 3.11: Fix a memory leak. See `issue 328
  <https://github.com/python-greenlet/greenlet/issues/328>`_ and
  `gevent issue 1924 <https://github.com/gevent/gevent/issues/1924>`_.


2.0.0.post0 (2022-11-03)
========================

- Add ``Programming Language :: Python :: 3.11`` to the PyPI
  classifier metadata.


2.0.0 (2022-10-31)
==================

- Nothing changed yet.


2.0.0rc5 (2022-10-31)
=====================

- Linux: Fix another group of rare crashes that could occur when shutting down an
  interpreter running multiple threads. See `issue 325 <https://github.com/python-greenlet/greenlet/issues/325>`_.


2.0.0rc4 (2022-10-30)
=====================

- Linux: Fix a rare crash that could occur when shutting down an
  interpreter running multiple threads, when some of those threads are
  in greenlets making calls to functions that release the GIL.


2.0.0rc3 (2022-10-29)
=====================

- Python 2: Fix a crash that could occur when raising an old-style
  instance object.


2.0.0rc2 (2022-10-28)
=====================

- Workaround `a CPython 3.8 bug
  <https://github.com/python/cpython/issues/81308>`_ that could cause
  the interpreter to crash during an early phase of shutdown with the
  message "Fatal Python error: Python memory allocator called without
  holding the GI." This only impacted CPython 3.8a3 through CPython
  3.9a5; the fix is only applied to CPython 3.8 releases (please don't
  use an early alpha release of CPython 3.9).


2.0.0rc1 (2022-10-27)
=====================

- Deal gracefully with greenlet switches that occur while deferred
  deallocation of objects is happening using CPython's "trash can"
  mechanism. Previously, if a large nested container held items that
  switched greenlets during delayed deallocation, and that second
  greenlet also invoked the trash can, CPython's internal state could
  become corrupt. This was visible as an assertion error in debug
  builds. Now, the relevant internal state is saved and restored
  during greenlet switches. See also `gevent issue 1909
  <https://github.com/gevent/gevent/issues/1909>`_.
- Rename the C API function ``PyGreenlet_GET_PARENT`` to
  ``PyGreenlet_GetParent`` for consistency. The old name remains
  available as a deprecated alias.



2.0.0a2 (2022-03-24)
====================

- Fix a crash on older versions of the Windows C runtime when an
  unhandled C++ exception was thrown inside a greenlet by another
  native extension. This is a bug in that extension, and the
  interpreter will still abort, but at least it does so deliberately.
  Thanks to Kirill Smelkov. See `PR 286
  <https://github.com/python-greenlet/greenlet/pull/286>`_.
- Musllinux wheels for aarch64 are now built, tested, and uploaded to
  PyPI. Thanks to Alexander Piskun.
- This version of greenlet is known to compile and pass tests on
  CPython 3.11.0a6. Earlier 3.11 releases will not work; later
  releases may or may not work. See `PR 294
  <https://github.com/python-greenlet/greenlet/pull/294>`_. Special
  thanks to Victor Stinner, Brandt Bucher and the CPython developers.


2.0.0a1 (2022-01-20)
====================

Platforms
---------

- Add experimental, untested support for 64-bit Windows on ARM using
  MSVC. See `PR 271 <https://github.com/python-greenlet/greenlet/pull/271>`_.

- Drop support for very old versions of GCC and MSVC.

- Compilation now requires a compiler that either supports C++11 or
  has some other intrinsic way to create thread local variables; for
  older GCC, clang and SunStudio we use ``__thread``, while for older
  MSVC we use ``__declspec(thread)``.

- Wheels compatible with the musllinux specification are built,
  tested, and uploaded to PyPI for x86_64. (This was retroactively
  done for version 1.1.2 as well.)

- This version of greenlet is known to compile and pass tests on
  CPython 3.11.0a4. Earlier or later 3.11 releases may or may not
  work. See `PR 280
  <https://github.com/python-greenlet/greenlet/pull/280>`_. Special
  thanks to Brandt Bucher and the CPython developers.

Fixes
~~~~~

- Fix several leaks that could occur when using greenlets from
  multiple threads. For example, it is no longer necessary to call
  ``getcurrent()`` before exiting a thread to allow its main greenlet
  to be cleaned up. See `issue 252 <https://github.com/python-greenlet/greenlet/issues/251>`_.

- Fix the C API ``PyGreenlet_Throw`` to perform the same error
  checking that the Python API ``greenlet.throw()`` does. Previously,
  it did no error checking.

- Fix C++ exception handling on 32-bit Windows. This might have
  ramifications if you embed Python in your application and also use
  SEH on 32-bit windows, or if you embed Python in a C++ application.
  Please contact the maintainers if you have problems in this area.

  In general, C++ exception handling is expected to be better on most
  platforms. This work is ongoing.

Changes
~~~~~~~

- The repr of some greenlets has changed. In particular, if the
  greenlet object was running in a thread that has exited, the repr
  now indicates that. *NOTE:* The repr of a greenlet is not part of
  the API and should not be relied upon by production code. It is
  likely to differ in other implementations such as PyPy.

- Main greenlets from threads that have exited are now marked as dead.


1.1.3.post0 (2022-10-10)
========================

- Add musllinux (Alpine) binary wheels.

.. important:: This preliminary support for Python 3.11 leaks memory.
               Please upgrade to greenlet 2 if you're using Python 3.11.

1.1.3 (2022-08-25)
==================

- Add support for Python 3.11. Please note that Windows binary wheels
  are not available at this time.

.. important:: This preliminary support for Python 3.11 leaks memory.
               Please upgrade to greenlet 2 if you're using Python 3.11.

1.1.2 (2021-09-29)
==================

- Fix a potential crash due to a reference counting error when Python
  subclasses of ``greenlet.greenlet`` were deallocated. The crash
  became more common on Python 3.10; on earlier versions, silent
  memory corruption could result. See `issue 245
  <https://github.com/python-greenlet/greenlet/issues/245>`_. Patch by
  fygao-wish.
- Fix a leak of a list object when the last reference to a greenlet
  was deleted from some other thread than the one to which it
  belonged. For this to work correctly, you must call a greenlet API
  like ``getcurrent()`` before the thread owning the greenlet exits:
  this is a long-standing limitation that can also lead to the leak of
  a thread's main greenlet if not called; we hope to lift this
  limitation. Note that in some cases this may also fix leaks of
  greenlet objects themselves. See `issue 251
  <https://github.com/python-greenlet/greenlet/issues/251>`_.
- Python 3.10: Tracing or profiling into a spawned greenlet didn't
  work as expected. See `issue 256
  <https://github.com/python-greenlet/greenlet/issues/256>`_, reported
  by Joe Rickerby.


1.1.1 (2021-08-06)
==================

- Provide Windows binary wheels for Python 3.10 (64-bit only).

- Update Python 3.10 wheels to be built against 3.10rc1, where
  applicable.


1.1.0 (2021-05-06)
==================

- Add support for Python 3.10. Pre-built binary wheels for 3.10 are
  not currently available for all platforms. The greenlet ABI is
  different on Python 3.10 from all previous versions, but as 3.10 was
  never supported before, and the ABI has not changed on other Python
  versions, this is not considered a reason to change greenlet's major
  version.


1.0.0 (2021-01-13)
==================

- Fix %s and %r formatting of a greenlet on Python 2. Previously it
  would result in a Unicode string instead of a native string. See
  `issue 218
  <https://github.com/python-greenlet/greenlet/issues/218>`_.

- Move continuous integration from Travis CI to Github Actions.


1.0a1 (2020-11-20)
==================

- Add the ability to set a greenlet's PEP 567 contextvars context
  directly, by assigning to the greenlet's ``gr_context`` attribute.
  This restores support for some patterns of using greenlets atop an
  async environment that became more challenging in 0.4.17. Thanks to
  Joshua Oreman, Mike bayer, and Fantix King, among others. See `PR
  198 <https://github.com/python-greenlet/greenlet/pull/198/>`_.

- The repr of greenlet objects now includes extra information about
  its state. This is purely informative and the details are subject to
  change. See `issue 215 <https://github.com/python-greenlet/greenlet/issues/215>`_.

- The ``greenlet`` module is now a package. There are no API changes,
  so all existing imports, including from C code, should continue to
  work.

- (C API) The undocumented ``GREENLET_VERSION`` macro that defined a string
  giving the greenlet version is now deprecated and will not be updated.

- (Documentation) Publish the change log to https://greenlet.readthedocs.io

Supported Platforms
-------------------

- Drop support for Python 2.4, 2.5, 2.6, 3.0, 3.1, 3.2 and 3.4.
  The project metadata now includes the ``python_requires`` data to
  help installation tools understand supported versions.
- Add partial support for AIX ppc64 and IBM i. Thanks to Jesse
  Gorzinski and Kevin Adler. See `PR 197
  <https://github.com/python-greenlet/greenlet/pull/197>`_.

Packaging Changes
-----------------

- Require setuptools to build from source.
- Stop asking setuptools to build both .tar.gz and .zip
  sdists. PyPI has standardized on .tar.gz for all platforms.
- Stop using a custom distutils command to build
  extensions. distutils is deprecated.
- Remove the ability to use the deprecated command
  ``python setup.py test``. Run greenlet tests with your favorite
  unittest-compatible test runner, e.g., ``python -m unittest discover
  greenlet.tests``. See `issue 185 <https://github.com/python-greenlet/greenlet/issues/185>`_.
- The directory layout and resulting sdists have changed.
  See `issue 184
  <https://github.com/python-greenlet/greenlet/issues/184>`_.
- greenlet is now always built with support for tracing and garbage
  collection, and, on Python 3.7 and above, support for context
  variables. The internal and undocumented C preprocessor macros that
  could be used to alter that at compile time have been removed (no
  combination other than the defaults was ever tested). This helps
  define a stable ABI.


0.4.17 (2020-09-22)
===================
- Support for PEP 567 ContextVars

0.4.16
======
- Support for DEC Alpha architecture
- Support for Python 3.9
- Support for Python 3.10a0

0.4.15
======
- Support for RISC-V architecture
- Workaround a gcc bug on ppc64

0.4.14
======
- Support for C-SKY architecture
- Fixed support for ppc64 ABI
- Fixed support for Python 3.7

0.4.13
======
- Support for Python 3.7
- Support for MinGW x64

0.4.12
======
- Stop using trashcan api

0.4.11
======
- Fixes for aarch64 architecture

0.4.10
======
- Added missing files to manifest
- Added workaround for ppc32 on Linux
- Start building binary manylinux1 wheels

0.4.9
=====
- Fixed Windows builds

0.4.8
=====
- Added support for iOS (arm32)
- Added support for ppc64le

0.4.7
=====
- Added a missing workaround for ``return 0`` on mips
- Restore compatibility with Python 2.5
- Fixed stack switching on sparc

0.4.6
=====
- Expose ``_stack_saved`` property on greenlet objects, it may be used to
  introspect the amount of memory used by a saved stack, but the API is
  subject to change in the future
- Added a workaround for ``return 0`` compiler optimizations on all
  architectures
- C API typo fixes

0.4.5
=====
- Fixed several bugs in greenlet C API
- Fixed a bug in multi-threaded applications, which manifested itself
  with spurious "cannot switch to a different thread" exceptions
- Fixed some crashes on arm and mips architectures

0.4.4
=====
- Fixed PyGreenlet_SetParent signature, thanks to BoonsNaibot
- Fixed 64-bit Windows builds depending on wrong runtime dll

0.4.3
=====
- Better slp_switch performance on SPARC
- Drop support for Python 2.3
- Fix trashcan assertions on debug builds of Python
- Remove deprecated -fno-tree-dominator-opts compiler switch
- Enable switch code for SunStudio on 32-bit SunOS
- Support for abc abstract methods in greenlet subclasses
- Support custom directories for tests
- Document switch tracing support

0.4.2
=====
- Add .travis.yml
- Fix 'err' may be used uninitialized in this function
- Check _MSC_VER for msvc specific code
- Fix slp_switch on SPARC for multi-threaded environments
- Add support for m68k

0.4.1
=====
* fix segfaults when using gcc 4.8 on amd64/x86 unix
* try to disable certain gcc 4.8 optimizations that make greenlet
  crash
* Fix greenlet on aarch64 with gcc 4.8
* workaround segfault on SunOS/sun4v
* Add support for Aarch64
* Add support for x32 psABI on x86_64
* Changed memory constraints for assembly macro for PPC Linux
  platforms.

0.4.0
=====
* Greenlet has an instance dictionary now, which means it can be
  used for implementing greenlet local storage, etc. However, this
  might introduce incompatibility if subclasses have ``__dict__`` in their
  ``__slots__``. Classes like that will fail, because greenlet already
  has ``__dict__`` out of the box.
* Greenlet no longer leaks memory after thread termination, as long as
  terminated thread has no running greenlets left at the time.
* Add support for debian sparc and openbsd5-sparc64
* Add support for ppc64 linux
* Don't allow greenlets to be copied with copy.copy/deepcopy
* Fix arm32/thumb support
* Restore greenlet's parent after kill
* Add experimental greenlet tracing

0.3.4
=====
* Use plain distutils for install command, this fixes installation of
  the greenlet.h header.
* Enhanced arm32 support
* Fix support for Linux/S390 zSeries
* Workaround compiler bug on RHEL 3 / CentOS 3

0.3.3
=====
* Use sphinx to build documentation and publish it on greenlet.rtfd.org
* Prevent segfaults on openbsd 4/i386
* Workaround gcc-4.0 not allowing to clobber rbx
* Enhance test infrastructure
* Fix possible compilation problems when including greenlet.h in C++ mode
* Make the greenlet module work on x64 windows
* Add a test for greenlet C++ exceptions
* Fix compilation on Solaris with SunStudio

0.3.2
=====
* Fix various crashes with recent gcc versions and VC90
* Try to fix stack save/restore on arm32
* Store and restore the threadstate on exceptions like pypy/stackless do
* GreenletExit is now based on BaseException on Python >= 2.5
* Switch to using PyCapsule for Python 2.7 and 3.1
* Port for AIX on PowerPC
* Fix the sparc/solaris header
* Improved build dependencies patch from flub.
* Can't pass parent=None to greenlet.greenlet() (fixes #21)
* Rudimentary gc support (only non-live greenlets are garbage collected though)

0.3.1
=====
* Fix reference leak when passing keyword arguments to greenlets (mbachry)
* Updated documentation.

0.3
===
* Python 3 support.
* New C API to expose Greenlets to C Extensions.
* greenlet.switch() now accept's keyword arguments.
* Fix Python crasher caused by switching to new greenlet from another thread.
* Fix Python 2.6 crash on Windows when built with VS2009. (arigo)
* arm32 support from stackless (Sylvain Baro)
* Linux mips support (Thiemo Seufer)
* MingGW GCC 4.4 support (Giovanni Bajo)
* Fix for a threading bug (issue 40 in py lib) (arigo and ghazel)
* Loads more unit tests, some from py lib (3 times as many as Greenlet 0.2)
* Add documentation from py lib.
* General code, documentation and repository cleanup (Kyle Ambroff, Jared Kuolt)
