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
