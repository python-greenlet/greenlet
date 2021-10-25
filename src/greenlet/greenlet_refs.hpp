#ifndef GREENLET_REFS_HPP
#define GREENLET_REFS_HPP

#include "greenlet_internal.hpp"

namespace greenlet { namespace refs {
    // A set of classes to make reference counting rules in python
    // code explicit.
    //
    // Rules of use:
    // (1) Functions returning a new reference that the caller of the
    // function is expected to dispose of should return a
    // ``OwnedObject`` object. This object automatically releases its
    // reference when it goes out of scope. It works like a ``std::shared_ptr``
    // and can be copied or used as a function parameter (but don't do
    // that). Note that constructing a ``OwnedObject`` from a
    // PyObject* steals the reference.
    // (2) Parameters to functions should be either a
    // ``OwnedObject&``, or, more generally, a ``PyObjectPointer&``.
    // If the function needs to create its own new reference, it can
    // do so by copying to a local ``OwnedObject``.
    // (3) Functions returning an existing pointer that is NOT
    // incref'd, and which the caller MUST NOT decref,
    // should return a ``BorrowedObject``.

    //
    // For a class with a single pointer member, whose constructor
    // does nothing but copy a pointer parameter into the member, and
    // which can then be converted back to the pointer type, compilers
    // generate code that's the same as just passing the pointer.
    // That is, func(BorrowedObject x) called like ``PyObject* p =
    // ...; f(p)`` has 0 overhead. Similarly, they "unpack" to the
    // pointer type with 0 overhead.
    //
    // If there are no virtual functions, no complex inheritance (maybe?) and
    // no destructor, these can be directly used as parameters in
    // Python callbacks like tp_init: the layout is the same as a
    // single pointer. Only subclasses with trivial constructors that
    // do nothing but set the single pointer member are safe to use
    // that way.
    template<typename T>
    class OwnedReference;

    template<typename T>
    class BorrowedReference;

    typedef BorrowedReference<PyObject> BorrowedObject;
    typedef OwnedReference<PyObject> OwnedObject;

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
        explicit PyObjectPointer(T* it=nullptr) : p(it)
        {
        }

        // We don't allow automatic casting to PyObject* at this
        // level, because then we could be passed to Py_DECREF/INCREF,
        // but we want nothing to do with memory management. If you
        // know better, then you can use the get() method, like on a
        // std::shared_ptr. Except we name it borrow() to clarify that
        // if this is a reference-tracked object, the pointer you get
        // back will go away when the object does.
        // TODO: This should probably not exist here, but be moved
        // down to relevant sub-types.

        inline T* borrow() const G_NOEXCEPT
        {
            return this->p;
        }

        PyObject* borrow_o() const G_NOEXCEPT
        {
            return reinterpret_cast<PyObject*>(this->p);
        }

        inline T* operator->() const G_NOEXCEPT
        {
            return this->p;
        }

        bool is_None() const G_NOEXCEPT
        {
            return this->p == Py_None;
        }

        G_EXPLICIT_OP operator bool() const G_NOEXCEPT
        {
            return p != nullptr;
        }

        inline Py_ssize_t REFCNT() const G_NOEXCEPT
        {
            return p ? Py_REFCNT(p) : -42;
        }

        inline PyTypeObject* TYPE() const G_NOEXCEPT
        {
            return p ? Py_TYPE(p) : nullptr;
        }

        // TODO: These two methods only make sense for greenlet
        // objects, but there's not a good spot in the inheritance
        // tree to put them without introducing VTable pointers
        inline bool active() const
        {
            return PyGreenlet_ACTIVE(this->p);
        }

        inline bool started() const
        {
            return PyGreenlet_STARTED(this->p);
        }

        inline OwnedObject PyStr() const G_NOEXCEPT;
        inline const char* as_str() const G_NOEXCEPT;
        inline OwnedObject PyGetAttrString(const char* const name) const G_NOEXCEPT;
        inline OwnedObject PyRequireAttrString(const char* const name) const;
        inline OwnedObject PyCall(const BorrowedObject& arg) const G_NOEXCEPT;
        inline OwnedObject PyCall(PyMainGreenlet* arg) const G_NOEXCEPT;
        inline OwnedObject PyCall(const PyObject* arg) const G_NOEXCEPT;
        // PyObject_Call(this, args, kwargs);
        inline OwnedObject PyCall(const BorrowedObject args,
                                  const BorrowedObject kwargs) const G_NOEXCEPT;
        inline OwnedObject PyCall(const OwnedObject& args,
                                  const OwnedObject& kwargs) const G_NOEXCEPT;

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


    template<typename T>
    inline bool operator==(const PyObjectPointer<T>& lhs, const void* const rhs) G_NOEXCEPT
    {
        return lhs.borrow_o() == rhs;
    }

    template<typename T, typename X>
    inline bool operator==(const PyObjectPointer<T>& lhs, const PyObjectPointer<X>& rhs) G_NOEXCEPT
    {
        return lhs.borrow_o() == rhs.borrow_o();
    }

    template<typename T, typename X>
    inline bool operator!=(const PyObjectPointer<T>& lhs, const PyObjectPointer<X>& rhs) G_NOEXCEPT
    {
        return lhs.borrow_o() != rhs.borrow_o();
    }

    template<typename T=PyObject>
    class OwnedReference : public PyObjectPointer<T>
    {
    private:
        friend class OwnedList;

    protected:
        explicit OwnedReference(T* it) : PyObjectPointer<T>(it)
        {
        }

    public:

        // Constructors

        static OwnedReference<T> consuming(PyObject* p)
        {
            return OwnedReference<T>(p);
        }

        static OwnedReference<T> owning(T* p)
        {
            OwnedReference<T> result(p);
            Py_XINCREF(result.p);
            return result;
        }

        OwnedReference() : PyObjectPointer<T>(nullptr)
        {}

        OwnedReference(const PyObjectPointer<>& other) : PyObjectPointer<T>(nullptr)
        {
            this->p = other.borrow();
            Py_XINCREF(this->p);
        }

        // It would be good to make use of the C++11 distinction
        // between move and copy operations, e.g., constructing from a
        // pointer should be a move operation.
        // In the common case of ``OwnedObject x = Py_SomeFunction()``,
        // the call to the copy constructor will be elided completely.
        OwnedReference(const OwnedReference<T>& other) : PyObjectPointer<T>(other.p)
        {
            Py_XINCREF(this->p);
        }

        static OwnedReference<T> None()
        {
            Py_INCREF(Py_None);
            return OwnedReference<T>(Py_None);
        }

        // XXX: I'd prefer to do things like this with
        // a call to std::move or std::swap, but I'm having issues
        // making that work...

        OwnedReference<T>& operator=(const OwnedReference<T>& other)
        {
            Py_XINCREF(other.p);
            const T* tmp = this->p;
            this->p = other.p;
            Py_XDECREF(tmp);
            return *this;
        }

        OwnedReference<T>& operator=(T* const other)
        {
            Py_XINCREF(other);
            T* tmp = this->p;
            this->p = other;
            Py_XDECREF(tmp);
            return *this;
        }

        OwnedReference<T>& operator=(const BorrowedReference<T> other)
        {
            return this->operator=(other.borrow());
        }

        inline void steal(T* other)
        {
            assert(this->p == nullptr);
            this->p = other;
        }

        T* relinquish_ownership()
        {
            T* result = this->p;
            this->p = nullptr;
            return result;
        }

        T* acquire() const
        {
            // Return a new reference.
            // TODO: This may go away when we have reference objects
            // throughout the code.
            Py_XINCREF(this->p);
            return this->p;
        }

        // Nothing else declares a destructor, we're the leaf, so we
        // should be able to get away without virtual.
        ~OwnedReference()
        {
            Py_CLEAR(this->p);
        }

        void CLEAR()
        {
            Py_CLEAR(this->p);
        }
    };

    class NewReference : public OwnedObject
    {
    private:
        G_NO_COPIES_OF_CLS(NewReference);
    public:
        // Consumes the reference. Only use this
        // for API return values.
        NewReference(PyObject* it) : OwnedObject(it)
        {
        }
    };

    template<typename T>
    class _OwnedGreenlet;

    typedef _OwnedGreenlet<PyGreenlet> OwnedGreenlet;
    typedef _OwnedGreenlet<PyMainGreenlet> OwnedMainGreenlet;

    template<typename T>
    class _BorrowedGreenlet;
    typedef _BorrowedGreenlet<PyGreenlet> BorrowedGreenlet;


    template<typename T=PyGreenlet>
    class _OwnedGreenlet: public OwnedReference<T>
    {
    private:
    protected:
        _OwnedGreenlet<T>(T* it) : OwnedReference<T>(it)
        {}
    public:
        _OwnedGreenlet() : OwnedReference<T>()
        {}

        _OwnedGreenlet(const _OwnedGreenlet<T>& other) : OwnedReference<T>(other)
        {
        }
        _OwnedGreenlet(OwnedMainGreenlet& other) :
            OwnedReference<T>(reinterpret_cast<T*>(other.acquire()))
        {
        }
        _OwnedGreenlet(const BorrowedGreenlet& other);
        // Steals a reference.
        static _OwnedGreenlet<T> consuming(PyGreenlet* it)
        {
            return _OwnedGreenlet<T>(reinterpret_cast<T*>(it));
        }
        static _OwnedGreenlet<T> consuming(PyMainGreenlet* it)
        {
            return _OwnedGreenlet<T>(reinterpret_cast<T*>(it));
        }

        inline _OwnedGreenlet<T>& operator=(const OwnedGreenlet& other)
        {
            return this->operator=(other.borrow());
        }

        inline _OwnedGreenlet<T>& operator=(const BorrowedGreenlet& other);

        _OwnedGreenlet<T>& operator=(const OwnedMainGreenlet& other)
        {
            PyMainGreenlet* owned = other.acquire();
            Py_XDECREF(this->p);
            this->p = reinterpret_cast<T*>(owned);
            return *this;
        }

        _OwnedGreenlet<T>& operator=(T* const other)
        {
            OwnedReference<T>::operator=(other);
            return *this;
        }

        T* relinquish_ownership()
        {
            T* result = this->p;
            this->p = nullptr;
            return result;
        }

        PyObject* relinquish_ownership_o()
        {
            return reinterpret_cast<PyObject*>(relinquish_ownership());
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

        BorrowedReference() : PyObjectPointer<T>(nullptr)
        {}

        operator T*() const
        {
            return this->p;
        }
    };

    typedef BorrowedReference<PyObject> BorrowedObject;
    //typedef BorrowedReference<PyGreenlet> BorrowedGreenlet;

    template<typename T=PyGreenlet>
    class _BorrowedGreenlet : public BorrowedReference<T>
    {
    public:
        _BorrowedGreenlet() :
            BorrowedReference<T>(nullptr)
        {}

        _BorrowedGreenlet(T* it) :
            BorrowedReference<T>(it)
        {}

        _BorrowedGreenlet(const BorrowedObject& it) :
            BorrowedReference<T>(nullptr)
        {
            if (!PyGreenlet_Check(it)) {
                throw TypeError("Expected a greenlet");
            }
            this->_set_raw_pointer(static_cast<PyObject*>(it));
        }

        _BorrowedGreenlet(const OwnedGreenlet& it) :
            BorrowedReference<T>(it.borrow())
        {}

        // We get one of these for PyGreenlet, but one for PyObject
        // is handy as well
        operator PyObject*() const
        {
            return reinterpret_cast<PyObject*>(this->p);
        }
    };

    typedef _BorrowedGreenlet<PyGreenlet> BorrowedGreenlet;

    template<typename T>
    _OwnedGreenlet<T>::_OwnedGreenlet(const BorrowedGreenlet& other)
        : OwnedReference<T>(reinterpret_cast<T*>(other.borrow()))
    {
        Py_XINCREF(this->p);
    }

    // template<typename T>
    // OwnedGreenlet::OwnedGreenlet<T>(const BorrowedGreenlet& other) :
    //     OwnedReference<T>(other.acquire())
    // {}

    class BorrowedMainGreenlet : public _BorrowedGreenlet<PyMainGreenlet>
    {
    public:
        BorrowedMainGreenlet(const OwnedMainGreenlet& it) :
            _BorrowedGreenlet<PyMainGreenlet>(it.borrow())
        {}
        BorrowedMainGreenlet(PyMainGreenlet* it) : _BorrowedGreenlet<PyMainGreenlet>(it)
        {}
    };

    template<typename T>
    _OwnedGreenlet<T>& _OwnedGreenlet<T>::operator=(const BorrowedGreenlet& other)
    {
        return this->operator=(other.borrow());
    }


    class ImmortalObject : public PyObjectPointer<>
    {
    private:
        G_NO_ASSIGNMENT_OF_CLS(ImmortalObject);
    public:
        explicit ImmortalObject(PyObject* it) : PyObjectPointer<>(it)
        {
        }

        static ImmortalObject consuming(PyObject* it)
        {
            return ImmortalObject(it);
        }

        inline operator PyObject*() const
        {
            return this->p;
        }
    };

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyStr() const G_NOEXCEPT
    {
        assert(this->p);
        return OwnedObject::consuming(PyObject_Str(this->p));
    }

