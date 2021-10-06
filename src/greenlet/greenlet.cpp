/* -*- indent-tabs-mode: nil; tab-width: 4; -*- */
/* Format with:
 *  clang-format -i --style=file src/greenlet/greenlet.c
 *
 *
 * Fix missing braces with:
 *   clang-tidy src/greenlet/greenlet.c -fix -checks="readability-braces-around-statements"
*/
#include "greenlet_internal.hpp"
#include "greenlet_thread_state.hpp"
#include "greenlet_thread_support.hpp"
using greenlet::ThreadState;



#include "structmember.h"

#ifdef __clang__
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wunused-parameter"
#    pragma clang diagnostic ignored "-Wmissing-field-initializers"
#    pragma clang diagnostic ignored "-Wwritable-strings"
#elif defined(__GNUC__)
#    pragma GCC diagnostic push
//  warning: ISO C++ forbids converting a string constant to ‘char*’
// (The python APIs aren't const correct and accept writable char*)
#    pragma GCC diagnostic ignored "-Wwrite-strings"
#endif


/***********************************************************

A PyGreenlet is a range of C stack addresses that must be
saved and restored in such a way that the full range of the
stack contains valid data when we switch to it.

Stack layout for a greenlet:

               |     ^^^       |
               |  older data   |
               |               |
  stack_stop . |_______________|
        .      |               |
        .      | greenlet data |
        .      |   in stack    |
        .    * |_______________| . .  _____________  stack_copy + stack_saved
        .      |               |     |             |
        .      |     data      |     |greenlet data|
        .      |   unrelated   |     |    saved    |
        .      |      to       |     |   in heap   |
 stack_start . |     this      | . . |_____________| stack_copy
               |   greenlet    |
               |               |
               |  newer data   |
               |     vvv       |


Note that a greenlet's stack data is typically partly at its correct
place in the stack, and partly saved away in the heap, but always in
the above configuration: two blocks, the more recent one in the heap
and the older one still in the stack (either block may be empty).

Greenlets are chained: each points to the previous greenlet, which is
the one that owns the data currently in the C stack above my
stack_stop.  The currently running greenlet is the first element of
this chain.  The main (initial) greenlet is the last one.  Greenlets
whose stack is entirely in the heap can be skipped from the chain.

The chain is not related to execution order, but only to the order
in which bits of C stack happen to belong to greenlets at a particular
point in time.

The main greenlet doesn't have a stack_stop: it is responsible for the
complete rest of the C stack, and we don't know where it begins.  We
use (char*) -1, the largest possible address.

States:
  stack_stop == NULL && stack_start == NULL:  did not start yet
  stack_stop != NULL && stack_start == NULL:  already finished
  stack_stop != NULL && stack_start != NULL:  active

The running greenlet's stack_start is undefined but not NULL.

 ***********************************************************/

/*** global state ***/

/* In the presence of multithreading, this is a bit tricky:

   - ts_current always store a reference to a greenlet, but it is
     not really the current greenlet after a thread switch occurred.

   - each *running* greenlet uses its run_info field to know which
     thread it is attached to.  A greenlet can only run in the thread
     where it was created.  This run_info is a ref to tstate->dict.

   - the thread state dict is used to save and restore ts_current,
     using the dictionary key 'ts_curkey'.
*/

extern PyTypeObject PyGreenlet_Type;


static PyObject* ts_event_switch;
static PyObject* ts_event_throw;
static PyObject* PyExc_GreenletError;
static PyObject* PyExc_GreenletExit;
static PyObject* ts_empty_tuple;
static PyObject* ts_empty_dict;

//#define _GDPoPrint(o) do {if (!PyErr_Occurred()) {PyObject_Print(o, stderr, 0);} else { fprintf(stderr, "ERROR:"); PyErr_Print(); } } while(0)
//#define _GDPrint(...) fprintf(stderr, __VA_ARGS__)

// static void _GDPoPrint(void* o)
// {
//     if (PyErr_Occurred()) {
//         PyObject *t, *v, *tb;
//         // Clears the internal values; we own a reference to them.
//         PyErr_Fetch(&t, &v, &tb);
//         PyErr_NormalizeException(&t, &v, &tb);
//         Py_XINCREF(t);
//         Py_XINCREF(v);
//         Py_XINCREF(tb);
//         fprintf(stderr, "ERROR SET: (t=%p v=%p tb=%p) %s",
//                 t, v, tb,
//                 v ? v->ob_type->tp_name : "NOPE");
//         // We need to put the values back to print them; this takes
//         // away one of our references.
//         PyErr_Restore(t, v, tb);
//         // This clears the state
//         PyErr_WriteUnraisable((PyObject*)o);
//         fprintf(stderr, " (printing: ");
//         PyObject_Print((PyObject*)o, stderr, 0);
//         fprintf(stderr, ") ");
//         // steal our other reference.
//         PyErr_Restore(t, v, tb);
//     }
//     else {
//         PyObject_Print((PyObject*)o, stderr, 0);
//     }
// }



//typedef std::vector<PyGreenlet*> g_deleteme_t;

#undef _GDPrint
#define _GDPrint(...)
#define _GDPoPrint(...)



//#define _GDPoPrint(o)



// The intent when this is used multiple times in a function is to
// take a reference to it in a local variable, to avoid the
// thread-local indirection. On some platforms (macOS),
// accessing a thread-local involves a function call (plus an initial
// function call in each function that uses a thread local); in
// contrast, static volatile variables are at some pre-computed offset.
//static  ThreadState g_thread_state_global;

// RECALL: legacy thread-local objects (__thread on GCC, __declspec(thread) on
// MSVC) can't have destructors.

static G_MUTEX_TYPE g_cleanup_queue_lock;

static int
_ThreadStateCreator_Destroy(PyMainGreenlet* main)
{
    // Holding the GIL.
    // Passed a borrowed reference to the main greenlet.
    // main greenlet -> state -> main greenlet
    // main greenlet -> main greenlet
    ThreadState* s = main->thread_state;
    _GDPrint("PendingCall: Destroying main greenlet %p (refcount %ld)\n", main, Py_REFCNT(main));
    // When we need to do cross-thread operations, we check this.
    // A NULL value means the thread died some time ago.
    main->thread_state = NULL;
    _GDPrint("\tSet thread state to null\n");
    delete s; // Deleting this runs the constructor, DECREFs the main greenlet.
    return 0;
}

#if G_USE_STANDARD_THREADING == 1
// We can't use the PythonAllocator for this, because we push to it
// from the thread state destructor, which doesn't have the GIL,
// and Python's allocators can only be called with the GIL.
static std::vector<PyMainGreenlet*> g_cleanup_queue;

static int
_ThreadStateCreator_DestroyAll(void* arg)
{
    // We're holding the GIL here, so no Python code should be able to
    // run to call ``os.fork()``. (It's important not to be holding
    // this lock when some other thread forks, so try to keep it
    // short.)
    // TODO: On platforms that support it, use ``pthread_atform`` to
    // drop this lock.
    while (1) {
        PyMainGreenlet* to_destroy;
        {
            G_MUTEX_ACQUIRE(g_cleanup_queue_lock);
            _GDPrint("Processing %ld cleanups\n", g_cleanup_queue.size());
            if (g_cleanup_queue.empty()) {
                G_MUTEX_RELEASE(g_cleanup_queue_lock);
                break;
            }
            to_destroy = g_cleanup_queue.back();
            g_cleanup_queue.pop_back();
            G_MUTEX_RELEASE(g_cleanup_queue_lock);
        }
        // Drop the lock while we do the actual deletion.
        _ThreadStateCreator_Destroy(to_destroy);
    }
    return 0;
}

static void
ThreadStateCreator_Destroy(ThreadState* state)
{
    // We are *NOT* holding the GIL. Our thread is in the middle
    // of dieing and the Python thread state is already gone so we
    // can't use most Python APIs. One that is safe is There is a
    // limited number of calls that can be queued: 32
    // (NPENDINGCALLS) in CPython 3.10. We should probably look
    // into some way to coalesce these calls.
    G_MUTEX_ACQUIRE(g_cleanup_queue_lock);

    if (state && state->has_main_greenlet()) {
        // Because we don't have the GIL, this is a race condition.
        if (!PyInterpreterState_Head()) {
            _GDPrint("\tInterp torn down\n");
            // We have to leak the thread state, if the
            // interpreter has shut down when we're getting
            // deallocated, we can't run the cleanup code that
            // deleting it would imply.
            G_MUTEX_RELEASE(g_cleanup_queue_lock);
            return;
        }

        g_cleanup_queue.push_back(state->borrow_main_greenlet());
        if (g_cleanup_queue.size() == 1) {
            // We added the first item to the queue. We need to schedule
            // the cleanup.
            int result = Py_AddPendingCall(_ThreadStateCreator_DestroyAll,
                                           NULL);
            if (result < 0) {
                _GDPrint("OH NO FAILED TO ADD PENDING CLEANUP.\n");
            }
        }
        else {
            _GDPrint("NEAT. Added to existing pending call.");
        }
        G_MUTEX_RELEASE(g_cleanup_queue_lock);
    }
}
#else
static class _CleanupQueue {
public:
    ssize_t size() { return 0; };
} g_cleanup_queue;
#endif


class ThreadStateCreator
{
private:
    ThreadState* _state;
#if G_HAS_METHOD_DELETE == 0
    ThreadStateCreator(const ThreadStateCreator& other);
    ThreadStateCreator& operator=(const ThreadStateCreator& other);
#endif
public:

    // Only one of these, auto created per thread
#if G_HAS_METHOD_DELETE == 1
    ThreadStateCreator(const ThreadStateCreator& other) = delete;
    ThreadStateCreator& operator=(const ThreadStateCreator& other) = delete;
#endif

    ThreadStateCreator() :
        _state(0)
    {
    }

    ~ThreadStateCreator()
    {
#if G_USE_STANDARD_THREADING == 1
        ThreadStateCreator_Destroy(this->_state);
#else
        // if we're not using standard threading, we're using
        // the Python thread-local dictionary to perform our cleanup,
        // which means we're deallocated when holding the GIL. The
        // thread state is valide enough still for us to destroy
        // stuff.
        if (this->_state && this->_state->borrow_main_greenlet()) {
            _ThreadStateCreator_Destroy(this->_state->borrow_main_greenlet());
        }
#endif
    }


