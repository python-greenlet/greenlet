#ifndef GREENLET_PYTHON_STATE_CPP
#define GREENLET_PYTHON_STATE_CPP

#include <Python.h>
#include "greenlet_greenlet.hpp"

// --------------------------------------------------------------------------

void
PyFrameStack_Init(PyFrameStack *fs)
{
    memset(fs, 0, sizeof(*fs));

#if GREENLET_USE_CFRAME
    fs->cframe = &PyThreadState_GET()->root_cframe;
#endif
}


void
PyFrameStack_Save(PyFrameStack *fs, const PyThreadState *const tstate)
{
#if PY_VERSION_HEX >= 0x30C0000
    fs->trash_delete_nesting = tstate->trash.delete_nesting;
#else
    fs->trash_delete_nesting = tstate->trash_delete_nesting;
#endif

#if PY_VERSION_HEX >= 0x30C0000
    fs->py_recursion_depth = tstate->py_recursion_limit - tstate->py_recursion_remaining;
    fs->c_recursion_depth = C_RECURSION_LIMIT - tstate->c_recursion_remaining;
#elif PY_VERSION_HEX >= 0x30B0000
    fs->recursion_depth = tstate->recursion_limit - tstate->recursion_remaining;
#else
    fs->recursion_depth = tstate->recursion_depth;
#endif

#if GREENLET_USE_CFRAME
    fs->cframe = tstate->cframe;
#endif

#if GREENLET_USE_CFRAME && PY_VERSION_HEX < 0x30C0000
    fs->use_tracing = tstate->cframe->use_tracing;
#endif

#if PY_VERSION_HEX >= 0x30B0000
    fs->current_frame = tstate->cframe->current_frame;
#endif

#if PY_VERSION_HEX >= 0x30B0000
    fs->datastack_chunk = tstate->datastack_chunk;
    fs->datastack_top = tstate->datastack_top;
    fs->datastack_limit = tstate->datastack_limit;
#endif
}


void
PyFrameStack_Restore(PyFrameStack *fs, PyThreadState *tstate)
{
#if PY_VERSION_HEX >= 0x30C0000
    tstate->trash.delete_nesting = fs->trash_delete_nesting;
#else
    tstate->trash_delete_nesting = fs->trash_delete_nesting;
#endif

#if PY_VERSION_HEX >= 0x30C0000
    tstate->py_recursion_remaining = tstate->py_recursion_limit - fs->py_recursion_depth;
    tstate->c_recursion_remaining = C_RECURSION_LIMIT - fs->c_recursion_depth;
#elif PY_VERSION_HEX >= 0x30B0000
    tstate->recursion_remaining = tstate->recursion_limit - fs->recursion_depth;
#else
    tstate->recursion_depth = fs->recursion_depth;
#endif

#if GREENLET_USE_CFRAME
    tstate->cframe = fs->cframe;
#endif

#if GREENLET_USE_CFRAME && PY_VERSION_HEX < 0x30C0000
    tstate->cframe->use_tracing = fs->use_tracing;
#endif

#if PY_VERSION_HEX >= 0x30B0000
    tstate->cframe->current_frame = fs->current_frame;
#endif

#if PY_VERSION_HEX >= 0x30B0000
    tstate->datastack_chunk = fs->datastack_chunk;
    tstate->datastack_top = fs->datastack_top;
    tstate->datastack_limit = fs->datastack_limit;
#endif
}


void
PyFrameStack_UpdateRecursionDepth(PyFrameStack *fs, const PyThreadState *const tstate)
{
#if PY_VERSION_HEX >= 0x30C0000
    fs->py_recursion_depth = tstate->py_recursion_limit - tstate->py_recursion_remaining;
    fs->c_recursion_depth = tstate->py_recursion_limit - tstate->py_recursion_remaining;
#elif PY_VERSION_HEX >= 0x30B0000
    fs->recursion_depth = tstate->recursion_limit - tstate->recursion_remaining;
#else
    fs->recursion_depth = tstate->recursion_depth;
#endif
}


#if GREENLET_USE_CFRAME
void
PyFrameStack_UpdateCFrame(PyFrameStack *fs, _PyCFrame& frame)
{
    frame = *PyThreadState_GET()->cframe;
    fs->cframe = &frame;
    fs->cframe->previous = &PyThreadState_GET()->root_cframe;
}
#endif


void
PyFrameStack_UpdateTracing(PyFrameStack *fs, PyThreadState *const origin_tstate)
{
#if GREENLET_USE_CFRAME && PY_VERSION_HEX < 0x30C0000
    fs->use_tracing = origin_tstate->cframe->use_tracing;
#endif
}


void
PyFrameStack_DidFinish(PyFrameStack *fs, PyThreadState* tstate)
{
#if PY_VERSION_HEX >= 0x30B0000
    PyObjectArenaAllocator alloc;
    _PyStackChunk* chunk = NULL;
    if (tstate) {
        chunk = tstate->datastack_chunk;

        PyObject_GetArenaAllocator(&alloc);
        tstate->datastack_chunk = NULL;
        tstate->datastack_limit = NULL;
        tstate->datastack_top = NULL;

    }
    else if (fs->datastack_chunk) {
        chunk = fs->datastack_chunk;
        PyObject_GetArenaAllocator(&alloc);
    }

    if (alloc.free && chunk) {
        while (chunk) {
            _PyStackChunk *prev = chunk->previous;
            chunk->previous = NULL;
            alloc.free(alloc.ctx, chunk, chunk->size);
            chunk = prev;
        }
    }

    fs->datastack_chunk = NULL;
    fs->datastack_limit = NULL;
    fs->datastack_top = NULL;
#endif
}

