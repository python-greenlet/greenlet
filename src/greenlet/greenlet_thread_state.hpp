#ifndef GREENLET_THREAD_STATE_HPP
#define GREENLET_THREAD_STATE_HPP

#include <stdexcept>

#include "greenlet_internal.hpp"
#include "greenlet_refs.hpp"
#include "greenlet_thread_support.hpp"

using greenlet::refs::BorrowedObject;
using greenlet::refs::BorrowedGreenlet;
using greenlet::refs::BorrowedMainGreenlet;
using greenlet::refs::OwnedMainGreenlet;
using greenlet::refs::OwnedObject;
using greenlet::refs::OwnedGreenlet;
using greenlet::refs::OwnedList;
using greenlet::refs::PyErrFetchParam;
using greenlet::refs::PyArgParseParam;
using greenlet::refs::ImmortalString;
using greenlet::refs::CreatedModule;
using greenlet::refs::PyErrPieces;
using greenlet::refs::NewReference;

namespace greenlet {
/**
 * Thread-local state of greenlets.
 *
 * Each native thread will get exactly one of these objects,
 * autmatically accessed through the best available thread-local
 * mechanism the compiler supports (``thread_local`` for C++11
 * compilers or ``__thread``/``declspec(thread)`` for older GCC/clang
 * or MSVC, respectively.)
 *
 * Previously, we kept thread-local state mostly in a bunch of
 * ``static volatile`` variables in the main greenlet file.. This had
 * the problem of requiring extra checks, loops, and great care
 * accessing these variables if we potentially invoked any Python code
 * that could relaese the GIL, because the state could change out from
 * under us. Making the variables thread-local solves this problem.
 *
 * When we detected that a greenlet API accessing the current greenlet
 * was invoked from a different thread than the greenlet belonged to,
 * we stored a reference to the greenlet in the Python thread
 * dictionary for the thread the greenlet belonged to. This could lead
 * to memory leaks if the thread then exited (because of a reference
 * cycle, as greenlets referred to the thread dictionary, and deleting
 * non-current greenlets leaked their frame plus perhaps arguments on
 * the C stack). If a thread exited while still having running
 * greenlet objects (perhaps that had just switched back to the main
 * greenlet), and did not invoke one of the greenlet APIs *in that
 * thread, immediately before it exited, without some other thread
 * then being invoked*, such a leak was guaranteed.
 *
 * This can be partly solved by using compiler thread-local variables
 * instead of the Python thread dictionary, thus avoiding a cycle.
 *
 * To fully solve this problem, we need a reliable way to know that a
 * thread is done and we should clean up the main greenlet. On POSIX,
 * we can use the destructor function of ``pthread_key_create``, but
 * there's nothing similar on Windows; a C++11 thread local object
 * reliably invokes its destructor when the thread it belongs to exits
 * (non-C++11 compilers offer ``__thread`` or ``declspec(thread)`` to
 * create thread-local variables, but they can't hold C++ objects that
 * invoke destructors; the C++11 version is the most portable solution
 * I found). When the thread exits, we can drop references and
 * otherwise manipulate greenlets and frames that we know can no
 * longer be switched to. For compilers that don't support C++11
 * thread locals, we have a solution that uses the python thread
 * dictionary, though it may not collect everything as promptly as
 * other compilers do, if some other library is using the thread
 * dictionary and has a cycle or extra reference.
 *
 * There are two small wrinkles. The first is that when the thread
 * exits, it is too late to actually invoke Python APIs: the Python
 * thread state is gone, and the GIL is released. To solve *this*
 * problem, our destructor uses ``Py_AddPendingCall`` to transfer the
 * destruction work to the main thread. (This is not an issue for the
 * dictionary solution.)
 *
 * The second is that once the thread exits, the thread local object
 * is invalid and we can't even access a pointer to it, so we can't
 * pass it to ``Py_AddPendingCall``. This is handled by actually using
 * a second object that's thread local (ThreadStateCreator) and having
 * it dynamically allocate this object so it can live until the
 * pending call runs.
 */



class ThreadState {
private:
    // As of commit 08ad1dd7012b101db953f492e0021fb08634afad
    // this class needed 56 bytes in o Py_DEBUG build
    // on 64-bit macOS 11.
    // Adding the vector takes us up to 80 bytes ()

    /* Strong reference to the main greenlet */
    OwnedMainGreenlet main_greenlet;

    /* Strong reference to the current greenlet. */
    OwnedGreenlet current_greenlet;

    /* Strong reference to the trace function, if any. */
    OwnedObject tracefunc;