    inline ThreadState& state()
    {
        // The main greenlet will own this pointer when it is created,
        // which will be right after this. The plan is to give every
        // greenlet a pointer to the main greenlet for the thread it
        // runs in; if we are doing something cross-thread, we need to
        // access the pointer from the main greenlet. Deleting the
        // thread, and hence the thread-local storage, will delete the
        // state pointer in the main greenlet.
        if (!this->_state) {
            // XXX: Assumming allocation never fails
            this->_state = new ThreadState;
            // For non-standard threading, we need to store an object
            // in the Python thread state dictionary so that it can be
            // DECREF'd when the thread ends (ideally; the dict could
            // last longer) and clean this object up.
        }
        return *this->_state;
    }

    operator ThreadState&()
    {
        return this->state();
    }

    // shadow API to make this easier to use.
    inline PyGreenlet* borrow_target()
    {
        return this->state().borrow_target();
    };
    inline PyGreenlet* borrow_current()
    {
        return this->state().borrow_current();
    };
    inline PyGreenlet* borrow_origin()
    {
        return this->state().borrow_origin();
    };
    inline PyMainGreenlet* borrow_main_greenlet()
    {
        return this->state().borrow_main_greenlet();
    };

};

#if G_USE_STANDARD_THREADING == 1
static G_THREAD_LOCAL_VAR ThreadStateCreator g_thread_state_global;
#define GET_THREAD_STATE() g_thread_state_global
#else
// Define a Python object that goes in the Python thread state dict
// when the greenlet thread state is created, and which owns the
// reference to the greenlet thread local state.
// When the thread state dict is cleaned up, so too is the thread
// state. This works best if we make sure there are no circular
// references to the thread state.
typedef struct _PyGreenletCleanup {
    PyObject_HEAD
    ThreadStateCreator* thread_state_creator;
} PyGreenletCleanup;

static void
cleanup_dealloc(PyGreenletCleanup* self)
{
    PyObject_GC_UnTrack(self);
    ThreadStateCreator* tmp = self->thread_state_creator;
    self->thread_state_creator = NULL;
    if (tmp) {
        delete tmp;
    }

}

static int
cleanup_clear(PyGreenletCleanup* self)
{
    Py_CLEAR(self->thread_state_creator);
    return 0;
}

static int
cleanup_traverse(PyGreenletCleanup* self, visitproc visit, void* arg)
{
    // TODO: Anything we should traverse? Probably yes.
    return 0;
}

static int
cleanup_is_gc(PyGreenlet* self)
{
    return 1;
}

static PyTypeObject PyGreenletCleanup_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "greenlet._greenlet.ThreadStateCleanup",
    sizeof(struct _PyGreenletCleanup),
    0,                   /* tp_itemsize */
    /* methods */
    (destructor)cleanup_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    0,                         /* tp_repr */
    0,                         /* tp_as _number*/
    0,                         /* tp_as _sequence*/
    0,                         /* tp_as _mapping*/
    0,                         /* tp_hash */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer*/
    G_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    "Internal use only",                        /* tp_doc */
    (traverseproc)cleanup_traverse, /* tp_traverse */
    (inquiry)cleanup_clear,         /* tp_clear */
    0,                                  /* tp_richcompare */
    // XXX: Don't our flags promise a weakref?
    0,                           /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    0,                      /* tp_methods */
    0,                      /* tp_members */
    0,                      /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,         /* tp_dictoffset */
    0,               /* tp_init */
    PyType_GenericAlloc,                  /* tp_alloc */
    PyType_GenericNew,                          /* tp_new */
    PyObject_GC_Del,                   /* tp_free */
    (inquiry)cleanup_is_gc,         /* tp_is_gc */
};
static G_THREAD_LOCAL_VAR ThreadStateCreator* _g_thread_state_global_ptr = nullptr;
static ThreadStateCreator& GET_THREAD_STATE()
{
    if (!_g_thread_state_global_ptr) {
        // NOTE: If any of this fails, we'll probably go on to hard
        // crash the process, because we're returning a reference to a
        // null pointer. we've called Py_FatalError(), but have no way
        // to commuticate that to the user. Since these should
        // essentially never fail unless the entire process is borked,
        // a hard crash with a decent C backtrace is much more useful.
        _g_thread_state_global_ptr = new ThreadStateCreator();
        if (!_g_thread_state_global_ptr) {
            Py_FatalError("greenlet: Failed to create greenlet thread state.");
        }
        else {
            PyGreenletCleanup* cleanup = (PyGreenletCleanup*)PyType_GenericAlloc(&PyGreenletCleanup_Type, 0);
            if (!cleanup) {
                Py_FatalError("greenlet: Failed to create greenlet thread state cleanup.");
            }
            else {
                cleanup->thread_state_creator = _g_thread_state_global_ptr;
#if PY_MAJOR_VERSION >= 3
                assert(PyObject_GC_IsTracked((PyObject*)cleanup));
#else
                assert(_PyObject_GC_IS_TRACKED(cleanup));
#endif
                PyObject* ts_dict = PyThreadState_GetDict();
                if (!ts_dict) {
                    Py_FatalError("greenlet: Failed to get Python thread state.");
                }
                else {
                    if (PyDict_SetItemString(ts_dict, "__greenlet_cleanup", (PyObject*)cleanup) < 0) {
                        Py_FatalError("greenlet: Failed to save cleanup key in Python thread state.");
                    }
                    else {
                        // The dict owns the reference now.
                        Py_DECREF(cleanup);
                    }
                }
            }
        }
    }
    return *_g_thread_state_global_ptr;
}
#endif



/***********************************************************/
/* Thread-aware routines, switching global variables when needed */



static void
green_clear_exc(PyGreenlet* g)
{
#if GREENLET_PY37
    g->exc_info = NULL;
    g->exc_state.exc_type = NULL;
    g->exc_state.exc_value = NULL;
    g->exc_state.exc_traceback = NULL;
    g->exc_state.previous_item = NULL;
#else
    g->exc_type = NULL;
    g->exc_value = NULL;
    g->exc_traceback = NULL;
#endif
}

static PyMainGreenlet*
green_create_main(void)
{
    PyMainGreenlet* gmain;

    /* create the main greenlet for this thread */
    gmain = (PyMainGreenlet*)PyType_GenericAlloc(&PyMainGreenlet_Type, 0);
    if (gmain == NULL) {
        return NULL;
    }
    gmain->super.stack_start = (char*)1;
    gmain->super.stack_stop = (char*)-1;
    // circular reference; the pending call will clean this up.
    gmain->super.main_greenlet_s = gmain;
    Py_INCREF(gmain);
    assert(Py_REFCNT(gmain) == 2);
    return gmain;
}


static PyMainGreenlet*
find_and_borrow_main_greenlet_in_lineage(PyGreenlet* g)
{
    while (!PyGreenlet_STARTED(g)) {
        g = g->parent;
        if (g == NULL) {
            /* garbage collected greenlet in chain */
            // XXX: WHAT?
            return NULL;
        }
    }
    // XXX: What about the actual main greenlet?
    return g->main_greenlet_s;
}

/***********************************************************/

/* Some functions must not be inlined:
   * slp_restore_state, when inlined into slp_switch might cause
     it to restore stack over its own local variables
   * slp_save_state, when inlined would add its own local
     variables to the saved stack, wasting space
   * slp_switch, cannot be inlined for obvious reasons
   * g_initialstub, when inlined would receive a pointer into its
     own stack frame, leading to incomplete stack save/restore
*/


#ifdef GREENLET_NOINLINE_SUPPORTED
/* add forward declarations */
static GREENLET_NOINLINE_P(PyObject*, g_switch_finish)(int);
static int GREENLET_NOINLINE(g_initialstub)(void*);
static void GREENLET_NOINLINE(g_switchstack_success)(void);

extern "C" {
    static void GREENLET_NOINLINE(slp_restore_state)(void);
    static int GREENLET_NOINLINE(slp_save_state)(char*, ThreadState&);
#    if !(defined(MS_WIN64) && defined(_M_X64))
    static int GREENLET_NOINLINE(slp_switch)(void);
#    endif
};
#    define GREENLET_NOINLINE_INIT() \
        do {                         \
        } while (0)
#else
/* force compiler to call functions via pointers */
/* XXX: Do we even want/need to support such compilers? This code path
   is untested on CI. */
static int (*g_initialstub)(void*);
static PyObject* (*g_switch_finish)(int err);
static void (*g_switchstack_success)(void);

extern "C" {
    static void (*slp_restore_state)(void);
    static int (*slp_save_state)(char*, ThreadState&);
    static int (*slp_switch)(void);
};
#    define GREENLET_NOINLINE(name) cannot_inline_##name
#    define GREENLET_NOINLINE_P(rtype, name) rtype cannot_inline_##name
#    define GREENLET_NOINLINE_INIT()                                  \
        do {                                                          \
            slp_restore_state = GREENLET_NOINLINE(slp_restore_state); \
            slp_save_state = GREENLET_NOINLINE(slp_save_state);       \
            slp_switch = GREENLET_NOINLINE(slp_switch);               \
            g_initialstub = GREENLET_NOINLINE(g_initialstub);         \
            g_switch_finish = GREENLET_NOINLINE(g_switch_finish);           \
            g_switchstack_success = GREENLET_NOINLINE(g_switchstack_success);           \
        } while (0)
#endif



/*
 * the following macros are spliced into the OS/compiler
 * specific code, in order to simplify maintenance.
 */
// We can save about 10% of the time it takes to switch greenlets if
// we thread the thread state through the slp_save_state() and the
// following slp_restore_state() calls from
// slp_switch()->g_switchstack() (which already needs to access it).
//
// However:
//
// that requires changing the prototypes and implementations of the
// switching functions. If we just change the prototype of
// slp_switch() to accept the argument and update the macros, without
// changing the implementation of slp_switch(), we get crashes on
// 64-bit Linux and 32-bit x86 (for reasons that aren't 100% clear);
// on the other hand, 64-bit macOS seems to be fine. Also, 64-bit
// windows is an issue because slp_switch is written fully in assembly
// and currently ignores its argument so some code would have to be
// adjusted there to pass the argument on to the
// ``slp_save_state_asm()`` function (but interestingly, because of
// the calling convention, the extra argument is just ignored and
// things function fine, albeit slower, if we just modify
// ``slp_save_state_asm`()` to fetch the pointer to pass to the macro.)
#define SLP_SAVE_STATE(stackref, stsizediff) \
    do {                                                    \
    ThreadState& greenlet_thread_state = GET_THREAD_STATE(); \
    stackref += STACK_MAGIC;                 \
    if (slp_save_state((char*)stackref, greenlet_thread_state)) \
        return -1;                           \
    if (!PyGreenlet_ACTIVE(greenlet_thread_state.borrow_target())) \
        return 1;                            \
    stsizediff = greenlet_thread_state.borrow_target()->stack_start - (char*)stackref; \
    } while (0)

