==========================
 Caveats and Known Issues
==========================

This document will describe known issues and sharp edges of greenlets.


Native Functions Should Be Re-entrant
=====================================

Use caution when switching greenlet stacks that include native (C)
frames. Much like with threads, if the library function is not
re-entrant, and more than one greenlet attempts to enter it, subtle
problems can result.

Common constructs in C that may not be reentrant include:

- static variables in functions;
- global variables.

This was the source of an issue in gevent that led to corruption of
libuv's internal state. The fix was to avoid re-entering the
vulnerable function.

Use Caution Mixing Greenlets and Signal Handlers
================================================

In CPython, signal handler functions *must* return in order for the
rest of the program to proceed. Switching greenlets in a signal
handler to, for example, get back to the main greenlet, such that the
signal handler function doesn't really return to CPython, is likely to
lead to a hang.

See :issue:`143` for an example.

Free-threading Is Experimental
==============================

Beginning with greenlet 3.3.0, support for Python 3.14's free-threaded
mode is enabled. Use caution, as it has only limited testing.

There are known issues running greenlets in a free-threaded CPython.
These include:

- As with any threaded program, use caution when forking. Greenlet
  maintains internal locks and forking at the wrong time might result
  in the child process hanging.
- Garbage collection differences may cause ``GreenletExit`` to no
  longer be raised in certain multi-threaded scenarios.
- There may be other memory leaks.
- It may be necessary to disable the thread-local bytecode cache (and
  hence the specializing interpreter) to avoid a rare crash. If your
  process crashes on accessing an attribute or object, or at shutdown
  during module cleanup, try setting the environment variable
  ``PYTHON_TLBC=0`` or using the ``-X tlbc=0`` argument.
