#ifndef GREENLET_ALLOCATOR_HPP
#define GREENLET_ALLOCATOR_HPP

#include <Python.h>
#include <memory>
#include "greenlet_compiler_compat.hpp"
// #include <iostream>

namespace greenlet
{
    // This allocator is stateless; all instances are identical.
    // It can *ONLY* be used when we're sure we're holding the GIL
    // (Python's allocators require the GIL).
    template <class T>
    struct PythonAllocator : public std::allocator<T> {

        PythonAllocator(const PythonAllocator& UNUSED(other))
            : std::allocator<T>()
        {
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

        T* allocate(size_t number_objects, const void* UNUSED(hint)=0)
        {
            // using std::cerr;
            // using std::endl;

            // cerr << "Allocating " << number_objects
            //      << " at " << UNUSED_hint
            //      << " from " << typeid(this).name()
            //      << endl;
            void* p;
            if (number_objects == 1)
                p = PyObject_Malloc(sizeof(T));
            else
                p = PyMem_Malloc(sizeof(T) * number_objects);
            return static_cast<T*>(p);
        }

        void deallocate(T* t, size_t n)
        {
            // using std::cerr;
            // using std::endl;
            // cerr << "Deallocating " << n
            //      << " at " << t
            //      << " of " << typeid(t).name()
            //      << " from " << typeid(this).name()
            //      << endl;
            void* p = t;
            if (n == 1) {
                PyObject_Free(p);
            }
            else
                PyMem_Free(p);
        }

    };
}

#endif