#define SLP_RESTORE_STATE() slp_restore_state()

#define SLP_EVAL
#define slp_switch GREENLET_NOINLINE(slp_switch)
#include "slp_platformselect.h"
#undef slp_switch

#ifndef STACK_MAGIC
#    error \
        "greenlet needs to be ported to this platform, or taught how to detect your compiler properly."
#endif /* !STACK_MAGIC */

#ifdef EXTERNAL_ASM
/* CCP addition: Make these functions, to be called from assembler.
 * The token include file for the given platform should enable the
 * EXTERNAL_ASM define so that this is included.
 */
extern "C" {
intptr_t
slp_save_state_asm(intptr_t* ref)
{
    intptr_t diff;
    SLP_SAVE_STATE(ref, diff);
    return diff;
}

void
slp_restore_state_asm(void)
{
    SLP_RESTORE_STATE();
}

extern int
slp_switch(void);
};
#endif

/***********************************************************/

static int
g_save(PyGreenlet* g, char* stop)
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
    intptr_t sz1 = g->stack_saved;
    intptr_t sz2 = stop - g->stack_start;
    assert(g->stack_start != NULL);
    if (sz2 > sz1) {
        char* c = (char*)PyMem_Realloc(g->stack_copy, sz2);
        if (!c) {
            PyErr_NoMemory();
            return -1;
        }
        memcpy(c + sz1, g->stack_start + sz1, sz2 - sz1);
        g->stack_copy = c;
        g->stack_saved = sz2;
    }
    return 0;
}

static void GREENLET_NOINLINE(slp_restore_state)(void)
{
    ThreadState& state = GET_THREAD_STATE();
    PyGreenlet* g = state.borrow_target();
    PyGreenlet* owner = state.borrow_current();

#ifdef SLP_BEFORE_RESTORE_STATE
    SLP_BEFORE_RESTORE_STATE();
#endif

    /* Restore the heap copy back into the C stack */
    if (g->stack_saved != 0) {
        memcpy(g->stack_start, g->stack_copy, g->stack_saved);
        PyMem_Free(g->stack_copy);
        g->stack_copy = NULL;
        g->stack_saved = 0;
    }
    if (owner->stack_start == NULL) {
        owner = owner->stack_prev; /* greenlet is dying, skip it */
    }
    while (owner && owner->stack_stop <= g->stack_stop) {
        owner = owner->stack_prev; /* find greenlet with more stack */
    }
    g->stack_prev = owner;
}

static int GREENLET_NOINLINE(slp_save_state)(char* stackref, ThreadState& state)
{
    /* must free all the C stack up to target_stop */
    char* target_stop = (state.borrow_target())->stack_stop;
    PyGreenlet* owner = state.borrow_current();
    assert(owner->stack_saved == 0);
    if (owner->stack_start == NULL) {
        owner = owner->stack_prev; /* not saved if dying */
    }
    else {
        owner->stack_start = stackref;
    }

#ifdef SLP_BEFORE_SAVE_STATE
    SLP_BEFORE_SAVE_STATE();
#endif

    while (owner->stack_stop < target_stop) {
        /* ts_current is entierely within the area to free */
        if (g_save(owner, owner->stack_stop)) {
            return -1; /* XXX */
        }
        owner = owner->stack_prev;
    }
    if (owner != (state.borrow_target())) {
        if (g_save(owner, target_stop)) {
            return -1; /* XXX */
        }
    }
    return 0;
}

static void GREENLET_NOINLINE(g_switchstack_success)(void)
{
    // XXX: The ownership rules can be simplified here.
    ThreadState& state = GET_THREAD_STATE();
    PyGreenlet* target = state.borrow_target();
    PyGreenlet* origin = state.borrow_current();
    PyThreadState* tstate = PyThreadState_GET();
    tstate->recursion_depth = target->recursion_depth;
    tstate->frame = target->top_frame;
    target->top_frame = NULL;
#if GREENLET_PY37
    tstate->context = target->context;
    target->context = NULL;
    /* Incrementing this value invalidates the contextvars cache,
       which would otherwise remain valid across switches */
    tstate->context_ver++;
#endif

#if GREENLET_PY37
    tstate->exc_state = target->exc_state;
    tstate->exc_info =
        target->exc_info ? target->exc_info : &tstate->exc_state;
#else
    tstate->exc_type = target->exc_type;
    tstate->exc_value = target->exc_value;
    tstate->exc_traceback = target->exc_traceback;
#endif
    green_clear_exc(target);
#if GREENLET_USE_CFRAME
    tstate->cframe = target->cframe;
    /*
      If we were tracing, we need to keep tracing.
      There should never be the possibility of hitting the
      root_cframe here. See note above about why we can't
      just copy this from ``origin->cframe->use_tracing``.
    */
    tstate->cframe->use_tracing = state.switchstack_use_tracing;
#endif
    assert(state.borrow_origin() == NULL);
    Py_INCREF(target); // XXX: Simplify ownership rules
    state.release_ownership_of_current_and_steal_new_current(target);
    state.steal_ownership_as_origin(origin);
    state.wref_target(NULL);
}

/**
   Perform a stack switch according to some thread-local variables
   that must be set in ``g_thread_state_global`` before calling this
   function. Those variables are:

   - current greenlet (holds a reference)
   - target greenlet: greenlet to switch to (weak reference)
   - switch_args: NULL if PyErr_Occurred(),
     else a tuple of args sent to ts_target (weak reference)
   - switch_kwargs: switch kwargs (weak reference)

   Because the stack switch happens in this function, this function
   can't use its own stack (local) variables, set before the switch,
   and then accessed after the switch.

   Further, you con't even access ``g_thread_state_global`` before and
   after the switch from the global variable. Because it is thread
   local (and hard to declare as volatile), some compilers cache it in
   a register/on the stack, notably new versions of MSVC; this breaks
   with strange crashes sometime later, because writing to anything in
   ``g_thread_state_global`` after the switch is actually writing to
   random memory. For this reason, we call a non-inlined function to
   finish the operation.


   On return results are passed via those same global variables, plus:

   - origin: originating greenlet (holds a reference)

   It is very important that stack switch is 'atomic', i.e. no
   calls into other Python code allowed (except very few that
   are safe), because global variables are very fragile. (This should
   no longer be the case with thread-local variables.)
*/
static int
g_switchstack(void)
{

    { /* save state */
        ThreadState& state = GET_THREAD_STATE();
        PyGreenlet* current = state.borrow_current();
        PyThreadState* tstate = PyThreadState_GET();
        current->recursion_depth = tstate->recursion_depth;
        current->top_frame = tstate->frame;
#if GREENLET_PY37
        current->context = tstate->context;
#endif
#if GREENLET_PY37
        current->exc_info = tstate->exc_info;
        current->exc_state = tstate->exc_state;
#else
        current->exc_type = tstate->exc_type;
        current->exc_value = tstate->exc_value;
        current->exc_traceback = tstate->exc_traceback;
#endif
#if GREENLET_USE_CFRAME
        /*
         IMPORTANT: ``cframe`` is a pointer into the STACK.
         Thus, because the call to ``slp_switch()``
         changes the contents of the stack, you cannot read from
         ``ts_current->cframe`` after that call and necessarily
         get the same values you get from reading it here. Anything
         you need to restore from now to then must be saved
         in a global/threadlocal variable (because we can't use stack variables
         here either).
         */
        current->cframe = tstate->cframe;
        state.switchstack_use_tracing = tstate->cframe->use_tracing;
#endif
    }

    int err = slp_switch();

    if (err < 0) { /* error */
        PyGreenlet* current = GET_THREAD_STATE().borrow_current();
        current->top_frame = NULL;
#if GREENLET_PY37
        green_clear_exc(current);
#else
        current->exc_type = NULL;
        current->exc_value = NULL;
        current->exc_traceback = NULL;
#endif

        assert(GET_THREAD_STATE().borrow_origin() == NULL);
        GET_THREAD_STATE().state().wref_target(NULL);
    }
    else {
        // No stack-based variables are valid anymore.
        g_switchstack_success();
    }
    return err;
}

static int
g_calltrace(PyObject* tracefunc, PyObject* event, PyGreenlet* origin,
            PyGreenlet* target)
{
    PyObject* retval;
    PyObject *exc_type, *exc_val, *exc_tb;
    PyThreadState* tstate;
    PyErr_Fetch(&exc_type, &exc_val, &exc_tb);
    tstate = PyThreadState_GET();
    tstate->tracing++;
    TSTATE_USE_TRACING(tstate) = 0;
    retval = PyObject_CallFunction(tracefunc, "O(OO)", event, origin, target);
    tstate->tracing--;
    TSTATE_USE_TRACING(tstate) =
        (tstate->tracing <= 0 &&
         ((tstate->c_tracefunc != NULL) || (tstate->c_profilefunc != NULL)));
    if (retval == NULL) {
        /* In case of exceptions trace function is removed */
        GET_THREAD_STATE().state().del_tracefunc();
        Py_XDECREF(exc_type);
        Py_XDECREF(exc_val);
        Py_XDECREF(exc_tb);
        return -1;
    }
    else {
        Py_DECREF(retval);
    }
    PyErr_Restore(exc_type, exc_val, exc_tb);
    return 0;
}

static GREENLET_NOINLINE_P(PyObject*, g_switch_finish)(int err)
{
    /* For a very short time, immediately after the 'atomic'
       g_switchstack() call, global variables are in a known state.
       We need to save everything we need, before it is destroyed
       by calls into arbitrary Python code.

       XXX: This is no longer really necessary since we don't use
       globals.
       XXX: However, we probably have the same stack issues as
       g_switchstack itself!
    */
    ThreadState& state = GET_THREAD_STATE();
    PyObject* args = state.borrow_switch_args();
    PyObject* kwargs = state.borrow_switch_kwargs();
    state.wref_switch_args_kwargs(NULL, NULL);
    _GDPrint("Finishing switch into: ");
    _GDPoPrint((PyObject*)state.borrow_current());
    _GDPrint("\n\tRefcount: %ld\n", Py_REFCNT(state.borrow_current()));
    if (err < 0) {
        /* Turn switch errors into switch throws */
        assert(state.borrow_origin() == NULL);
        Py_CLEAR(kwargs);
        Py_CLEAR(args);
    }
    else {
        PyGreenlet* origin = state.release_ownership_of_origin();
        PyGreenlet* current = state.borrow_current();
        PyObject* tracefunc = state.get_tracefunc();

        if (tracefunc) {
            if (g_calltrace(tracefunc,
                            args ? ts_event_switch : ts_event_throw,
                            origin,
                            current) < 0) {
                /* Turn trace errors into switch throws */
                Py_CLEAR(kwargs);
                Py_CLEAR(args);
            }
            Py_DECREF(tracefunc);
        }

        Py_DECREF(origin);

        _GDPrint("Finished switch into: ");
        _GDPoPrint((PyObject*)state.borrow_current());
        _GDPrint("\n\tRefcount: %ld\n", Py_REFCNT(state.borrow_current()));

    }

    /* We need to figure out what values to pass to the target greenlet
       based on the arguments that have been passed to greenlet.switch(). If
       switch() was just passed an arg tuple, then we'll just return that.
       If only keyword arguments were passed, then we'll pass the keyword
       argument dict. Otherwise, we'll create a tuple of (args, kwargs) and
       return both. */
    if (kwargs == NULL) {
        return args;
    }

    if (PyDict_Size(kwargs) == 0) {
        Py_DECREF(kwargs);
        return args;
    }

    if (PySequence_Length(args) == 0) {
        Py_DECREF(args);
        return kwargs;
    }

    PyObject* tuple = PyTuple_New(2);
    if (tuple == NULL) {
        Py_DECREF(args);
        Py_DECREF(kwargs);
        _GDPrint("g_switch_finish: err no tuple\n");
        return NULL;
    }
    PyTuple_SET_ITEM(tuple, 0, args);
    PyTuple_SET_ITEM(tuple, 1, kwargs);
    return tuple;
}

static PyObject*
g_switch(PyGreenlet* target, PyObject* args, PyObject* kwargs)
{
    /* _consumes_ a reference to the args tuple and kwargs dict,
       and return a new tuple reference */
    int err = 0;
    ThreadState& state = GET_THREAD_STATE();

    /* check ts_current */
    /* If we get here, there must be one, right?.  */
    assert(state.borrow_current() != nullptr);
    // Switching greenlets used to attempt to clean out ones that need
    // deleted *if* we detected a thread switch. Should it still do
    // that?
    // An issue is that if we delete a greenlet from another thread,
    // it gets queued to this thread, and ``kill_greenlet()`` switches
    // back into the greenlet
    /*
    if (!STATE_OK) {
        Py_XDECREF(args);
        Py_XDECREF(kwargs);
        _GDPrint("g_switch: err 1\n");
        return NULL;
    }
    */

    void* run_info = find_and_borrow_main_greenlet_in_lineage(target);
    if (run_info == NULL || run_info != state.borrow_main_greenlet()) {
        Py_XDECREF(args);
        Py_XDECREF(kwargs);
        PyErr_SetString(PyExc_GreenletError,
                        run_info ?
                            "cannot switch to a different thread (1)" :
                            "cannot switch to a garbage collected greenlet");
        _GDPrint("g_switch: err 2   \n");
        return NULL;
    }

    state.wref_switch_args_kwargs(args, kwargs);

    /* find the real target by ignoring dead greenlets,
       and if necessary starting a greenlet. */
    while (target) {
        if (PyGreenlet_ACTIVE(target)) {
            state.wref_target(target);
            _GDPrint("Found target: ");
            _GDPoPrint((PyObject*)target);
            _GDPrint("\n\tRefcount: %ld\n", Py_REFCNT(target));
            err = g_switchstack();
            break;
        }
        if (!PyGreenlet_STARTED(target)) {
            void* dummymarker;
            state.wref_target(target);
            err = g_initialstub(&dummymarker);
            if (err == 1) {
                continue; /* retry the switch */
            }
            break;
        }
        target = target->parent;
    }
    return g_switch_finish(err);
}

static PyObject*
g_handle_exit(PyObject* result)
{
    if (result == NULL && PyErr_ExceptionMatches(PyExc_GreenletExit)) {
        /* catch and ignore GreenletExit */
        PyObject *exc, *val, *tb;
        PyErr_Fetch(&exc, &val, &tb);
        if (val == NULL) {
            Py_INCREF(Py_None);
            val = Py_None;
        }
        result = val;
        Py_DECREF(exc);
        Py_XDECREF(tb);
    }
    if (result != NULL) {
        /* package the result into a 1-tuple */
        PyObject* r = result;
        result = PyTuple_New(1);
        if (result) {
            PyTuple_SET_ITEM(result, 0, r);
        }
        else {
            Py_DECREF(r);
        }
    }
    return result;
}

static int GREENLET_NOINLINE(g_initialstub)(void* mark)
{
    int err;
    PyObject* run;
    PyObject *exc, *val, *tb;
    ThreadState& state = GET_THREAD_STATE();
    PyGreenlet* self = state.borrow_target();
    PyObject* args = state.borrow_switch_args();
    PyObject* kwargs = state.borrow_switch_kwargs();
#if GREENLET_USE_CFRAME
    /*
      See green_new(). This is a stack-allocated variable used
      while *self* is in PyObject_Call().
      We want to defer copying the state info until we're sure
      we need it and are in a stable place to do so.
    */
    CFrame trace_info;
#endif
    /* save exception in case getattr clears it */
    PyErr_Fetch(&exc, &val, &tb);
    /*
       self.run is the object to call in the new greenlet.
       This could run arbitrary python code and switch greenlets!
       XXX: We used to override the ``run_info`` pointer to act as the 'run'
       attribute if they set it manually on an instance, instead of
       putting it into the dict. Why? No Idea.
    */
    run = PyObject_GetAttrString((PyObject*)self, "run");
    if (run == NULL) {
        Py_XDECREF(exc);
        Py_XDECREF(val);
        Py_XDECREF(tb);
        return -1;
    }

    /* restore saved exception */
    PyErr_Restore(exc, val, tb);

    /* recheck the state in case getattr caused thread switches */
    /*
    if (!STATE_OK) {
        Py_DECREF(run);
        return -1;
    }
    */

    /* recheck run_info in case greenlet reparented anywhere above */
    void* run_info = find_and_borrow_main_greenlet_in_lineage(self);
    if (run_info == NULL || run_info != state.borrow_main_greenlet()) {
        Py_DECREF(run);
        PyErr_SetString(PyExc_GreenletError,
                        run_info ?
                            "cannot switch to a different thread" :
                            "cannot switch to a garbage collected greenlet");
        return -1;
    }

    /* by the time we got here another start could happen elsewhere,
     * that means it should now be a regular switch.
     * XXX: Is this true now that they're thread local?
     */
    if (PyGreenlet_STARTED(self)) {
        Py_DECREF(run);
        state.wref_switch_args_kwargs(args, kwargs);
        return 1;
    }

#if GREENLET_USE_CFRAME
    /* OK, we need it, we're about to switch greenlets, save the state. */
    trace_info = *PyThreadState_GET()->cframe;
    /* Make the target greenlet refer to the stack value. */
    self->cframe = &trace_info;
    /*
      And restore the link to the previous frame so this one gets
      unliked appropriately.
    */
    self->cframe->previous = &PyThreadState_GET()->root_cframe;
#endif
    /* start the greenlet */
    self->stack_start = NULL;
    self->stack_stop = (char*)mark;
    if ((state.borrow_current())->stack_start == NULL) {
        /* ts_current is dying */
        self->stack_prev = (state.borrow_current())->stack_prev;
    }
    else {
        self->stack_prev = state.borrow_current();
    }
    self->top_frame = NULL;
    green_clear_exc(self);
    self->recursion_depth = PyThreadState_GET()->recursion_depth;

    /* restore arguments in case they are clobbered
     * XXX: Still needed now they're thread local?
     */
    state.wref_target(self);
    state.wref_switch_args_kwargs(args, kwargs);

    /* perform the initial switch */
    err = g_switchstack();

    /* returns twice!
       The 1st time with ``err == 1``: we are in the new greenlet
       The 2nd time with ``err <= 0``: back in the caller's greenlet
    */
    if (err == 1) {
        /* in the new greenlet */
        PyGreenlet* origin;
        PyObject* tracefunc;
        PyObject* result;
        PyGreenlet* parent;
        self->stack_start = (char*)1; /* running */

        /* grab origin while we still can */
        origin = state.release_ownership_of_origin();

        Py_CLEAR(self->run_callable); // XXX: We could clear this much
                                      // earlier, right?
        assert(self->main_greenlet_s == NULL);
        self->main_greenlet_s = state.get_main_greenlet();
        assert(self->main_greenlet_s);

        if ((tracefunc = state.get_tracefunc()) != NULL) {
            if (g_calltrace(tracefunc,
                            args ? ts_event_switch : ts_event_throw,
                            origin,
                            self) < 0) {
                /* Turn trace errors into switch throws */
                Py_CLEAR(kwargs);
                Py_CLEAR(args);
            }
            Py_DECREF(tracefunc);
        }

        Py_DECREF(origin);

        if (args == NULL) {
            /* pending exception */
            result = NULL;
        }
        else {
            /* call g.run(*args, **kwargs) */
            result = PyObject_Call(run, args, kwargs);
            Py_DECREF(args);
            Py_XDECREF(kwargs);
        }
        Py_DECREF(run);
        result = g_handle_exit(result);

        /* jump back to parent */
        self->stack_start = NULL; /* dead */
        for (parent = self->parent; parent != NULL; parent = parent->parent) {
            result = g_switch(parent, result, NULL);
            /* Return here means switch to parent failed,
             * in which case we throw *current* exception
             * to the next parent in chain.
             */
            assert(result == NULL);
        }
        /* We ran out of parents, cannot continue */
        PyErr_WriteUnraisable((PyObject*)self);
        Py_FatalError("greenlets cannot continue");
    }
    /* back in the parent */
    if (err < 0) {
        /* start failed badly, restore greenlet state */
        self->stack_start = NULL;
        self->stack_stop = NULL;
        self->stack_prev = NULL;
    }
    return err;
}

/***********************************************************/

static PyGreenlet*
green_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    PyGreenlet* o =
        (PyGreenlet*)PyBaseObject_Type.tp_new(type, ts_empty_tuple, ts_empty_dict);
    if (o != NULL) {
        /*
        if (!STATE_OK) {
            Py_DECREF(o);
            return NULL;
        }
        */

        o->parent = GET_THREAD_STATE().state().get_or_establish_current();
#if GREENLET_USE_CFRAME
        /*
          The PyThreadState->cframe pointer usually points to memory on the
          stack, alloceted in a call into PyEval_EvalFrameDefault.

          Initially, before any evaluation begins, it points to the initial
          PyThreadState object's ``root_cframe`` object, which is statically
          allocated for the lifetime of the thread.

          A greenlet can last for longer than a call to
          PyEval_EvalFrameDefault, so we can't set its ``cframe`` pointer to
          be the current ``PyThreadState->cframe``; nor could we use one from
          the greenlet parent for the same reason. Yet a further no: we can't
          allocate one scoped to the greenlet and then destroy it when the
          greenlet is deallocated, because inside the interpreter the CFrame
          objects form a linked list, and that too can result in accessing
          memory beyond its dynamic lifetime (if the greenlet doesn't actually
          finish before it dies, its entry could still be in the list).

          Using the ``root_cframe`` is problematic, though, because its
          members are never modified by the interpreter and are set to 0,
          meaning that its ``use_tracing`` flag is never updated. We don't
          want to modify that value in the ``root_cframe`` ourself: it
          *shouldn't* matter much because we should probably never get back to
          the point where that's the only cframe on the stack; even if it did
          matter, the major consequence of an incorrect value for
          ``use_tracing`` is that if its true the interpreter does some extra
          work --- however, it's just good code hygiene.

          Our solution: before a greenlet runs, after its initial creation,
          it uses the ``root_cframe`` just to have something to put there.
          However, once the greenlet is actually switched to for the first
          time, ``g_initialstub`` (which doesn't actually "return" while the
          greenlet is running) stores a new CFrame on its local stack, and
          copies the appropriate values from the currently running CFrame;
          this is then made the CFrame for the newly-minted greenlet.
          ``g_initialstub`` then proceeds to call ``glet.run()``, which
          results in ``PyEval_...`` adding the CFrame to the list. Switches
          continue as normal. Finally, when the greenlet finishes, the call to
          ``glet.run()`` returns and the CFrame is taken out of the linked
          list and the stack value is now unused and free to expire.
        */
        o->cframe = &PyThreadState_GET()->root_cframe;
#endif
    }
    return o;
}

static int
green_setrun(PyGreenlet* self, PyObject* nrun, void* c);
static int
green_setparent(PyGreenlet* self, PyObject* nparent, void* c);

static int
green_init(PyGreenlet* self, PyObject* args, PyObject* kwargs)
{
    PyObject* run = NULL;
    PyObject* nparent = NULL;
    static const char* const kwlist[] = {
        "run",
        "parent",
        NULL
    };

    // recall: The O specifier does NOT increase the reference count.
    if (!PyArg_ParseTupleAndKeywords(
            args, kwargs, "|OO:green", (char**)kwlist, &run, &nparent)) {
        return -1;
    }

    if (run != NULL) {
        if (green_setrun(self, run, NULL)) {
            return -1;
        }
    }
    if (nparent != NULL && nparent != Py_None) {
        return green_setparent(self, nparent, NULL);
    }
    return 0;
}

static int
kill_greenlet(PyGreenlet* self)
{
    /* Cannot raise an exception to kill the greenlet if
       it is not running in the same thread! */
    if (self->main_greenlet_s == GET_THREAD_STATE().borrow_main_greenlet()) {
        /* The dying greenlet cannot be a parent of ts_current
           because the 'parent' field chain would hold a
           reference */
        PyObject* result;
        PyGreenlet* oldparent;
        PyGreenlet* tmp;
        // XXX: should not be needed here, right? Plus, this causes recursion.
        // if (!STATE_OK) {
        //     return -1;
        // }
        oldparent = self->parent;
        self->parent = GET_THREAD_STATE().state().get_current();
        /* Send the greenlet a GreenletExit exception. */
        PyErr_SetString(PyExc_GreenletExit, "Killing the greenlet because all references have vanished.");
        result = g_switch(self, NULL, NULL);
        tmp = self->parent;
        self->parent = oldparent;
        Py_XDECREF(tmp);
        if (result == NULL) {
            return -1;
        }
        Py_DECREF(result);
        return 0;
    }

    // Not the same thread! Temporarily save the greenlet
    // into its thread's deleteme list, *if* it exists.
    // If that thread has already exited, and processed its pending
    // cleanup, we'll never be able to clean everything up: we won't
    // be able to raise an exception.
    // That's mostly OK! Since we can't add it to a list, our refcount
    // won't increase, and we'll go ahead with the DECREFs later.
    if (self->main_greenlet_s->thread_state) {
        _GDPrint("Adding to state %p\n", self->main_greenlet_s->thread_state);
        self->main_greenlet_s->thread_state->delete_when_thread_running(self);
    }
    else {
        // We need to make it look non-active, though, so that dealloc
        // finishes killing it.
        self->stack_start = NULL;
        assert(!PyGreenlet_ACTIVE(self));
        Py_CLEAR(self->top_frame);
    }
    return 0;
}

static int
green_traverse(PyGreenlet* self, visitproc visit, void* arg)
{
    /* We must only visit referenced objects, i.e. only objects
       Py_INCREF'ed by this greenlet (directly or indirectly):
       - stack_prev is not visited: holds previous stack pointer, but it's not
       referenced
       - frames are not visited: alive greenlets are not garbage collected
       anyway */
    Py_VISIT((PyObject*)self->parent);
    Py_VISIT(self->main_greenlet_s);
    Py_VISIT(self->run_callable);
#if GREENLET_PY37
    Py_VISIT(self->context);
#endif
#if GREENLET_PY37
    Py_VISIT(self->exc_state.exc_type);
    Py_VISIT(self->exc_state.exc_value);
    Py_VISIT(self->exc_state.exc_traceback);
#else
    Py_VISIT(self->exc_type);
    Py_VISIT(self->exc_value);
    Py_VISIT(self->exc_traceback);
#endif
    Py_VISIT(self->dict);
    return 0;
}

static int
green_is_gc(PyGreenlet* self)
{
    _GDPrint("\t\tIs GC? ");
    _GDPoPrint((PyObject*)self);
    int result = 0;
    /* Main greenlet can be garbage collected since it can only
       become unreachable if the underlying thread exited.
       Active greenlet cannot be garbage collected, however. */
    if (PyGreenlet_MAIN(self) || !PyGreenlet_ACTIVE(self)) {
        result = 1;
    }
    // The main greenlet pointer can go away if the thread dies.
    if (self->main_greenlet_s && !self->main_greenlet_s->thread_state) {
        // Our thread is dead! We can never run again. Might as well
        // GC us.
        _GDPrint("Allowing GC of greenlet from dead thread\n");
        result = 1;
    }
    _GDPrint(" %s\n", result ? "YES" : "NO");
    _GDPrint("\t\t\tRefcount: %ld Frame count: %ld\n",
            Py_REFCNT(self), self->top_frame ? Py_REFCNT(self->top_frame) : -1);
    return result;
}

static int
green_clear(PyGreenlet* self)
{
    /* Greenlet is only cleared if it is about to be collected.
       Since active greenlets are not garbage collectable, we can
       be sure that, even if they are deallocated during clear,
       nothing they reference is in unreachable or finalizers,
       so even if it switches we are relatively safe. */
    _GDPrint("Clearing ");
    _GDPoPrint((PyObject*)self);
    _GDPrint("\n");
    Py_CLEAR(self->parent);
    Py_CLEAR(self->main_greenlet_s); // XXX breaks when this is a state
    Py_CLEAR(self->run_callable);
#if GREENLET_PY37
    Py_CLEAR(self->context);
#endif
#if GREENLET_PY37
    Py_CLEAR(self->exc_state.exc_type);
    Py_CLEAR(self->exc_state.exc_value);
    Py_CLEAR(self->exc_state.exc_traceback);
#else
    Py_CLEAR(self->exc_type);
    Py_CLEAR(self->exc_value);
    Py_CLEAR(self->exc_traceback);
#endif
    Py_CLEAR(self->dict);
    return 0;
}

/**
 * Returns 0 on failure (the object was resurrected) or 1 on success.
 **/
static int
_green_dealloc_kill_started_non_main_greenlet(PyGreenlet* self)
{
    Py_ssize_t refcnt;
    PyObject *error_type, *error_value, *error_traceback;
    _GDPrint("dealloc: mark 1\n");
    /* Hacks hacks hacks copied from instance_dealloc() */
    /* Temporarily resurrect the greenlet. */
    assert(Py_REFCNT(self) == 0);
    Py_SET_REFCNT(self, 1);
    /* Save the current exception, if any. */
    PyErr_Fetch(&error_type, &error_value, &error_traceback);
    if (kill_greenlet(self) < 0) {
        PyErr_WriteUnraisable((PyObject*)self);
        /* XXX what else should we do? */
    }
    /* Check for no resurrection must be done while we keep
     * our internal reference, otherwise PyFile_WriteObject
     * causes recursion if using Py_INCREF/Py_DECREF
     */
    if (Py_REFCNT(self) == 1 && PyGreenlet_ACTIVE(self)) {
        /* Not resurrected, but still not dead!
           XXX what else should we do? we complain. */
        PyObject* f = PySys_GetObject("stderr");
        Py_INCREF(self); /* leak! */
        if (f != NULL) {
            PyFile_WriteString("GreenletExit did not kill ", f);
            PyFile_WriteObject((PyObject*)self, f, 0);
            PyFile_WriteString("\n", f);
        }
    }
    /* Restore the saved exception. */
    PyErr_Restore(error_type, error_value, error_traceback);
    /* Undo the temporary resurrection; can't use DECREF here,
     * it would cause a recursive call.
     */
    assert(Py_REFCNT(self) > 0);

    refcnt = Py_REFCNT(self) - 1;
    Py_SET_REFCNT(self, refcnt);
    if (refcnt != 0) {
        /* Resurrected! */
        _Py_NewReference((PyObject*)self);
        Py_SET_REFCNT(self, refcnt);
        /* Better to use tp_finalizer slot (PEP 442)
         * and call ``PyObject_CallFinalizerFromDealloc``,
         * but that's only supported in Python 3.4+; see
         * Modules/_io/iobase.c for an example.
         *
         * The following approach is copied from iobase.c in CPython 2.7.
         * (along with much of this function in general). Here's their
         * comment:
         *
         * When called from a heap type's dealloc, the type will be
         * decref'ed on return (see e.g. subtype_dealloc in typeobject.c). */
        if (PyType_HasFeature(Py_TYPE(self), Py_TPFLAGS_HEAPTYPE)) {
            Py_INCREF(Py_TYPE(self));
        }

        PyObject_GC_Track((PyObject*)self);

        _Py_DEC_REFTOTAL;
#ifdef COUNT_ALLOCS
        --Py_TYPE(self)->tp_frees;
        --Py_TYPE(self)->tp_allocs;
#endif /* COUNT_ALLOCS */
        return 0;
    }
    return 1;
}

static void
green_dealloc(PyGreenlet* self)
{
    PyObject_GC_UnTrack(self);
#ifndef NDEBUG
    PyObject* already_in_err = PyErr_Occurred();
#endif
    _GDPrint("About to attempt to dealloc %p\n", self);
    if (PyGreenlet_ACTIVE(self)
        && self->main_greenlet_s != NULL // means started
        && !PyGreenlet_MAIN(self)) {
        if (!_green_dealloc_kill_started_non_main_greenlet(self)) {
            _GDPrint("Resurrected %p\n", self);
            return;
        }
        _GDPrint("Did kill, did not resurrect %p\n", self);
    }

    if (self->weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject*)self);
    }
    assert(already_in_err || !PyErr_Occurred());
    Py_CLEAR(self->parent);
    assert(already_in_err || !PyErr_Occurred());
    Py_CLEAR(self->main_greenlet_s);
    assert(already_in_err || !PyErr_Occurred());
#if GREENLET_PY37
    Py_CLEAR(self->context);
    assert(already_in_err || !PyErr_Occurred());
#endif
#if GREENLET_PY37
    Py_CLEAR(self->exc_state.exc_type);
    Py_CLEAR(self->exc_state.exc_value);
    Py_CLEAR(self->exc_state.exc_traceback);
    assert(already_in_err || !PyErr_Occurred());
#else
    Py_CLEAR(self->exc_type);
    assert(already_in_err || !PyErr_Occurred());
    Py_CLEAR(self->exc_value);
    assert(already_in_err || !PyErr_Occurred());
    Py_CLEAR(self->exc_traceback);
    assert(already_in_err || !PyErr_Occurred());
#endif
    Py_CLEAR(self->dict);
    assert(already_in_err || !PyErr_Occurred());
    // XXX: We should never get here with a main greenlet that still
    // has a thread state attached. If we do, that's a problem, right?
    // (If it's not a problem, we can customize the dealloc function
    // for the main greenlet class).
#if 0
    if (self->thread_state) {
        // HMM. We shouldn't get here.
        _GDPrint("DEALLOC MAIN GREENLET (%p) WITH THREAD STATE (%p)\n", self, self->thread_state);
        assert(!self->thread_state);
    }
#endif
    Py_TYPE(self)->tp_free((PyObject*)self);
    assert(already_in_err || !PyErr_Occurred());
}

static inline PyObject*
single_result(PyObject* results)
{
    if (results != NULL
        && PyTuple_Check(results)
        && PyTuple_GET_SIZE(results) == 1) {
        PyObject* result = PyTuple_GET_ITEM(results, 0);
        Py_INCREF(result);
        Py_DECREF(results);
        return result;
    }
    // _GDPrint("single_result: current: ");
    // _GDPoPrint((PyObject*)g_thread_state_global.borrow_current());
    // _GDPrint("\n\tRefcount: %ld\n", Py_REFCNT(g_thread_state_global.borrow_current()));
    return results;
}

static PyObject*
throw_greenlet(PyGreenlet* self, PyObject* typ, PyObject* val, PyObject* tb)
{
    /* Note: _consumes_ a reference to typ, val, tb */
    PyObject* result = NULL;
    PyErr_Restore(typ, val, tb);
    if (PyGreenlet_STARTED(self) && !PyGreenlet_ACTIVE(self)) {
        /* dead greenlet: turn GreenletExit into a regular return */
        result = g_handle_exit(result);
    }
    return single_result(g_switch(self, result, NULL));
}

PyDoc_STRVAR(
    green_switch_doc,
    "switch(*args, **kwargs)\n"
    "\n"
    "Switch execution to this greenlet.\n"
    "\n"
    "If this greenlet has never been run, then this greenlet\n"
    "will be switched to using the body of ``self.run(*args, **kwargs)``.\n"
    "\n"
    "If the greenlet is active (has been run, but was switch()'ed\n"
    "out before leaving its run function), then this greenlet will\n"
    "be resumed and the return value to its switch call will be\n"
    "None if no arguments are given, the given argument if one\n"
    "argument is given, or the args tuple and keyword args dict if\n"
    "multiple arguments are given.\n"
    "\n"
    "If the greenlet is dead, or is the current greenlet then this\n"
    "function will simply return the arguments using the same rules as\n"
    "above.\n");

static PyObject*
green_switch(PyGreenlet* self, PyObject* args, PyObject* kwargs)
{
    _GDPrint("Switching to: ");
    _GDPoPrint((PyObject*)self);
    _GDPrint("\t\n Refcount: %ld\n", Py_REFCNT((PyObject*)self));

    Py_INCREF(args);
    Py_XINCREF(kwargs);
    return single_result(g_switch(self, args, kwargs));
}

PyDoc_STRVAR(
    green_throw_doc,
    "Switches execution to this greenlet, but immediately raises the\n"
    "given exception in this greenlet.  If no argument is provided, the "
    "exception\n"
    "defaults to `greenlet.GreenletExit`.  The normal exception\n"
    "propagation rules apply, as described for `switch`.  Note that calling "
    "this\n"
    "method is almost equivalent to the following::\n"
    "\n"
    "    def raiser():\n"
    "        raise typ, val, tb\n"
    "    g_raiser = greenlet(raiser, parent=g)\n"
    "    g_raiser.switch()\n"
    "\n"
    "except that this trick does not work for the\n"
    "`greenlet.GreenletExit` exception, which would not propagate\n"
    "from ``g_raiser`` to ``g``.\n");

static PyObject*
green_throw(PyGreenlet* self, PyObject* args)
{
    PyObject* typ = PyExc_GreenletExit;
    PyObject* val = NULL;
    PyObject* tb = NULL;

    if (!PyArg_ParseTuple(args, "|OOO:throw", &typ, &val, &tb)) {
        return NULL;
    }

    /* First, check the traceback argument, replacing None, with NULL */
    if (tb == Py_None) {
        tb = NULL;
    }
    else if (tb != NULL && !PyTraceBack_Check(tb)) {
        PyErr_SetString(PyExc_TypeError,
                        "throw() third argument must be a traceback object");
        return NULL;
    }

    Py_INCREF(typ);
    Py_XINCREF(val);
    Py_XINCREF(tb);

    if (PyExceptionClass_Check(typ)) {
        PyErr_NormalizeException(&typ, &val, &tb);
    }
    else if (PyExceptionInstance_Check(typ)) {
        /* Raising an instance. The value should be a dummy. */
        if (val && val != Py_None) {
            PyErr_SetString(
                PyExc_TypeError,
                "instance exception may not have a separate value");
            goto failed_throw;
        }
        else {
            /* Normalize to raise <class>, <instance> */
            Py_XDECREF(val);
            val = typ;
            typ = PyExceptionInstance_Class(typ);
            Py_INCREF(typ);
        }
    }
    else {
        /* Not something you can raise. throw() fails. */
        PyErr_Format(PyExc_TypeError,
                     "exceptions must be classes, or instances, not %s",
                     Py_TYPE(typ)->tp_name);
        goto failed_throw;
    }

    return throw_greenlet(self, typ, val, tb);

failed_throw:
    /* Didn't use our arguments, so restore their original refcounts */
    Py_DECREF(typ);
    Py_XDECREF(val);
    Py_XDECREF(tb);
    return NULL;
}

static int
green_bool(PyGreenlet* self)
{
    return PyGreenlet_ACTIVE(self);
}

static PyObject*
green_getdict(PyGreenlet* self, void* c)
{
    if (self->dict == NULL) {
        self->dict = PyDict_New();
        if (self->dict == NULL) {
            return NULL;
        }
    }
    Py_INCREF(self->dict);
    return self->dict;
}

static int
green_setdict(PyGreenlet* self, PyObject* val, void* c)
{
    PyObject* tmp;

    if (val == NULL) {
        PyErr_SetString(PyExc_TypeError, "__dict__ may not be deleted");
        return -1;
    }
    if (!PyDict_Check(val)) {
        PyErr_SetString(PyExc_TypeError, "__dict__ must be a dictionary");
        return -1;
    }
    tmp = self->dict;
    Py_INCREF(val);
    self->dict = val;
    Py_XDECREF(tmp);
    return 0;
}

static int
_green_not_dead(PyGreenlet* self)
{
    return PyGreenlet_ACTIVE(self) || !PyGreenlet_STARTED(self);
}


static PyObject*
green_getdead(PyGreenlet* self, void* c)
{
    if (_green_not_dead(self)) {
        Py_RETURN_FALSE;
    }
    else {
        Py_RETURN_TRUE;
    }
}

static PyObject*
green_get_stack_saved(PyGreenlet* self, void* c)
{
    return PyLong_FromSsize_t(self->stack_saved);
}

static PyObject*
green_getrun(PyGreenlet* self, void* c)
{
    if (PyGreenlet_STARTED(self) || self->run_callable == NULL) {
        PyErr_SetString(PyExc_AttributeError, "run");
        return NULL;
    }
    Py_INCREF(self->run_callable);
    return self->run_callable;
}

static int
green_setrun(PyGreenlet* self, PyObject* nrun, void* c)
{
    if (PyGreenlet_STARTED(self)) {
        PyErr_SetString(PyExc_AttributeError,
                        "run cannot be set "
                        "after the start of the greenlet");
        return -1;
    }
    PyObject* old = self->run_callable;
    self->run_callable = nrun;
    Py_XINCREF(nrun);
    Py_XDECREF(old);
    return 0;
}

static PyObject*
green_getparent(PyGreenlet* self, void* c)
{
    PyObject* result = self->parent ? (PyObject*)self->parent : Py_None;
    Py_INCREF(result);
    return result;
}

static int
green_setparent(PyGreenlet* self, PyObject* nparent, void* c)
{

    PyGreenlet* run_info = NULL;
    if (nparent == NULL) {
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }
    if (!PyGreenlet_Check(nparent)) {
        PyErr_SetString(PyExc_TypeError, "parent must be a greenlet");
        return -1;
    }
    for (PyGreenlet* p = (PyGreenlet*)nparent; p; p = p->parent) {
        if (p == self) {
            PyErr_SetString(PyExc_ValueError, "cyclic parent chain");
            return -1;
        }
        _GDPrint("Examining parent ");
        _GDPoPrint((PyObject*)p);
        _GDPrint("\n\tActive? %d Run info: %p\n", PyGreenlet_ACTIVE(p), p->main_greenlet_s);
        run_info = PyGreenlet_ACTIVE(p) ? (PyGreenlet*)p->main_greenlet_s : NULL;
    }
    if (run_info == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "parent must not be garbage collected");
        return -1;
    }
    if (PyGreenlet_STARTED(self) && self->main_greenlet_s != (void*)run_info) {
        PyErr_SetString(PyExc_ValueError,
                        "parent cannot be on a different thread");
        return -1;
    }
    PyGreenlet* p = self->parent;
    self->parent = (PyGreenlet*)nparent;
    Py_INCREF(nparent);
    Py_XDECREF(p);
    return 0;
}

