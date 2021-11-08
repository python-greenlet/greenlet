/* -*- indent-tabs-mode: nil; tab-width: 4; -*- */
#ifndef GREENLET_INTERNAL_H
#define GREENLET_INTERNAL_H
#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wunused-function"
#    pragma clang diagnostic ignored "-Wmissing-field-initializers"
#    pragma clang diagnostic ignored "-Wunused-variable"
#endif

/**
 * Implementation helpers.
 *
 * C++ templates and inline functions should go here.
 */
#include "greenlet_compiler_compat.hpp"
#include "greenlet_cpython_compat.hpp"
#include "greenlet_exceptions.hpp"
#include "greenlet_greenlet.hpp"
#include "greenlet_allocator.hpp"

#include <vector>

#define GREENLET_MODULE
struct _greenlet;
typedef struct _greenlet PyGreenlet;
namespace greenlet {

    class ThreadState;

    typedef std::vector<PyGreenlet*, PythonAllocator<PyGreenlet*> > g_deleteme_t;

};
struct _PyMainGreenlet;

#define implementation_ptr_t greenlet::Greenlet*


#include "greenlet.h"


static inline bool PyGreenlet_STARTED(const greenlet::Greenlet* g)
{
    return g->started();
}

static inline bool PyGreenlet_STARTED(const greenlet::refs::PyObjectPointer<PyGreenlet>& g)
{
    return PyGreenlet_STARTED(g->pimpl);
}

static inline bool PyGreenlet_STARTED(const PyGreenlet* g)
{
    return PyGreenlet_STARTED(g->pimpl);
}

static inline bool PyGreenlet_MAIN(const greenlet::Greenlet* g)
{
    return g->main();
}

static inline bool PyGreenlet_MAIN(const greenlet::refs::PyObjectPointer<PyGreenlet>& g)
{
    return PyGreenlet_MAIN(g->pimpl);
}

static inline bool PyGreenlet_MAIN(const PyGreenlet* g)
{
    return PyGreenlet_MAIN(g->pimpl);
}

static inline bool PyGreenlet_ACTIVE(const greenlet::Greenlet* g)
{
    return g->active();
}

static inline bool PyGreenlet_ACTIVE(const greenlet::refs::PyObjectPointer<PyGreenlet>& g)
{
    return PyGreenlet_ACTIVE(g->pimpl);
}

static inline bool PyGreenlet_ACTIVE(const PyGreenlet* g)
{
    return PyGreenlet_ACTIVE(g->pimpl);
}

template<>
inline greenlet::refs::_OwnedGreenlet<PyMainGreenlet>::operator greenlet::Greenlet*() const G_NOEXCEPT
{
    if (!this->p) {
        return nullptr;
    }
    return reinterpret_cast<PyGreenlet*>(this->p)->pimpl;
}

template <typename T, greenlet::refs::TypeChecker TC>
inline greenlet::Greenlet* greenlet::refs::_OwnedGreenlet<T, TC>::operator->() const G_NOEXCEPT
{
    return reinterpret_cast<PyGreenlet*>(this->p)->pimpl;
}

template <typename T, greenlet::refs::TypeChecker TC>
inline greenlet::Greenlet* greenlet::refs::_BorrowedGreenlet<T, TC>::operator->() const G_NOEXCEPT
{
    return reinterpret_cast<PyGreenlet*>(this->p)->pimpl;
}

#include <memory>
#include <stdexcept>


extern PyTypeObject PyGreenlet_Type;

// Define a special type for the main greenlets. This way it can have
// a thread state pointer without having to carry the expense of a
// NULL field around on every other greenlet.
// At the Python level, the main greenlet class is
// *almost* indistinguisable from plain greenlets.
typedef struct _PyMainGreenlet
{
    PyGreenlet super;
    greenlet::ThreadState* volatile thread_state;
} PyMainGreenlet;

// GCC and clang support mixing designated and non-designated
// initializers; recent MSVC requires ``/std=c++20`` to use
// designated initializer, and doesn't permit mixing. And then older
// MSVC doesn't support any of it.
PyTypeObject PyMainGreenlet_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "greenlet.main_greenlet", // tp_name
    sizeof(PyMainGreenlet)
};






/**
  * Forward declarations needed in multiple files.
  */
static PyMainGreenlet* green_create_main();
static PyObject* green_switch(PyGreenlet* self, PyObject* args, PyObject* kwargs);

#ifdef __clang__
#    pragma clang diagnostic pop
#endif


#endif

// Local Variables:
// flycheck-clang-include-path: ("../../include" "/opt/local/Library/Frameworks/Python.framework/Versions/3.10/include/python3.10")
// End:
