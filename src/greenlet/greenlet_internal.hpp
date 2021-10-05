/* -*- indent-tabs-mode: nil; tab-width: 4; -*- */
#ifndef GREENLET_INTERNAL_H
#define GREENLET_INTERNAL_H

/**
 * Implementation helpers.
 *
 * C++ templates and inline functions should go here.
 */


#define GREENLET_MODULE
namespace greenlet {
    class ThreadState;
};
#define state_ptr_t greenlet::ThreadState*

#include "greenlet.h"
#include "greenlet_compiler_compat.hpp"
#include "greenlet_cpython_compat.hpp"


#include <vector>
#include <memory>

#define UNUSED(expr) do { (void)(expr); } while (0)

namespace greenlet {

// This allocator is stateless; all instances are identical.
// It can *ONLY* be used when we're sure we're holding the GIL
// (Python's allocators require the GIL).
template <class T>
struct PythonAllocator : public std::allocator<T> {
    // As a reminder: the `delete` expression first executes
    // the destructors, and then it calls the static ``operator delete``
    // on the type to release the storage. That's what our dispose()
    // mimics.
    PythonAllocator(const PythonAllocator& other)
        : std::allocator<T>()
    {
        UNUSED(other);
    }

    PythonAllocator(const std::allocator<T> other)
        : std::allocator<T>(other)
    {}

    template <class U>
    PythonAllocator(const std::allocator<U>& other)
        : std::allocator<T>(other)
    {
    }

    PythonAllocator() : std::allocator<T>() {}

    T* allocate(size_t number_objects, const void* hint=0)
    {
        UNUSED(hint);
        void* p;
        if (number_objects == 1)
            p = PyObject_Malloc(sizeof(T));
        else
            p = PyMem_Malloc(sizeof(T) * number_objects);
        return static_cast<T*>(p);
    }

    void deallocate(T* t, size_t n)
    {
        void* p = t;
        if (n == 1) {
            PyObject_Free(p);
        }
        else
            PyMem_Free(p);
    }

    // Destroy and deallocate in one step.
    void dispose(T* other)
    {
        this->destroy(other);
        this->deallocate(other, 1);
    }
};

typedef std::vector<PyGreenlet*, PythonAllocator<PyGreenlet*> > g_deleteme_t;

};

/**
  * Forward declarations needed in multiple files.
  */
static PyGreenlet* green_create_main();
static PyObject* green_switch(PyGreenlet* self, PyObject* args, PyObject* kwargs);


#endif

// Local Variables:
// flycheck-clang-include-path: ("../../include" "/opt/local/Library/Frameworks/Python.framework/Versions/2.7/include/python2.7")
// End:
