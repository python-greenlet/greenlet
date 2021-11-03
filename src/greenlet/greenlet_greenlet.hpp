#ifndef GREENLET_GREENLET_HPP
#define GREENLET_GREENLET_HPP
/*
 * Declarations of the core data structures.
*/

#include <Python.h>
//#include "greenlet_internal.hpp"
#include "greenlet_compiler_compat.hpp"
#include "greenlet_refs.hpp"
#include "greenlet_cpython_compat.hpp"

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

    template<typename T>
    void operator<<(const PyThreadState *const tstate, T& exc);

    class PythonState
    {
    private:
        G_NO_COPIES_OF_CLS(PythonState);
        /* weak reference (suspended) or NULL (running) */
        struct _frame* _top_frame;

#if GREENLET_PY37
        OwnedObject _context;
#endif
#if  GREENLET_USE_CFRAME
        CFrame* cframe;
        int use_tracing;
#endif
        int recursion_depth;
    public:
        PythonState();
        bool has_top_frame() const G_NOEXCEPT;
        PyObject* top_frame() const G_NOEXCEPT;
#if GREENLET_PY37
        OwnedObject& context();
#endif
        void operator<<(const PyThreadState *const tstate) G_NOEXCEPT;
        void operator>>(PyThreadState* tstate) G_NOEXCEPT;
        void clear() G_NOEXCEPT;

        int tp_traverse(visitproc visit, void* arg, bool visit_top_frame) G_NOEXCEPT;
        void tp_clear(bool own_top_frame) G_NOEXCEPT;
        void set_initial_state(const PyThreadState* const tstate) G_NOEXCEPT;
#if GREENLET_USE_CFRAME
        void set_new_cframe(CFrame& frame) G_NOEXCEPT;
#endif
        void will_switch_from(PyThreadState *const origin_tstate) G_NOEXCEPT;
    };

    class StackState
    {
        // By having only plain C (POD) members, no virtual functions
        // or bases, we get a trivial assignment operator generated
        // for us.
    private:
        char* _stack_start;
        char* stack_stop;
        char* stack_copy;
        intptr_t _stack_saved;
        StackState* stack_prev;
        int copy_stack_to_heap_up_to(const char* const stop);
    public:
        /**
         * Creates a started, but inactive, state.
         */
        StackState(void* mark, StackState& current);
        /**
         * Creates an inactive, unstarted, state.
         */
        StackState();

        inline void copy_heap_to_stack(const StackState& current) G_NOEXCEPT;
        inline int copy_stack_to_heap(char* const stackref, const StackState& current) G_NOEXCEPT;
        inline bool started() const G_NOEXCEPT;
        inline bool main() const G_NOEXCEPT;
        inline bool active() const G_NOEXCEPT;
        inline void set_active() G_NOEXCEPT;
        inline void set_inactive() G_NOEXCEPT;
        inline intptr_t stack_saved() const G_NOEXCEPT;
        inline char* stack_start() const G_NOEXCEPT;
        static inline StackState make_main() G_NOEXCEPT;
    };
};

template<typename T>
void greenlet::operator<<(const PyThreadState *const tstate, T& exc)
{
    exc.operator<<(tstate);
}

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


using greenlet::PythonState;

PythonState::PythonState()
{
#if GREENLET_USE_CFRAME
    /*
      The PyThreadState->cframe pointer usually points to memory on
      the stack, alloceted in a call into PyEval_EvalFrameDefault.

      Initially, before any evaluation begins, it points to the
      initial PyThreadState object's ``root_cframe`` object, which is
      statically allocated for the lifetime of the thread.

      A greenlet can last for longer than a call to
      PyEval_EvalFrameDefault, so we can't set its ``cframe`` pointer
      to be the current ``PyThreadState->cframe``; nor could we use
      one from the greenlet parent for the same reason. Yet a further
      no: we can't allocate one scoped to the greenlet and then
      destroy it when the greenlet is deallocated, because inside the
      interpreter the CFrame objects form a linked list, and that too
      can result in accessing memory beyond its dynamic lifetime (if
      the greenlet doesn't actually finish before it dies, its entry
      could still be in the list).

      Using the ``root_cframe`` is problematic, though, because its
      members are never modified by the interpreter and are set to 0,
      meaning that its ``use_tracing`` flag is never updated. We don't
      want to modify that value in the ``root_cframe`` ourself: it
      *shouldn't* matter much because we should probably never get
      back to the point where that's the only cframe on the stack;
      even if it did matter, the major consequence of an incorrect
      value for ``use_tracing`` is that if its true the interpreter
      does some extra work --- however, it's just good code hygiene.

      Our solution: before a greenlet runs, after its initial
      creation, it uses the ``root_cframe`` just to have something to
      put there. However, once the greenlet is actually switched to
      for the first time, ``g_initialstub`` (which doesn't actually
      "return" while the greenlet is running) stores a new CFrame on
      its local stack, and copies the appropriate values from the
      currently running CFrame; this is then made the CFrame for the
      newly-minted greenlet. ``g_initialstub`` then proceeds to call
      ``glet.run()``, which results in ``PyEval_...`` adding the
      CFrame to the list. Switches continue as normal. Finally, when
      the greenlet finishes, the call to ``glet.run()`` returns and
      the CFrame is taken out of the linked list and the stack value
      is now unused and free to expire.

      XXX: I think we can do better. If we're deallocing in the same
      thread, can't we traverse the list and unlink our frame?
      Can we just keep a reference to the thread state in case we
      dealloc in another thread? (Is that even possible if we're still
      running and haven't returned from g_initialstub?)
    */
    this->cframe = &PyThreadState_GET()->root_cframe;
#endif
}

void PythonState::operator<<(const PyThreadState *const tstate) G_NOEXCEPT
{
    this->recursion_depth = tstate->recursion_depth;
    this->_top_frame = tstate->frame;
#if GREENLET_PY37
    this->_context.steal(tstate->context);
#endif
#if GREENLET_USE_CFRAME
    /*
      IMPORTANT: ``cframe`` is a pointer into the STACK. Thus, because
      the call to ``slp_switch()`` changes the contents of the stack,
      you cannot read from ``ts_current->cframe`` after that call and
      necessarily get the same values you get from reading it here.
      Anything you need to restore from now to then must be saved in a
      global/threadlocal variable (because we can't use stack
      variables here either). For things that need to persist across
      the switch, use `will_switch_from`.
    */
    this->cframe = tstate->cframe;
    this->use_tracing = tstate->cframe->use_tracing;
#endif
}

void PythonState::operator>>(PyThreadState *const tstate) G_NOEXCEPT
{
    tstate->recursion_depth = this->recursion_depth;
    tstate->frame = this->_top_frame;
    this->_top_frame = nullptr;

#if GREENLET_PY37
    tstate->context = this->_context.relinquish_ownership();
    this->_context = nullptr;
    /* Incrementing this value invalidates the contextvars cache,
       which would otherwise remain valid across switches */
    tstate->context_ver++;
#endif
#if GREENLET_USE_CFRAME
    tstate->cframe = this->cframe;
    /*
      If we were tracing, we need to keep tracing.
      There should never be the possibility of hitting the
      root_cframe here. See note above about why we can't
      just copy this from ``origin->cframe->use_tracing``.
    */
    tstate->cframe->use_tracing = this->use_tracing;
#endif
}

void PythonState::will_switch_from(PyThreadState *const origin_tstate) G_NOEXCEPT
{
#if GREENLET_USE_CFRAME
    // The weird thing is, we don't actually save this for an
    // effect on the current greenlet, it's saved for an
    // effect on the target greenlet. That is, we want
    // continuity of this setting across the greenlet switch.
    this->use_tracing = origin_tstate->cframe->use_tracing;
#endif
}

void PythonState::set_initial_state(const PyThreadState* const tstate) G_NOEXCEPT
{
    this->_top_frame = nullptr;
    this->recursion_depth = tstate->recursion_depth;
}
// TODO: Better state management about when we own the top frame.
int PythonState::tp_traverse(visitproc visit, void* arg, bool own_top_frame) G_NOEXCEPT
{
#if GREENLET_PY37
    Py_VISIT(this->_context.borrow());
#endif
    if (own_top_frame) {
        Py_VISIT(this->_top_frame);
    }
    return 0;
}

void PythonState::tp_clear(bool own_top_frame) G_NOEXCEPT
{
#if GREENLET_PY37
    this->_context.CLEAR();
#endif
    // If we get here owning a frame,
    // we got dealloc'd without being finished. We may or may not be
    // in the same thread.
    if (own_top_frame) {
        Py_CLEAR(this->_top_frame);
    }
}

#if GREENLET_USE_CFRAME
void PythonState::set_new_cframe(CFrame& frame) G_NOEXCEPT
{
    frame = *PyThreadState_GET()->cframe;
    /* Make the target greenlet refer to the stack value. */
    this->cframe = &frame;
    /*
      And restore the link to the previous frame so this one gets
      unliked appropriately.
    */
    this->cframe->previous = &PyThreadState_GET()->root_cframe;
}
#endif

bool PythonState::has_top_frame() const G_NOEXCEPT
{
    return this->_top_frame != nullptr;
}

