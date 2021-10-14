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


#define GREENLET_MODULE
namespace greenlet {
    class ThreadState;
};
struct _PyMainGreenlet;
#define main_greenlet_ptr_t struct _PyMainGreenlet*


#include "greenlet.h"
#include "greenlet_compiler_compat.hpp"
#include "greenlet_cpython_compat.hpp"

#include <vector>
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
    greenlet::ThreadState* thread_state;
} PyMainGreenlet;

// GCC and clang support mixing designated and non-designated
// initializers; recent MSVC requires ``/std=c++20`` to use
// designated initializer, and doesn't permit mixing. And then older
// MSVC doesn't support any of it.
static PyTypeObject PyMainGreenlet_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "greenlet.main_greenlet", // tp_name
    sizeof(PyMainGreenlet)
};


#define UNUSED(expr) do { (void)(expr); } while (0)

namespace greenlet
{

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

    class TypeError : public std::runtime_error
    {
    public:
        TypeError(const char* const what) : std::runtime_error(what)
        {
            if (!PyErr_Occurred()) {
                PyErr_SetString(PyExc_TypeError, what);
            }
        }
    };

    // A set of classes to make reference counting rules in python
    // code explicit.
    //
    // For a class with a single pointer member, whose constructor
    // does nothing but copy a pointer parameter into the member, and
    // which can then be converted back to the pointer type, compilers
    // generate code that's the same as just passing the pointer.
    // That is, func(BorrowedReference x) called like ``PyObject* p =
    // ...; f(p)`` has 0 overhead. Similarly, they "unpack" to the
    // pointer type with 0 overhead.
    //
    // If there are no virtual functions, no complex inheritance (maybe?) and
    // no destructor, these can be directly used as parameters in
    // Python callbacks like tp_init: the layout is the same as a
    // single pointer. Only subclasses with trivial constructors that
    // do nothing but set the single pointer member are safe to use
    // that way.
    class OwnedObject;
    template<typename T>
    class BorrowedReference;
    typedef BorrowedReference<PyObject> BorrowedObject;

    // This is the base class for thnigs that can be done with a
    // PyObject pointer. It assumes nothing about memory management.
    // NOTE: Nothing is virtual, so subclasses shouldn't add new
    // storage fields or try to override these methods.
    template <typename T=PyObject>
    class PyObjectPointer
    {
    protected:
        T* p;
    public:
        PyObjectPointer(T* it) : p(it)
        {}

        T* operator->() const
        {
            return this->p;
        }

        bool operator==(const PyObjectPointer& other) const
        {
            return other.p == this->p;
        }

        G_EXPLICIT_OP operator bool() const
        {
            return p != nullptr;
        }

        inline Py_ssize_t REFCNT() const
        {
            return Py_REFCNT(p);
        }

        inline OwnedObject PyGetAttrString(const char* const name) const;
        inline OwnedObject PyCall(const BorrowedObject& arg) const;
        inline OwnedObject PyCall(PyMainGreenlet* arg) const;
        inline OwnedObject PyCall(const PyObject* arg) const;
    protected:
        void _set_raw_pointer(void* t)
        {
            p = reinterpret_cast<T*>(t);
        }
        void* _get_raw_pointer() const
        {
            return p;
        }
    };

    template <typename T=PyObject>
    class BorrowedReference : public PyObjectPointer<T>
    {
    public:
        // Allow implicit creation from PyObject* pointers as we
        // transition to using these classes. Also allow automatic
        // conversion to PyObject* for passing to C API calls and even
        // for Py_INCREF/DECREF, because we ourselves do no memory management.
        BorrowedReference(T* it) : PyObjectPointer<T>(it)
        {}

        operator T*() const
        {
            return this->p;
        }
    };

    typedef BorrowedReference<PyObject> BorrowedObject;
    //typedef BorrowedReference<PyGreenlet> BorrowedGreenlet;

    class BorrowedGreenlet : public BorrowedReference<PyGreenlet>
    {
    public:
        BorrowedGreenlet() : BorrowedReference(nullptr)
        {}

        BorrowedGreenlet(PyGreenlet* it) : BorrowedReference(it)
        {}

        BorrowedGreenlet(const BorrowedObject& it) : BorrowedReference(nullptr)
        {
            if (!PyGreenlet_Check(it)) {
                throw TypeError("Expected a greenlet");
            }
            _set_raw_pointer(static_cast<PyObject*>(it));
        }

    };

