/* -*- indent-tabs-mode: nil; tab-width: 4; -*- */
#ifndef GREENLET_CPYTHON_COMPAT_H
#define GREENLET_CPYTHON_COMPAT_H

/**
 * Helpers for compatibility with multiple versions of CPython.
 */

#include "Python.h"

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
// Define these flags like Cython does if we're on an old version.
#ifndef Py_TPFLAGS_CHECKTYPES
  #define Py_TPFLAGS_CHECKTYPES 0
#endif
#ifndef Py_TPFLAGS_HAVE_INDEX
  #define Py_TPFLAGS_HAVE_INDEX 0
#endif
#ifndef Py_TPFLAGS_HAVE_NEWBUFFER
  #define Py_TPFLAGS_HAVE_NEWBUFFER 0
#endif
#ifndef Py_TPFLAGS_HAVE_FINALIZE
  #define Py_TPFLAGS_HAVE_FINALIZE 0
#endif
#ifndef Py_TPFLAGS_HAVE_VERSION_TAG
   #define Py_TPFLAGS_HAVE_VERSION_TAG 0
#endif

#define G_TPFLAGS_DEFAULT Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VERSION_TAG | Py_TPFLAGS_CHECKTYPES | Py_TPFLAGS_HAVE_NEWBUFFER | Py_TPFLAGS_HAVE_GC

#if PY_MAJOR_VERSION >= 3
#    define GNative_FromFormat PyUnicode_FromFormat
#else
#    define GNative_FromFormat PyString_FromFormat
#endif

#if PY_MAJOR_VERSION >= 3
#    define Greenlet_Intern PyUnicode_InternFromString
#else
#    define Greenlet_Intern PyString_InternFromString
#endif

#if PY_VERSION_HEX < 0x03090000
// The official version only became available in 3.9
#    define PyObject_GC_IsTracked(o) _PyObject_GC_IS_TRACKED(o)
#endif

#if PY_MAJOR_VERSION < 3
struct PyModuleDef {
    int unused;
    const char* const m_name;
    const char* m_doc;
    Py_ssize_t m_size;
    PyMethodDef* m_methods;
    // Then several more fields we're not currently using.
};
#define PyModuleDef_HEAD_INIT 1
// NOTE: On Python 3, this returns a new reference (the module isn't
// permanently in sys.modules yet until this is returned from the init
// function), but the Python 2 version returns a *borrowed* reference.
PyObject* PyModule_Create(PyModuleDef* m)
{
    return Py_InitModule(m->m_name, m->m_methods);
}
#endif

#endif /* GREENLET_CPYTHON_COMPAT_H */