PyObject* PythonState::top_frame() const G_NOEXCEPT
{
    return reinterpret_cast<PyObject*>(this->_top_frame);
}

#if GREENLET_PY37
OwnedObject& PythonState::context()
{
    return this->_context;
}
#endif

using greenlet::StackState;

StackState::StackState(void* mark, StackState& current)
    : _stack_start(nullptr),
      stack_stop((char*)mark),
      stack_copy(nullptr),
      _stack_saved(0)
{
    if (!current._stack_start) {
        /* ts_current is dying */
        this->stack_prev = current.stack_prev;
    }
    else {
        this->stack_prev = &current;
    }
}

StackState::StackState()
    : _stack_start(nullptr),
      stack_stop(nullptr),
      stack_copy(nullptr),
      _stack_saved(0)
{}

inline void StackState::copy_heap_to_stack(const StackState& current) G_NOEXCEPT
{
    /* Restore the heap copy back into the C stack */
    if (this->_stack_saved != 0) {
        memcpy(this->_stack_start, this->stack_copy, this->_stack_saved);
        PyMem_Free(this->stack_copy);
        this->stack_copy = nullptr;
        this->_stack_saved = 0;
    }
    StackState* owner = const_cast<StackState*>(&current);
    if (owner->_stack_start == NULL) {
        owner = owner->stack_prev; /* greenlet is dying, skip it */
    }
    while (owner && owner->stack_stop <= this->stack_stop) {
        owner = owner->stack_prev; /* find greenlet with more stack */
    }
    this->stack_prev = owner;
}

int StackState::copy_stack_to_heap_up_to(const char* const stop)
{
    /* Save more of g's stack into the heap -- at least up to 'stop'
       g->stack_stop |________|
                     |        |
                     |    __ stop       . . . . .
                     |        |    ==>  .       .
                     |________|          _______
                     |        |         |       |
                     |        |         |       |
      g->stack_start |        |         |_______| g->stack_copy
     */
    intptr_t sz1 = this->_stack_saved;
    intptr_t sz2 = stop - this->_stack_start;
    assert(this->_stack_start != NULL);
    if (sz2 > sz1) {
        char* c = (char*)PyMem_Realloc(this->stack_copy, sz2);
        if (!c) {
            PyErr_NoMemory();
            return -1;
        }
        memcpy(c + sz1, this->_stack_start + sz1, sz2 - sz1);
        this->stack_copy = c;
        this->_stack_saved = sz2;
    }
    return 0;
}

inline int StackState::copy_stack_to_heap(char* const stackref,
                                          const StackState& current) G_NOEXCEPT
{
    /* must free all the C stack up to target_stop */
    const char* const target_stop = this->stack_stop;

    StackState* owner = const_cast<StackState*>(&current);
    assert(owner->_stack_saved == 0); // everything is present on the stack
    if (owner->_stack_start == nullptr) {
        owner = owner->stack_prev; /* not saved if dying */
    }
    else {
        owner->_stack_start = stackref;
    }

    while (owner->stack_stop < target_stop) {
        /* ts_current is entierely within the area to free */
        if (owner->copy_stack_to_heap_up_to(owner->stack_stop)) {
            return -1; /* XXX */
        }
        owner = owner->stack_prev;
    }
    if (owner != this) {
        if (owner->copy_stack_to_heap_up_to(target_stop)) {
            return -1; /* XXX */
        }
    }
    return 0;
}

inline bool StackState::started() const G_NOEXCEPT
{
    return this->stack_stop != nullptr;
}

inline bool StackState::main() const G_NOEXCEPT
{
    return this->stack_stop == (char*)-1;
}

inline bool StackState::active() const G_NOEXCEPT
{
    return this->_stack_start != nullptr;
}

inline void StackState::set_active() G_NOEXCEPT
{
    assert(this->_stack_start == nullptr);
    this->_stack_start = (char*)1;
}

inline void StackState::set_inactive() G_NOEXCEPT
{
    this->_stack_start = nullptr;
    // XXX: What if we still have memory out there?
    // That case is actually triggered by
    // test_issue251_issue252_explicit_reference_not_collectable (greenlet.tests.test_leaks.TestLeaks)
    // and
    // test_issue251_issue252_need_to_collect_in_background (greenlet.tests.test_leaks.TestLeaks)
}

inline intptr_t StackState::stack_saved() const G_NOEXCEPT
{
    return this->_stack_saved;
}

inline char* StackState::stack_start() const G_NOEXCEPT
{
    return this->_stack_start;
}


inline StackState StackState::make_main() G_NOEXCEPT
{
    StackState s;
    s._stack_start = (char*)1;
    s.stack_stop = (char*)-1;
    return s;
}



#endif