#ifdef Py_CONTEXT_H
#    define GREENLET_NO_CONTEXTVARS_REASON "This build of greenlet"
#else
#    define GREENLET_NO_CONTEXTVARS_REASON "This Python interpreter"
#endif

static PyObject*
green_getcontext(PyGreenlet* self, void* c)
{
#if GREENLET_PY37
/* XXX: Should not be necessary, we don't access the current greenlet
   other than to compare it to ourself and its fine if that's null.
 */
/*
    if (!STATE_OK) {
        return NULL;
    }
*/
    PyThreadState* tstate = PyThreadState_GET();
    PyObject* result = NULL;

    if (PyGreenlet_ACTIVE(self) && self->top_frame == NULL) {
        /* Currently running greenlet: context is stored in the thread state,
           not the greenlet object. */
        if (GET_THREAD_STATE().state().is_current(self)) {
            result = tstate->context;
        }
        else {
            PyErr_SetString(PyExc_ValueError,
                            "cannot get context of a "
                            "greenlet that is running in a different thread");
            return NULL;
        }
    }
    else {
        /* Greenlet is not running: just return context. */
        result = self->context;
    }
    if (result == NULL) {
        result = Py_None;
    }
    Py_INCREF(result);
    return result;
#else
    PyErr_SetString(PyExc_AttributeError,
                    GREENLET_NO_CONTEXTVARS_REASON
                    " does not support context variables");
    return NULL;
#endif
}

static int
green_setcontext(PyGreenlet* self, PyObject* nctx, void* c)
{
#if GREENLET_PY37
/* XXX: Should not be necessary, we don't access the current greenlet
   other than to compare it to ourself and its fine if that's null.
 */
/*
    if (!STATE_OK) {
        return -1;
    }
*/
    if (nctx == NULL) {
        PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
        return -1;
    }
    if (nctx == Py_None) {
        /* "Empty context" is stored as NULL, not None. */
        nctx = NULL;
    }
    else if (!PyContext_CheckExact(nctx)) {
        PyErr_SetString(PyExc_TypeError,
                        "greenlet context must be a "
                        "contextvars.Context or None");
        return -1;
    }

    PyThreadState* tstate = PyThreadState_GET();
    PyObject* octx = NULL;

    if (PyGreenlet_ACTIVE(self) && self->top_frame == NULL) {
        /* Currently running greenlet: context is stored in the thread state,
           not the greenlet object. */
        if (GET_THREAD_STATE().state().is_current(self)) {
            octx = tstate->context;
            tstate->context = nctx;
            tstate->context_ver++;
            Py_XINCREF(nctx);
        }
        else {
            PyErr_SetString(PyExc_ValueError,
                            "cannot set context of a "
                            "greenlet that is running in a different thread");
            return -1;
        }
    }
    else {
        /* Greenlet is not running: just set context. Note that the
           greenlet may be dead.*/
        octx = self->context;
        self->context = nctx;
        Py_XINCREF(nctx);
    }
    Py_XDECREF(octx);
    return 0;
#else
    PyErr_SetString(PyExc_AttributeError,
                    GREENLET_NO_CONTEXTVARS_REASON
                    " does not support context variables");
    return -1;
#endif
}

#undef GREENLET_NO_CONTEXTVARS_REASON

static PyObject*
green_getframe(PyGreenlet* self, void* c)
{
    PyObject* result = self->top_frame ? (PyObject*)self->top_frame : Py_None;
    Py_INCREF(result);
    return result;
}

static PyObject*
green_getstate(PyGreenlet* self)
{
    PyErr_Format(PyExc_TypeError,
                 "cannot serialize '%s' object",
                 Py_TYPE(self)->tp_name);
    return NULL;
}

static PyObject*
green_repr(PyGreenlet* self)
{
    /*
      Return a string like
      <greenlet.greenlet at 0xdeadbeef [current][active started]|dead main>

      The handling of greenlets across threads is not super good.
      We mostly use the internal definitions of these terms, but they
      generally should make sense to users as well.
     */
    PyObject* result;
    int never_started = !PyGreenlet_STARTED(self) && !PyGreenlet_ACTIVE(self);

    // XXX: Should not need this, and it has side effects.
    /*
    if (!STATE_OK) {
        return NULL;
    }
    */

    // Disguise the main greenlet type; changing the name in the repr breaks
    // doctests, but having a different actual tp_name is important
    // for debugging.
    const char* const tp_name = Py_TYPE(self) == &PyMainGreenlet_Type
        ? PyGreenlet_Type.tp_name
        : Py_TYPE(self)->tp_name;

    if (_green_not_dead(self)) {
        /* XXX: The otid= is almost useless becasue you can't correlate it to
         any thread identifier exposed to Python. We could use
         PyThreadState_GET()->thread_id, but we'd need to save that in the
         greenlet, or save the whole PyThreadState object itself.

         As it stands, its only useful for identifying greenlets from the same thread.
        */
        result = GNative_FromFormat(
            "<%s object at %p (otid=%p)%s%s%s%s>",
            tp_name,
            self,
            self->main_greenlet_s,
            GET_THREAD_STATE().state().is_current(self)
                ? " current"
                : (PyGreenlet_STARTED(self) ? " suspended" : ""),
            PyGreenlet_ACTIVE(self) ? " active" : "",
            never_started ? " pending" : " started",
            PyGreenlet_MAIN(self) ? " main" : ""
        );
    }
    else {
        /* main greenlets never really appear dead. */
        result = GNative_FromFormat(
            "<%s object at %p (otid=%p) dead>",
            tp_name,
            self,
            self->main_greenlet_s
            );
    }

    return result;
}

/*****************************************************************************
 * C interface
 *
 * These are exported using the CObject API
 */

static PyGreenlet*
PyGreenlet_GetCurrent(void)
{
    return GET_THREAD_STATE().state().get_or_establish_current();
}

static int
PyGreenlet_SetParent(PyGreenlet* g, PyGreenlet* nparent)
{
    if (!PyGreenlet_Check(g)) {
        PyErr_SetString(PyExc_TypeError, "parent must be a greenlet");
        return -1;
    }

    return green_setparent((PyGreenlet*)g, (PyObject*)nparent, NULL);
}

static PyGreenlet*
PyGreenlet_New(PyObject* run, PyGreenlet* parent)
{
    /* XXX: Why doesn't this call green_new()? There's some duplicate
     code. */
    PyGreenlet* g = NULL;
    g = (PyGreenlet*)PyType_GenericAlloc(&PyGreenlet_Type, 0);
    if (g == NULL) {
        return NULL;
    }

    if (run != NULL) {
        green_setrun(g, run, nullptr);
    }

    if (parent != NULL) {
        if (PyGreenlet_SetParent(g, parent)) {
            Py_DECREF(g);
            return NULL;
        }
    }
    else {
        if ((g->parent = PyGreenlet_GetCurrent()) == NULL) {
            Py_DECREF(g);
            return NULL;
        }
    }
#if GREENLET_USE_CFRAME
    g->cframe = &PyThreadState_GET()->root_cframe;
#endif
    return g;
}

static PyObject*
PyGreenlet_Switch(PyGreenlet* g, PyObject* args, PyObject* kwargs)
{
    PyGreenlet* self = (PyGreenlet*)g;

    if (!PyGreenlet_Check(self)) {
        PyErr_BadArgument();
        return NULL;
    }

    if (args == NULL) {
        args = Py_BuildValue("()");
    }
    else {
        Py_INCREF(args);
    }

    if (kwargs != NULL && PyDict_Check(kwargs)) {
        Py_INCREF(kwargs);
    }
    else {
        kwargs = NULL;
    }

    return single_result(g_switch(self, args, kwargs));
}

static PyObject*
PyGreenlet_Throw(PyGreenlet* self, PyObject* typ, PyObject* val, PyObject* tb)
{
    if (!PyGreenlet_Check(self)) {
        PyErr_BadArgument();
        return NULL;
    }
    Py_INCREF(typ);
    Py_XINCREF(val);
    Py_XINCREF(tb);
    return throw_greenlet(self, typ, val, tb);
}

/** End C API ****************************************************************/

static PyMethodDef green_methods[] = {
    {"switch",
     (PyCFunction)green_switch,
     METH_VARARGS | METH_KEYWORDS,
     green_switch_doc},
    {"throw", (PyCFunction)green_throw, METH_VARARGS, green_throw_doc},
    {"__getstate__", (PyCFunction)green_getstate, METH_NOARGS, NULL},
    {NULL, NULL} /* sentinel */
};

static PyGetSetDef green_getsets[] = {
    {"__dict__", (getter)green_getdict, (setter)green_setdict, /*XXX*/ NULL},
    {"run", (getter)green_getrun, (setter)green_setrun, /*XXX*/ NULL},
    {"parent", (getter)green_getparent, (setter)green_setparent, /*XXX*/ NULL},
    {"gr_frame", (getter)green_getframe, NULL, /*XXX*/ NULL},
    {"gr_context",
     (getter)green_getcontext,
     (setter)green_setcontext,
     /*XXX*/ NULL},
    {"dead", (getter)green_getdead, NULL, /*XXX*/ NULL},
    {"_stack_saved", (getter)green_get_stack_saved, NULL, /*XXX*/ NULL},
    {NULL}};

static PyMemberDef green_members[] = {
    {NULL}
};

static PyNumberMethods green_as_number = {
    NULL, /* nb_add */
    NULL, /* nb_subtract */
    NULL, /* nb_multiply */
#if PY_MAJOR_VERSION < 3
    NULL, /* nb_divide */
#endif
    NULL,                /* nb_remainder */
    NULL,                /* nb_divmod */
    NULL,                /* nb_power */
    NULL,                /* nb_negative */
    NULL,                /* nb_positive */
    NULL,                /* nb_absolute */
    (inquiry)green_bool, /* nb_bool */
};