    template<typename T>
    inline const char* PyObjectPointer<T>::as_str() const G_NOEXCEPT
    {
        // NOTE: This is not Python exception safe.
        if (this->p) {
            OwnedObject py_str = this->PyStr();
#if PY_MAJOR_VERSION >= 3
            return PyUnicode_AsUTF8(py_str.borrow());
#else
            return PyString_AsString(py_str.borrow());
#endif
        }
        return "(nil)";
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyGetAttrString(const char* const name) const G_NOEXCEPT
    {
        assert(this->p);
        return OwnedObject::consuming(PyObject_GetAttrString(reinterpret_cast<PyObject*>(this->p), name));
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyRequireAttrString(const char* const name) const
    {
        assert(this->p);
        return OwnedObject::consuming(Require(PyObject_GetAttrString(this->p, name)));
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyCall(const BorrowedObject& arg) const G_NOEXCEPT
    {
        return this->PyCall(arg.borrow());
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyCall(PyMainGreenlet* arg) const G_NOEXCEPT
    {
        return this->PyCall(reinterpret_cast<const PyObject*>(arg));
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyCall(const PyObject* arg) const G_NOEXCEPT
    {
        assert(this->p);
        return OwnedObject::consuming(PyObject_CallFunctionObjArgs(this->p, arg, NULL));
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyCall(const BorrowedObject args,
                                                  const BorrowedObject kwargs) const G_NOEXCEPT
    {
        assert(this->p);
        return OwnedObject::consuming(PyObject_Call(this->p, args, kwargs));
    }

    template<typename T>
    inline OwnedObject PyObjectPointer<T>::PyCall(const OwnedObject& args,
                                                  const OwnedObject& kwargs) const G_NOEXCEPT
    {
        assert(this->p);
        return OwnedObject::consuming(PyObject_Call(this->p, args.borrow(), kwargs.borrow()));
    }

    class OwnedList : public OwnedObject
    {
    private:
        G_NO_ASSIGNMENT_OF_CLS(OwnedList);
    public:
        // TODO: Would like to use move.
        explicit OwnedList(const OwnedObject& other) : OwnedObject(other)
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

    // Use this to represent the module object used at module init
    // time.
    // This could either be a borrowed (Py2) or new (Py3) reference;
    // either way, we don't want to do any memory management
    // on it here, Python itself will handle that.
    // XXX: Actually, that's not quite right. On Python 3, if an
    // exception occurs before we return to the interpreter, this will
    // leak; but all previous versions also had that problem.
    class CreatedModule : public PyObjectPointer<>
    {
    private:
        G_NO_COPIES_OF_CLS(CreatedModule);
    public:
        CreatedModule(PyModuleDef& mod_def) : PyObjectPointer<>(
            Require(PyModule_Create(&mod_def)))
        {
        }

        // PyAddObject(): Add a reference to the object to the module.
        // On return, the reference count of the object is unchanged.
        //
        // The docs warn that PyModule_AddObject only steals the
        // reference on success, so if it fails after we've incref'd
        // or allocated, we're responsible for the decref.
        void PyAddObject(const char* name, const long new_bool)
        {
            // TODO: Debugging cruft that can go away
            Py_ssize_t cnt;
            PyObject* raw;
            {
            OwnedObject p = OwnedObject::consuming(Require(PyBool_FromLong(new_bool)));
            cnt = p.REFCNT();
            raw = p.borrow();
            this->PyAddObject(name, p);
            }
            // No way that raw is now invalid, even though we decref'd
            // once,
            // the module is keeping it alive
            assert(Py_REFCNT(raw) == cnt);
        }

        void PyAddObject(const char* name, const OwnedObject& new_object)
        {
            // The caller already owns a reference they will decref
            // when their variable goes out of scope, we still need to
            // incref/decref.
            this->PyAddObject(name, new_object.borrow());
        }

        void PyAddObject(const char* name, const ImmortalObject& new_object)
        {
            this->PyAddObject(name, new_object.borrow());
        }

        void PyAddObject(const char* name, const PyTypeObject& type)
        {
            this->PyAddObject(name, reinterpret_cast<const PyObject*>(&type));
        }

        void PyAddObject(const char* name, const PyObject* new_object)
        {
            Py_INCREF(new_object);
            try {
                Require(PyModule_AddObject(this->p, name, const_cast<PyObject*>(new_object)));
            }
            catch (const PyErrOccurred&) {
                Py_DECREF(p);
                throw;
            }
        }
    };

    class PyErrFetchParam : public PyObjectPointer<>
    {
        // Not an owned object, because we can't be initialized with
        // one, and we only sometimes acquire ownership.
    private:
        G_NO_COPIES_OF_CLS(PyErrFetchParam);
    public:
        // To allow declaring these and passing them to
        // PyErr_Fetch we implement the empty constructor,
        // and the address operator.
        PyErrFetchParam() : PyObjectPointer<>(nullptr)
        {
        }

        PyObject** operator&()
        {
            return &this->p;
        }

        // This allows us to pass one directly without the &,
        // BUT it has higher precedence than the bool operator
        // if it's not explicit.
        operator PyObject**()
        {
            return &this->p;
        }

        // We don't want to be able to pass these to Py_DECREF and
        // such so we don't have the implicit PyObject* conversion.

        inline PyObject* relinquish_ownership()
        {
            PyObject* result = this->p;
            this->p = nullptr;
            return result;
        }

        ~PyErrFetchParam()
        {
            Py_XDECREF(p);
        }
    };

    class OwnedErrPiece : public OwnedObject
    {
    private:

    public:
        // Unlike OwnedObject, this increments the refcount.
        OwnedErrPiece(PyObject* p=nullptr) : OwnedObject(p)
        {
            this->acquire();
        }

        PyObject** operator&()
        {
            return &this->p;
        }

        inline operator PyObject*() const
        {
            return this->p;
        }

        operator PyTypeObject*() const
        {
            return reinterpret_cast<PyTypeObject*>(this->p);
        }
    };

    class PyErrPieces
    {
    private:
        OwnedErrPiece type;
        OwnedErrPiece instance;
        OwnedErrPiece traceback;
        bool restored;
    public:
        // Takes new references; if we're destroyed before
        // restoring the error, we drop the references.
        PyErrPieces(PyObject* t, PyObject* v, PyObject* tb) :
            type(t),
            instance(v),
            traceback(tb),
            restored(0)
        {
            this->normalize();
        }

        PyErrPieces() :
            restored(0)
        {
            // PyErr_Fetch transfers ownership to us, so
            // we don't actually need to INCREF; but we *do*
            // need to DECREF if we're not restored.
            PyErrFetchParam t, v, tb;
            PyErr_Fetch(&t, &v, &tb);
            type.steal(t.relinquish_ownership());
            instance.steal(v.relinquish_ownership());
            traceback.steal(tb.relinquish_ownership());
        }

        void PyErrRestore()
        {
            // can only do this once
            assert(!this->restored);
            this->restored = true;
            PyErr_Restore(
                this->type.relinquish_ownership(),
                this->instance.relinquish_ownership(),
                this->traceback.relinquish_ownership());
            assert(!this->type && !this->instance && !this->traceback);
        }

    private:
        void normalize()
        {
            // First, check the traceback argument, replacing None,
            // with NULL
            if (traceback.is_None()) {
                traceback = nullptr;
            }

            if (traceback && !PyTraceBack_Check(traceback.borrow())) {
                PyErr_SetString(PyExc_TypeError,
                                "throw() third argument must be a traceback object");
                throw PyErrOccurred();
            }

            if (PyExceptionClass_Check(type)) {
                // If we just had a type, we'll now have a type and
                // instance.
                // The type's refcount will have gone up by one
                // because of the instance and the instance will have
                // a refcount of one. Either way, we owned, and still
                // do own, exactly one reference.
                PyErr_NormalizeException(&type, &instance, &traceback);

            }
            else if (PyExceptionInstance_Check(type)) {
                /* Raising an instance. The value should be a dummy. */
                if (instance && !instance.is_None()) {
                    PyErr_SetString(
                                    PyExc_TypeError,
                                    "instance exception may not have a separate value");
                    throw PyErrOccurred();
                }
                /* Normalize to raise <class>, <instance> */
                this->instance = this->type;
#ifndef NDEBUG
                Py_ssize_t type_count = Py_REFCNT(Py_TYPE(instance.borrow()));
#endif
                this->type = PyExceptionInstance_Class(instance.borrow());
                assert(type.REFCNT() == type_count + 1);
            }
            else {
                /* Not something you can raise. throw() fails. */
                PyErr_Format(PyExc_TypeError,
                     "exceptions must be classes, or instances, not %s",
                             Py_TYPE(type.borrow())->tp_name);
                throw PyErrOccurred();
            }
        }
    };

    // PyArg_Parse's O argument returns a borrowed reference.
    class PyArgParseParam : public BorrowedObject
    {
    private:
        G_NO_COPIES_OF_CLS(PyArgParseParam);
    public:
        explicit PyArgParseParam(PyObject* p=nullptr) : BorrowedObject(p)
        {
        }

        inline PyObject** operator&()
        {
            return &this->p;
        }
    };

};};

#endif
