#ifndef GREENLET_GREENLET_HPP
#define GREENLET_GREENLET_HPP
/*
 * Declarations of the core data structures.
*/

#include <Python.h>
//#include "greenlet_internal.hpp"
#include "greenlet_compiler_compat.hpp"
#include "greenlet_refs.hpp"

using greenlet::refs::OwnedObject;

namespace greenlet
{
    class ExceptionState
    {
    private:
        G_NO_COPIES_OF_CLS(ExceptionState);

#if PY_VERSION_HEX >= 0x030700A3
        // Even though these are borrowed objects, we actually own
        // them, when they're not null.
        // XXX: Express that in the API.
    private:
        _PyErr_StackItem* exc_info;
        _PyErr_StackItem exc_state;
#else
        OwnedObject exc_type;
        OwnedObject exc_value;
        OwnedObject exc_traceback;
#endif
    public:
        ExceptionState();
        void operator<<(const PyThreadState *const tstate) G_NOEXCEPT;
        void operator>>(PyThreadState* tstate) G_NOEXCEPT;
        void clear() G_NOEXCEPT;

        int tp_traverse(visitproc visit, void* arg) G_NOEXCEPT;
        void tp_clear() G_NOEXCEPT;
    };

    void operator<<(const PyThreadState *const tstate, ExceptionState& exc);
};

using greenlet::ExceptionState;

ExceptionState::ExceptionState()
{
    this->clear();
}

#if PY_VERSION_HEX >= 0x030700A3
// ******** Python 3.7 and above *********
void ExceptionState::operator<<(const PyThreadState *const tstate) G_NOEXCEPT
{
    this->exc_info = tstate->exc_info;
    this->exc_state = tstate->exc_state;
}

void ExceptionState::operator>>(PyThreadState *const tstate) G_NOEXCEPT
{
    tstate->exc_state = this->exc_state;
    tstate->exc_info =
        this->exc_info ? this->exc_info : &tstate->exc_state;
    this->clear();
}

void ExceptionState::clear() G_NOEXCEPT
{
    this->exc_info = nullptr;
    this->exc_state.exc_type = nullptr;
    this->exc_state.exc_value = nullptr;
    this->exc_state.exc_traceback = nullptr;
    this->exc_state.previous_item = nullptr;
}

int ExceptionState::tp_traverse(visitproc visit, void* arg) G_NOEXCEPT
{
    Py_VISIT(this->exc_state.exc_type);
    Py_VISIT(this->exc_state.exc_value);
    Py_VISIT(this->exc_state.exc_traceback);
    return 0;
}

void ExceptionState::tp_clear() G_NOEXCEPT
{
    Py_CLEAR(this->exc_state.exc_type);
    Py_CLEAR(this->exc_state.exc_value);
    Py_CLEAR(this->exc_state.exc_traceback);
}
#else
// ********** Python 3.6 and below ********
void ExceptionState::operator<<(const PyThreadState *const tstate) G_NOEXCEPT
{
    this->exc_type.steal(tstate->exc_type);
    this->exc_value.steal(tstate->exc_value);
    this->exc_traceback.steal(tstate->exc_traceback);
}

void ExceptionState::operator>>(PyThreadState *const tstate) G_NOEXCEPT
{
    tstate->exc_type <<= this->exc_type;
    tstate->exc_value <<= this->exc_value;
    tstate->exc_traceback <<= this->exc_traceback;
    this->clear();
}

void ExceptionState::clear() G_NOEXCEPT
{
    this->exc_type = nullptr;
    this->exc_value = nullptr;
    this->exc_traceback = nullptr;
}

int ExceptionState::tp_traverse(visitproc visit, void* arg) G_NOEXCEPT
{
    Py_VISIT(this->exc_type.borrow());
    Py_VISIT(this->exc_value.borrow());
    Py_VISIT(this->exc_traceback.borrow());
    return 0;
}

void ExceptionState::tp_clear() G_NOEXCEPT
{
    this->exc_type.CLEAR();
    this->exc_value.CLEAR();
    this->exc_traceback.CLEAR();
}
#endif

void greenlet::operator<<(const PyThreadState *const tstate, ExceptionState& exc)
{
    exc.operator<<(tstate);
}


#endif