// --------------------------------------------------------------------------

namespace greenlet {

PythonState::PythonState()
    : _top_frame()
{
    PyFrameStack_Init(&this->frame_stack);
}


inline void PythonState::may_switch_away() noexcept
{
#if GREENLET_PY311
    // PyThreadState_GetFrame is probably going to have to allocate a
    // new frame object. That may trigger garbage collection. Because
    // we call this during the early phases of a switch (it doesn't
    // matter to which greenlet, as this has a global effect), if a GC
    // triggers a switch away, two things can happen, both bad:
    // - We might not get switched back to, halting forward progress.
    //   this is pathological, but possible.
    // - We might get switched back to with a different set of
    //   arguments or a throw instead of a switch. That would corrupt
    //   our state (specifically, PyErr_Occurred() and this->args()
    //   would no longer agree).
    //
    // Thus, when we call this API, we need to have GC disabled.
    // This method serves as a bottleneck we call when maybe beginning
    // a switch. In this way, it is always safe -- no risk of GC -- to
    // use ``_GetFrame()`` whenever we need to, just as it was in
    // <=3.10 (because subsequent calls will be cached and not
    // allocate memory).

    GCDisabledGuard no_gc;
    Py_XDECREF(PyThreadState_GetFrame(PyThreadState_GET()));
#endif
}

void PythonState::operator<<(const PyThreadState *const tstate) noexcept
{
    PyFrameStack_Save(&this->frame_stack, tstate);
    this->_context.steal(tstate->context);
#if GREENLET_PY311
    PyFrameObject *frame = PyThreadState_GetFrame((PyThreadState *)tstate);
    Py_XDECREF(frame);  // PyThreadState_GetFrame gives us a new
                        // reference.
    this->_top_frame.steal(frame);
#else // Not 311
    this->_top_frame.steal(tstate->frame);
#endif // GREENLET_PY311
}

#if GREENLET_PY312
void GREENLET_NOINLINE(PythonState::unexpose_frames)()
{
    if (!this->top_frame()) {
        return;
    }

    // See GreenletState::expose_frames() and the comment on frames_were_exposed
    // for more information about this logic.
    _PyInterpreterFrame *iframe = this->_top_frame->f_frame;
    while (iframe != nullptr) {
        _PyInterpreterFrame *prev_exposed = iframe->previous;
        assert(iframe->frame_obj);
        memcpy(&iframe->previous, &iframe->frame_obj->_f_frame_data[0],
               sizeof(void *));
        iframe = prev_exposed;
    }
}
#else
void PythonState::unexpose_frames()
{}
#endif

void PythonState::operator>>(PyThreadState *const tstate) noexcept
{
    PyFrameStack_Restore(&this->frame_stack, tstate);
    tstate->context = this->_context.relinquish_ownership();
    /* Incrementing this value invalidates the contextvars cache,
       which would otherwise remain valid across switches */
    tstate->context_ver++;
#if GREENLET_PY311
  #if GREENLET_PY312
    this->unexpose_frames();
  #endif // GREENLET_PY312
    this->_top_frame.relinquish_ownership();
#else // not 3.11
    tstate->frame = this->_top_frame.relinquish_ownership();
#endif // GREENLET_PY311
}

inline void PythonState::will_switch_from(PyThreadState *const origin_tstate) noexcept
{
    PyFrameStack_UpdateTracing(&this->frame_stack, origin_tstate);
}

void PythonState::set_initial_state(const PyThreadState* const tstate) noexcept
{
    PyFrameStack_UpdateRecursionDepth(&this->frame_stack, tstate);
    this->_top_frame = nullptr;
}

// TODO: Better state management about when we own the top frame.
int PythonState::tp_traverse(visitproc visit, void* arg, bool own_top_frame) noexcept
{
    Py_VISIT(this->_context.borrow());
    if (own_top_frame) {
        Py_VISIT(this->_top_frame.borrow());
    }
    return 0;
}

void PythonState::tp_clear(bool own_top_frame) noexcept
{
    PythonStateContext::tp_clear();
    // If we get here owning a frame,
    // we got dealloc'd without being finished. We may or may not be
    // in the same thread.
    if (own_top_frame) {
        this->_top_frame.CLEAR();
    }
}

#if GREENLET_USE_CFRAME
void PythonState::set_new_cframe(_PyCFrame& frame) noexcept
{
    PyFrameStack_UpdateCFrame(&this->frame_stack, frame);
}
#endif

const PythonState::OwnedFrame& PythonState::top_frame() const noexcept
{
    return this->_top_frame;
}

void PythonState::did_finish(PyThreadState* tstate) noexcept
{
    PyFrameStack_DidFinish(&this->frame_stack, tstate);
}


}; // namespace greenlet

#endif // GREENLET_PYTHON_STATE_CPP