PyTypeObject PyGreenlet_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "greenlet.greenlet", /* tp_name */
    sizeof(PyGreenlet),  /* tp_basicsize */
    0,                   /* tp_itemsize */
    /* methods */
    (destructor)green_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_compare */
    (reprfunc)green_repr,      /* tp_repr */
    &green_as_number,          /* tp_as _number*/
    0,                         /* tp_as _sequence*/
    0,                         /* tp_as _mapping*/
    0,                         /* tp_hash */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer*/
    G_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    "greenlet(run=None, parent=None) -> greenlet\n\n"
    "Creates a new greenlet object (without running it).\n\n"
    " - *run* -- The callable to invoke.\n"
    " - *parent* -- The parent greenlet. The default is the current "
    "greenlet.",                        /* tp_doc */
    (traverseproc)green_traverse, /* tp_traverse */
    (inquiry)green_clear,         /* tp_clear */
    0,                                  /* tp_richcompare */
    offsetof(PyGreenlet, weakreflist),  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    green_methods,                      /* tp_methods */
    green_members,                      /* tp_members */
    green_getsets,                      /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    offsetof(PyGreenlet, dict),         /* tp_dictoffset */
    (initproc)green_init,               /* tp_init */
    PyType_GenericAlloc,                  /* tp_alloc */
    (newfunc)green_new,                          /* tp_new */
    PyObject_GC_Del,                   /* tp_free */
    (inquiry)green_is_gc,         /* tp_is_gc */
};



PyDoc_STRVAR(mod_getcurrent_doc,
             "getcurrent() -> greenlet\n"
             "\n"
             "Returns the current greenlet (i.e. the one which called this "
             "function).\n");

static PyObject*
mod_getcurrent(PyObject* self)
{
    return GET_THREAD_STATE().state().get_or_establish_current_object();
}

PyDoc_STRVAR(mod_settrace_doc,
             "settrace(callback) -> object\n"
             "\n"
             "Sets a new tracing function and returns the previous one.\n");
static PyObject*
mod_settrace(PyObject* self, PyObject* args)
{
    PyObject* tracefunc;
    if (!PyArg_ParseTuple(args, "O", &tracefunc)) {
        return NULL;
    }
    ThreadState& state = GET_THREAD_STATE();
    PyObject* previous = state.get_tracefunc();
    if (previous == NULL) {
        previous = Py_None;
        Py_INCREF(previous);
    }

    if (tracefunc == Py_None) {
        state.del_tracefunc();
    }
    else {
        state.set_tracefunc(tracefunc);
    }
    return previous;
}

PyDoc_STRVAR(mod_gettrace_doc,
             "gettrace() -> object\n"
             "\n"
             "Returns the currently set tracing function, or None.\n");

static PyObject*
mod_gettrace(PyObject* self)
{
    PyObject* tracefunc = GET_THREAD_STATE().state().get_tracefunc();
    if (tracefunc == NULL) {
        tracefunc = Py_None;
        Py_INCREF(tracefunc);
    }
    return tracefunc;
}

PyDoc_STRVAR(mod_set_thread_local_doc,
             "set_thread_local(key, value) -> None\n"
             "\n"
             "Set a value in the current thread-local dictionary. Debbuging only.\n");

static PyObject*
mod_set_thread_local(PyObject* mod, PyObject* args)
{
    PyObject* key;
    PyObject* value;
    PyObject* result = NULL;
    // PyArg borrows refs, do not decrement.
    if (PyArg_UnpackTuple(args, "set_thread_local", 2, 2, &key, &value)) {
        if(PyDict_SetItem(
                          PyThreadState_GetDict(), // borrow
                          key,
                          value) == 0 ) {
            // success
            Py_INCREF(Py_None);
            result = Py_None;
        }
    }
    return result;
}

PyDoc_STRVAR(mod_get_pending_cleanup_count_doc,
             "get_pending_cleanup_count() -> Integer\n"
             "\n"
             "Get the number of greenlet cleanup operations pending. Testing only.\n");


static PyObject*
mod_get_pending_cleanup_count(PyObject* mod)
{
    G_MUTEX_ACQUIRE(g_cleanup_queue_lock);
    PyObject* result = PyLong_FromSize_t(g_cleanup_queue.size());
    G_MUTEX_RELEASE(g_cleanup_queue_lock);
    return result;
}

static PyMethodDef GreenMethods[] = {
    {"getcurrent",
     (PyCFunction)mod_getcurrent,
     METH_NOARGS,
     mod_getcurrent_doc},
    {"settrace", (PyCFunction)mod_settrace, METH_VARARGS, mod_settrace_doc},
    {"gettrace", (PyCFunction)mod_gettrace, METH_NOARGS, mod_gettrace_doc},
    {"set_thread_local", (PyCFunction)mod_set_thread_local, METH_VARARGS, mod_set_thread_local_doc},
    {"get_pending_cleanup_count", (PyCFunction)mod_get_pending_cleanup_count, METH_NOARGS, mod_get_pending_cleanup_count_doc},
    {NULL, NULL} /* Sentinel */
};

static const char* const copy_on_greentype[] = {
    "getcurrent", "error", "GreenletExit", "settrace", "gettrace", NULL};

#if PY_MAJOR_VERSION >= 3
#    define INITERROR return NULL

static struct PyModuleDef greenlet_module_def = {
    PyModuleDef_HEAD_INIT,
    "greenlet._greenlet",
    NULL,
    -1,
    GreenMethods,
};

PyMODINIT_FUNC
PyInit__greenlet(void)
#else
#    define INITERROR return

PyMODINIT_FUNC
init_greenlet(void)
#endif
{
    PyObject* m = NULL;
    const char* const*  p = NULL;
    PyObject* c_api_object;
    static void* _PyGreenlet_API[PyGreenlet_API_pointers];

    GREENLET_NOINLINE_INIT();
    G_MUTEX_INIT(g_cleanup_queue_lock);
    if (!G_MUTEX_INIT_SUCCESS(g_cleanup_queue_lock)) {
        PyErr_SetString(PyExc_MemoryError, "can't allocate lock");
        INITERROR;
    }
#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&greenlet_module_def);
#else
    m = Py_InitModule("greenlet._greenlet", GreenMethods);
#endif
    if (m == NULL) {
        INITERROR;
    }

    ts_event_switch = Greenlet_Intern("switch");
    ts_event_throw = Greenlet_Intern("throw");

    if (PyType_Ready(&PyGreenlet_Type) < 0) {
        INITERROR;
    }
    PyMainGreenlet_Type.tp_base = &PyGreenlet_Type;
    Py_INCREF(&PyGreenlet_Type);
    // On Py27, if we don't manually inherit the flags, we don't get
    // Py_TPFLAGS_HAVE_CLASS, which breaks lots of things, notably
    // type checking for the subclass. We also wind up inheriting
    // HAVE_GC, which means we must set those fields as well, since if
    // its explicitly set they don't get copied
    PyMainGreenlet_Type.tp_flags = G_TPFLAGS_DEFAULT;
    PyMainGreenlet_Type.tp_traverse = (traverseproc)green_traverse;
    PyMainGreenlet_Type.tp_clear = (inquiry)green_clear;
    PyMainGreenlet_Type.tp_is_gc = (inquiry)green_is_gc;

    if (PyType_Ready(&PyMainGreenlet_Type) < 0) {
        INITERROR;
    }
#if G_USE_STANDARD_THREADING == 0
    if (PyType_Ready(&PyGreenletCleanup_Type) < 0) {
        INITERROR;
    }
#endif
    PyExc_GreenletError = PyErr_NewException("greenlet.error", NULL, NULL);
    if (PyExc_GreenletError == NULL) {
        INITERROR;
    }
    PyExc_GreenletExit =
        PyErr_NewException("greenlet.GreenletExit", PyExc_BaseException, NULL);
    if (PyExc_GreenletExit == NULL) {
        INITERROR;
    }

    ts_empty_tuple = PyTuple_New(0);
    if (ts_empty_tuple == NULL) {
        INITERROR;
    }

    ts_empty_dict = PyDict_New();
    if (ts_empty_dict == NULL) {
        INITERROR;
    }


    Py_INCREF(&PyGreenlet_Type);
    PyModule_AddObject(m, "greenlet", (PyObject*)&PyGreenlet_Type);
    Py_INCREF(PyExc_GreenletError);
    PyModule_AddObject(m, "error", PyExc_GreenletError);
    Py_INCREF(PyExc_GreenletExit);
    PyModule_AddObject(m, "GreenletExit", PyExc_GreenletExit);

    PyModule_AddObject(m, "GREENLET_USE_GC", PyBool_FromLong(1));
    PyModule_AddObject(m, "GREENLET_USE_TRACING", PyBool_FromLong(1));
    PyModule_AddObject(
        m, "GREENLET_USE_CONTEXT_VARS", PyBool_FromLong(GREENLET_PY37));
    PyModule_AddObject(m, "GREENLET_USE_STANDARD_THREADING", PyBool_FromLong(G_USE_STANDARD_THREADING));

    /* also publish module-level data as attributes of the greentype. */
    /* XXX: Why? */
    for (p = copy_on_greentype; *p; p++) {
        PyObject* o = PyObject_GetAttrString(m, *p);
        if (!o) {
            continue;
        }
        PyDict_SetItemString(PyGreenlet_Type.tp_dict, *p, o);
        Py_DECREF(o);
    }

    /*
     * Expose C API
     */

    /* types */
    _PyGreenlet_API[PyGreenlet_Type_NUM] = (void*)&PyGreenlet_Type;

    /* exceptions */
    _PyGreenlet_API[PyExc_GreenletError_NUM] = (void*)PyExc_GreenletError;
    _PyGreenlet_API[PyExc_GreenletExit_NUM] = (void*)PyExc_GreenletExit;

    /* methods */
    _PyGreenlet_API[PyGreenlet_New_NUM] = (void*)PyGreenlet_New;
    _PyGreenlet_API[PyGreenlet_GetCurrent_NUM] = (void*)PyGreenlet_GetCurrent;
    _PyGreenlet_API[PyGreenlet_Throw_NUM] = (void*)PyGreenlet_Throw;
    _PyGreenlet_API[PyGreenlet_Switch_NUM] = (void*)PyGreenlet_Switch;
    _PyGreenlet_API[PyGreenlet_SetParent_NUM] = (void*)PyGreenlet_SetParent;

    /* XXX: Note that our module name is ``greenlet._greenlet``, but for
       backwards compatibility with existing C code, we need the _C_API to
       be directly in greenlet.
    */
    c_api_object =
        PyCapsule_New((void*)_PyGreenlet_API, "greenlet._C_API", NULL);
    if (c_api_object != NULL) {
        PyModule_AddObject(m, "_C_API", c_api_object);
    }

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}

#ifdef __clang__
#    pragma clang diagnostic pop
#elif defined(__GNUC__)
#    pragma GCC diagnostic pop
#endif