    /* A vector of PyGreenlet pointers representing things that need
       deleted when this thread is running. The vector owns the
       references, but you need to manually INCREF/DECREF as you use
       them. */
    greenlet::g_deleteme_t deleteme;

#ifdef GREENLET_NEEDS_EXCEPTION_STATE_SAVED
    void* exception_state;
#endif

    static ImmortalString get_referrers_name;
    static PythonAllocator<ThreadState> allocator;

    G_NO_COPIES_OF_CLS(ThreadState);

public:
    static void* operator new(size_t UNUSED(count))
    {
        return ThreadState::allocator.allocate(1);
    }

    static void operator delete(void* ptr)
    {
        return ThreadState::allocator.deallocate(static_cast<ThreadState*>(ptr),
                                                 1);
    }

    ThreadState()
    {
        this->main_greenlet = OwnedMainGreenlet::consuming(green_create_main());
        this->current_greenlet = main_greenlet;

        if(!this->main_greenlet) {
            // We failed to create the main greenlet. That's bad.
            Py_FatalError("Failed to create main greenlet");
            throw PyErrOccurred();
        }
        // The main greenlet starts with 2 refs: The returned one, and
        // the internal self ref. We then copied it to the current
        // greenlet.
        assert(this->main_greenlet.REFCNT() == 3);
        this->main_greenlet.borrow()->thread_state = this;
        ThreadState::get_referrers_name = "get_referrers";

#ifdef GREENLET_NEEDS_EXCEPTION_STATE_SAVED
        this->exception_state = slp_get_exception_state();
#endif
    }

    inline void restore_exception_state()
    {
#ifdef GREENLET_NEEDS_EXCEPTION_STATE_SAVED
        // It's probably important this be inlined and only call C
        // functions to avoid adding an SEH frame.
        slp_set_exception_state(this->exception_state);
#endif
    }

    inline bool has_main_greenlet()
    {
        return !!this->main_greenlet;
    }

    inline BorrowedMainGreenlet borrow_main_greenlet()
    {
        assert(this->main_greenlet);
        assert(this->main_greenlet.REFCNT() >= 2);
        return this->main_greenlet;
    };

    inline OwnedMainGreenlet get_main_greenlet()
    {
        return this->main_greenlet;
    }

    inline BorrowedGreenlet borrow_current() const
    {
        return this->current_greenlet;
    }

    /**
     * In addition to returning a new reference to the currunt
     * greenlet, this perfroms any maintenance needed.
     */
    inline OwnedGreenlet get_current()
    {
        /* green_dealloc() cannot delete greenlets from other threads, so
           it stores them in the thread dict; delete them now. */
        this->clear_deleteme_list();
        assert(this->current_greenlet->main_greenlet == this->main_greenlet);
        assert(this->main_greenlet->main_greenlet == this->main_greenlet);
        return this->current_greenlet;
    };

    // inline bool is_current(const void* obj) const
    // {
    //     return this->current_greenlet.borrow() == obj;
    // }

    template<typename T>
    inline bool is_current(const refs::PyObjectPointer<T>& obj) const
    {
        return this->current_greenlet.borrow_o() == obj.borrow_o();
    }

    inline void set_current(const OwnedGreenlet& target)
    {
        this->current_greenlet = target;
    }

private:
    /**
     * Deref and remove the greenlets from the deleteme list. Must be
     * holding the GIL.
     *
     * If *murder* is true, then we must be called from a different
     * thread than the one that these greenlets were running in.
     * In that case, if the greenlet was actually running, we destroy
     * the frame reference and otherwise make it appear dead before
     * proceeding; otherwise, we would try (and fail) to raise an
     * exception in it and wind up right back in this list.
     */
    inline void clear_deleteme_list(const bool murder=false)
    {
        if (!this->deleteme.empty()) {
            // It's possible we could add items to this list while
            // running Python code if there's a thread switch, so we
            // need to defensively copy it before that can happen.
            g_deleteme_t copy = this->deleteme;
            this->deleteme.clear(); // in case things come back on the list
            for(greenlet::g_deleteme_t::iterator it = copy.begin(), end = copy.end();
                it != end;
                ++it ) {
                PyGreenlet* to_del = *it;
                if (murder) {
                    // Force each greenlet to appear dead; we can't raise an
                    // exception into it anymore anyway.
                    // XXX: If they have saved stack, it leaks!
                    to_del->pimpl->main_greenlet.CLEAR();

                    if (PyGreenlet_ACTIVE(to_del)) {
                        assert(to_del->pimpl->python_state.has_top_frame());
                        to_del->pimpl->stack_state.set_inactive();
                        assert(!PyGreenlet_ACTIVE(to_del));

                        // We're holding a borrowed reference to the last
                        // frame we executed. Since we borrowed it, the
                        // normal traversal, clear, and dealloc functions
                        // ignore it, meaning it leaks. (The thread state
                        // object can't find it to clear it when that's
                        // deallocated either, because by definition if we
                        // got an object on this list, it wasn't
                        // running and the thread state doesn't have
                        // this frame.)
                        // So here, we *do* clear it.
                        to_del->pimpl->python_state.tp_clear(true);
                    }
                }

                // The only reference to these greenlets should be in
                // this list, decreffing them should let them be
                // deleted again, triggering calls to green_dealloc()
                // in the correct thread (if we're not murdering).
                // This may run arbitrary Python code and switch
                // threads or greenlets!
                Py_DECREF(to_del);
                if (PyErr_Occurred()) {
                    PyErr_WriteUnraisable(nullptr);
                    PyErr_Clear();
                }
            }
        }
    }

public:

    /**
     * Returns a new reference, or a false object.
     */
    inline OwnedObject get_tracefunc() const
    {
        return tracefunc;
    };


    inline void set_tracefunc(BorrowedObject tracefunc)
    {
        assert(tracefunc);
        if (tracefunc == BorrowedObject(Py_None)) {
            this->tracefunc.CLEAR();
        }
        else {
#ifndef NDEBUG
            Py_ssize_t old_refs = Py_REFCNT(tracefunc);
#endif
            this->tracefunc = tracefunc;
            assert(this->tracefunc.REFCNT() == old_refs + 1);
        }
    }

    /**
     * Given a reference to a greenlet that some other thread
     * attempted to delete (has a refcount of 0) store it for later
     * deletion when the thread this state belongs to is current.
     */
    inline void delete_when_thread_running(PyGreenlet* to_del)
    {
        Py_INCREF(to_del);
        this->deleteme.push_back(to_del);
    }

    ~ThreadState()
    {
        if (!PyInterpreterState_Head()) {
            // We shouldn't get here (our callers protect us)
            // but if we do, all we can do is bail early.
            return;
        }

        // We should not have an "origin" greenlet; that only exists
        // for the temporary time during a switch, which should not
        // be in progress as the thread dies.
        //assert(!this->switching_state.origin);

        this->tracefunc.CLEAR();

        // Forcibly GC as much as we can.
        this->clear_deleteme_list(true);

        // The pending call did this.
        assert(this->main_greenlet.borrow()->thread_state == NULL);

        // If the main greenlet is the current greenlet,
        // then we "fell off the end" and the thread died.
        // It's possible that there is some other greenlet that
        // switched to us, leaving a reference to the main greenlet
        // on the stack, somewhere uncollectable. Try to detect that.
        if (this->current_greenlet == this->main_greenlet && this->current_greenlet) {
            assert(PyGreenlet_MAIN(this->main_greenlet));
            assert(!this->current_greenlet->python_state.has_top_frame());
            assert(this->main_greenlet->main_greenlet == this->main_greenlet.borrow());
            // Break a cycle we know about, the self reference
            // the main greenlet keeps.
            this->main_greenlet->main_greenlet.CLEAR();
            // Drop one reference we hold.
            this->current_greenlet.CLEAR();
            assert(!this->current_greenlet);
            // Only our reference to the main greenlet should be left,
            // But hold onto the pointer in case we need to do extra cleanup.
            PyMainGreenlet* old_main_greenlet = this->main_greenlet.borrow();
            Py_ssize_t cnt = this->main_greenlet.REFCNT();
            this->main_greenlet.CLEAR();
            if (cnt == 2 && Py_REFCNT(old_main_greenlet) == 1) {
                // Highly likely that the reference is somewhere on
                // the stack, not reachable by GC. Verify.
                // XXX: This is O(n) in the total number of objects.
                // TODO: Add a way to disable this at runtime, and
                // another way to report on it.
                NewReference gc(PyImport_ImportModule("gc"));
                if (gc) {
                    OwnedObject get_referrers = gc.PyRequireAttr(ThreadState::get_referrers_name);
                    OwnedList refs(get_referrers.PyCall(old_main_greenlet));
                    if (refs && refs.empty()) {
                        assert(refs.REFCNT() == 1);
                        // We found nothing! So we left a dangling
                        // reference: Probably the last thing some
                        // other greenlet did was call
                        // 'getcurrent().parent.switch()' to switch
                        // back to us. Clean it up. This will be the
                        // case on CPython 3.7 and newer, as they use
                        // an internal calling convertion that avoids
                        // creating method objects and storing them on
                        // the stack.
                        Py_DECREF(old_main_greenlet);
                    }
                    else if (refs
                             && refs.size() == 1
                             && PyCFunction_Check(refs.at(0))
                             && Py_REFCNT(refs.at(0)) == 2) {
                        assert(refs.REFCNT() == 1);
                        // Ok, we found a C method that refers to the
                        // main greenlet, and its only referenced
                        // twice, once in the list we just created,
                        // once from...somewhere else. If we can't
                        // find where else, then this is a leak.
                        // This happens in older versions of CPython
                        // that create a bound method object somewhere
                        // on the stack that we'll never get back to.
                        if (PyCFunction_GetFunction(refs.at(0).borrow()) == (PyCFunction)green_switch) {
                            BorrowedObject function_w = refs.at(0);
                            refs.clear(); // destroy the reference
                                          // from the list.
                            // back to one reference. Can *it* be
                            // found?
                            assert(function_w.REFCNT() == 1);
                            refs = get_referrers.PyCall(function_w);
                            if (refs && refs.empty()) {
                                // Nope, it can't be found so it won't
                                // ever be GC'd. Drop it.
                                Py_CLEAR(function_w);
                            }
                        }
                    }
                }
            }
        }

        // We need to make sure this greenlet appears to be dead,
        // because otherwise deallocing it would fail to raise an
        // exception in it (the thread is dead) and put it back in our
        // deleteme list.
        if (this->current_greenlet) {
            OwnedGreenlet& g = this->current_greenlet;
            assert(!g->python_state.has_top_frame());
            g->main_greenlet.CLEAR();

            // XXX: This could now put us in an invalid state, with
            // this->current_greenlet not being valid anymore.
            Py_DECREF(this->current_greenlet.borrow());
        }

        if (this->main_greenlet) {
            // Couldn't have been the main greenlet that was running
            // when the thread exited (because we already cleared this
            // pointer if it was). This shouldn't be possible?
            assert(PyGreenlet_MAIN(this->main_greenlet));
            // If the main greenlet was current when the thread died (it
            // should be, right?) then we cleared its self pointer above
            // when we cleared the current greenlet's main greenlet pointer.
            assert(this->main_greenlet->main_greenlet == this->main_greenlet
                   || !this->main_greenlet->main_greenlet);
            // self reference, probably gone
            this->main_greenlet->main_greenlet.CLEAR();

            // This will actually go away when the ivar is destructed.
            this->main_greenlet.CLEAR();
        }

        if (PyErr_Occurred()) {
            PyErr_WriteUnraisable(NULL);
            PyErr_Clear();
        }

    }

};

ImmortalString ThreadState::get_referrers_name(nullptr);
PythonAllocator<ThreadState> ThreadState::allocator;

template<typename Destructor>
class ThreadStateCreator
{
private:
    ThreadState* _state;
    G_NO_COPIES_OF_CLS(ThreadStateCreator);
public:

    // Only one of these, auto created per thread
    ThreadStateCreator() :
        _state(0)
    {
    }

    ~ThreadStateCreator()
    {
        Destructor(this->_state);
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
    // inline PyGreenlet* borrow_target()
    // {
    //     return this->state().borrow_target();
    // };
    inline PyGreenlet* borrow_current()
    {
        return this->state().borrow_current().borrow();
    };
    // inline PyGreenlet* borrow_origin()
    // {
    //     return this->state().borrow_origin().borrow();
    // };
    inline PyMainGreenlet* borrow_main_greenlet()
    {
        return this->state().borrow_main_greenlet().borrow();
    };

};

#if G_USE_STANDARD_THREADING == 1
// We can't use the PythonAllocator for this, because we push to it
// from the thread state destructor, which doesn't have the GIL,
// and Python's allocators can only be called with the GIL.
typedef std::vector<ThreadState*> cleanup_queue_t;
#else
class cleanup_queue_t {
public:
    inline ssize_t size() const { return 0; };
    inline bool empty() const { return true; };
    inline void pop_back()
    {
        throw std::out_of_range("empty queue.");
    };
    inline ThreadState* back()
    {
        throw std::out_of_range("empty queue.");
    };
    inline void push_back(ThreadState* g)
    {
        throw std::out_of_range("empty queue.");
    };
};
#endif
}; // namespace greenlet

#endif