    class ImmortalObject : public PyObjectPointer<>
    {
    private:
        G_NO_COPIES_OF_CLS(ImmortalObject);
    public:
        // CAUTION: Constructing from a PyObject*
        // steals the reference.
        explicit ImmortalObject(PyObject* it) : PyObjectPointer<>(it)
        {
        }

        operator PyObject*() const
        {
            return this->p;
        }
    };

    class OwnedObject : public PyObjectPointer<>
    {
    private:
        // We can't use G_NO_COPIES_OF_CLS(OwnedObject) because we need
        // one copy constructor.
        G_NO_ASSIGNMENT_OF_CLS(OwnedObject);
        friend class OwnedList;
    public:
        // CAUTION: Constructing from a PyObject*
        // steals the reference.
        explicit OwnedObject(PyObject* it) : PyObjectPointer<>(it)
        {
        }
        // TODO: In C++11, this should be the move constructor.
        // In the common case of ``OwnedObject x = Py_SomeFunction()``,
        // the call to the copy constructor will be elided completely.
        OwnedObject(const OwnedObject& other) : PyObjectPointer<>(other.p)
        {
            Py_XINCREF(this->p);
        }

        virtual ~OwnedObject()
        {
            Py_CLEAR(p);
        }
    };

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyGetAttrString(const char* const name) const
    {
        assert(this->p);
        return OwnedObject(PyObject_GetAttrString(this->p, name));
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyCall(const BorrowedObject& arg) const
    {
        return this->PyCall(static_cast<PyObject*>(arg));
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyCall(PyMainGreenlet* arg) const
    {
        return this->PyCall(reinterpret_cast<const PyObject*>(arg));
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyCall(const PyObject* arg) const
    {
        assert(this->p);
        return OwnedObject(PyObject_CallFunctionObjArgs(this->p, arg, NULL));
    }

    class OwnedList : public OwnedObject
    {
    private:
        G_NO_ASSIGNMENT_OF_CLS(OwnedList);
    public:
        // TODO: Would like to use move.
        OwnedList(const OwnedObject& other) : OwnedObject(other)
        {
            // At this point, we own a reference to the object,
            // or its null.
            // Should this raise type error?
            if (p && !PyList_Check(p)) {
                // Drat, not a list, drop the reference.
                Py_DECREF(p);
                p = nullptr;
            }
        }

        OwnedList& operator=(const OwnedObject& other)
        {
            if (other && PyList_Check(other.p)) {
                // Valid list. Own a new reference to it, discard the
                // reference to what we did own.
                PyObject* new_ptr = other.p;
                Py_INCREF(new_ptr);
                Py_XDECREF(this->p);
                this->p = new_ptr;
            }
            else {
                // Either the other object was NULL (an error) or it
                // wasn't a list. Either way, we're now invalidated.
                Py_XDECREF(this->p);
                this->p = nullptr;
            }
            return *this;
        }

        inline bool empty() const
        {
            return PyList_GET_SIZE(p) == 0;
        }

        inline Py_ssize_t size() const
        {
            return PyList_GET_SIZE(p);
        }

        inline BorrowedObject at(const Py_ssize_t index) const
        {
            return PyList_GET_ITEM(p, index);
        }

        inline void clear()
        {
            PyList_SetSlice(p, 0, PyList_GET_SIZE(p), NULL);
        }
    };

    class OutParam : public PyObjectPointer<>
    {
        // Not an owned object, because we can't be initialized with
        // one, and we only sometimes acquire ownership.
    private:
        G_NO_COPIES_OF_CLS(OutParam);
    public:
        // To allow declaring these and passing them to
        // PyErr_Fetch we implement the empty constructor,
        // and the address operator.
        OutParam() : PyObjectPointer<>(nullptr)
        {
        }

        PyObject** operator&()
        {
            return &this->p;
        }

        // This allows us to pass one directly without the &
        operator PyObject**()
        {
            return &this->p;
        }

        // We don't want to be able to pass these to Py_DECREF and
        // such so we don't have the implicit PyObject* conversion.

        inline PyObject* disown()
        {
            PyObject* result = this->p;
            this->p = nullptr;
            return result;
        }

        ~OutParam()
        {
            Py_XDECREF(p);
        }
    };
    /*
    class Stolen
    {
    private:
        PyObject* p;
        G_NO_COPIES_OF_CLS(Stolen);
    public:
        Stolen(OutParam& param) : p(param.p)
        {
            param.p = nullptr;
        }
        operator PyObject*() const
        {
            return this->p;
        }
    };
    */

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
