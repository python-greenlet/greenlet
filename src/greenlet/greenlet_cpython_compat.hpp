/* -*- indent-tabs-mode: nil; tab-width: 4; -*- */
#ifndef GREENLET_CPYTHON_COMPAT_H
#define GREENLET_CPYTHON_COMPAT_H

/**
 * Helpers for compatibility with multiple versions of CPython.
 */


#if PY_VERSION_HEX >= 0x030700A3
#    define GREENLET_PY37 1
#else
#    define GREENLET_PY37 0
#endif

#if PY_VERSION_HEX >= 0x30A00B1
/*
Python 3.10 beta 1 changed tstate->use_tracing to a nested cframe member.
See https://github.com/python/cpython/pull/25276
We have to save and restore this as well.
*/
#    define TSTATE_USE_TRACING(tstate) (tstate->cframe->use_tracing)
#    define GREENLET_USE_CFRAME 1
#else
#    define TSTATE_USE_TRACING(tstate) (tstate->use_tracing)
#    define GREENLET_USE_CFRAME 0
#endif

#ifndef Py_SET_REFCNT
/* Py_REFCNT and Py_SIZE macros are converted to functions
https://bugs.python.org/issue39573 */
#    define Py_SET_REFCNT(obj, refcnt) Py_REFCNT(obj) = (refcnt)
#endif

#ifndef _Py_DEC_REFTOTAL
/* _Py_DEC_REFTOTAL macro has been removed from Python 3.9 by:
  https://github.com/python/cpython/commit/49932fec62c616ec88da52642339d83ae719e924
*/
#    ifdef Py_REF_DEBUG
#        define _Py_DEC_REFTOTAL _Py_RefTotal--
#    else
#        define _Py_DEC_REFTOTAL
#    endif
#endif

#endif /* GREENLET_CPYTHON_COMPAT_H */
