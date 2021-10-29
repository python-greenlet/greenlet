/*
 * this is the internal transfer function.
 *
 * HISTORY
 * 24-Nov-02  Christian Tismer  <tismer@tismer.com>
 *      needed to add another magic constant to insure
 *      that f in slp_eval_frame(PyFrameObject *f)
 *      STACK_REFPLUS will probably be 1 in most cases.
 *      gets included into the saved stack area.
 * 26-Sep-02  Christian Tismer  <tismer@tismer.com>
 *      again as a result of virtualized stack access,
 *      the compiler used less registers. Needed to
 *      explicit mention registers in order to get them saved.
 *      Thanks to Jeff Senn for pointing this out and help.
 * 17-Sep-02  Christian Tismer  <tismer@tismer.com>
 *      after virtualizing stack save/restore, the
 *      stack size shrunk a bit. Needed to introduce
 *      an adjustment STACK_MAGIC per platform.
 * 15-Sep-02  Gerd Woetzel       <gerd.woetzel@GMD.DE>
 *      slightly changed framework for sparc
 * 01-Mar-02  Christian Tismer  <tismer@tismer.com>
 *      Initial final version after lots of iterations for i386.
 */

#define alloca _alloca

#define STACK_REFPLUS 1

#ifdef SLP_EVAL

#define STACK_MAGIC 0

/* Some magic to quell warnings and keep slp_switch() from crashing when built
   with VC90. Disable global optimizations, and the warning: frame pointer
   register 'ebp' modified by inline assembly code.

   We used to just disable global optimizations ("g") but upstream stackless
   Python, as well as stackman, turn off all optimizations.

References:
https://github.com/stackless-dev/stackman/blob/dbc72fe5207a2055e658c819fdeab9731dee78b9/stackman/platforms/switch_x86_msvc.h
https://github.com/stackless-dev/stackless/blob/main-slp/Stackless/platf/switch_x86_msvc.h
*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#pragma optimize("", off) /* so that autos are stored on the stack */
#pragma warning(disable:4731)
#pragma warning(disable:4733) /* disable warning about modifying FS[0] */

/*
 * XXX: From all the documentation I've read, we should only need to
 * store/restore the FS:[0], aka NT_TIB.ExceptionList, in order to be able to
 * handle exceptions correctly. But that doesn't work. Suppose we start a
 * greenlet, then go back to the main greenlet, then deallocate the child
 * greenlet, which raises a GreenletExit in Python; normally we translate that
 * into a C++ exception too, after the stacks have been successfully swapped
 * from main to child. However, even if we do that raising in a new function
 * called through a pointer, and we've verified that the SEH is correctly
 * saved and restored, the process terminates with error code 1 when we try to
 * throw the exception.
 *
 * Documentation claims that std::terminate() calls abort which terminates the
 * process with error code 3. So that can't be happening.
 *
 * Neither are we getting a Windows Access Violation, the process is just
 * exiting.
 *
 * This happens no matter which compiler options we use to handle exceptions.
 *
 * Indications from commit fc9515879b9d514c4d8244584a1fd2b2895e5e0f in 2011
 * are that saving the SEH was enough to solve at least some problems with
 * Visual C++, but apparently that's not enough anymore. This is true in both
 * an old compiler ("Visual C++ for Python\9.0"), and a much newer compiler
 * ("Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30133"). The
 * current (as of 2021-10-28) upstream of stackman saves only the SEH, but
 * uses quite a different switching function. The current upstream of
 * stackless Python does nothing with SEH.
 *
 * Help would be appreciated.
 */
//#define GREENLET_CANNOT_USE_EXCEPTIONS_NEAR_SWITCH 1

#define GREENLET_NEEDS_EXCEPTION_STATE_SAVED

static void*
slp_get_exception_state()
{
    return (void*)__readfsdword(FIELD_OFFSET(NT_TIB, ExceptionList));
}

static void
slp_set_exception_state(const void *const seh_state)
{
    __writefsdword(FIELD_OFFSET(NT_TIB, ExceptionList), seh_state);
}

typedef struct _GExceptionRegistration {
    struct _GExceptionRegistration* prev;
    void* handler_f;
} GExceptionRegistration;

static void
slp_show_seh_chain()
{
    GExceptionRegistration* seh_state = (GExceptionRegistration*)__readfsdword(FIELD_OFFSET(NT_TIB, ExceptionList));
    while (seh_state && seh_state != (GExceptionRegistration*)0xFFFFFFFF) {
        fprintf(stderr, "\tSEH_chain addr: %p handler: %p prev: %p\n",
                seh_state,
                seh_state->handler_f, seh_state->prev);
        if ((void*)seh_state->prev < (void*)100) {
            fprintf(stderr, "\tERROR: Broken chain.\n");
            break;
        }
        seh_state = seh_state->prev;
    }
}

static int
slp_switch(void)
{
    /* MASM systax is typically reversed from other assemblers.
       It is usually <instruction> <destination> <source>
     */
    int *stackref, stsizediff;
    /* store the structured exception state for this stack */
    DWORD seh_state = __readfsdword(FIELD_OFFSET(NT_TIB, ExceptionList));
    fprintf(stderr, "\nslp_switch: Saving seh_state %p for %p\n",
            seh_state, switching_thread_state);
    slp_show_seh_chain();
    __asm mov stackref, esp;
    /* modify EBX, ESI and EDI in order to get them preserved */
    __asm mov ebx, ebx;
    __asm xchg esi, edi;
    {
        SLP_SAVE_STATE(stackref, stsizediff);
        __asm {
            mov     eax, stsizediff
            add     esp, eax
            add     ebp, eax
        }
        SLP_RESTORE_STATE();
    }
    fprintf(stderr, "slp_switch: Replacing seh_state %p with %p for %p\n",
            __readfsdword(FIELD_OFFSET(NT_TIB, ExceptionList)),
            seh_state,
            switching_thread_state);
    // Traversing before the return is likely to be invalid because
    // it references things on the stack that have just moved.
    //fprintf(stderr, "slp_switch: Before replacement:\n");
    //slp_show_seh_chain();
    fprintf(stderr, "slp_switch: After replacement for %p:\n", switching_thread_state);
    __writefsdword(FIELD_OFFSET(NT_TIB, ExceptionList), seh_state);
    slp_show_seh_chain();
    return 0;
}

/* re-enable ebp warning and global optimizations. */
#pragma optimize("", on)
#pragma warning(default:4731)
#pragma warning(default:4733) /* disable warning about modifying FS[0] */


#endif

/*
 * further self-processing support
 */

/* we have IsBadReadPtr available, so we can peek at objects */
#define STACKLESS_SPY

#ifdef IMPLEMENT_STACKLESSMODULE
#include "Windows.h"
#define CANNOT_READ_MEM(p, bytes) IsBadReadPtr(p, bytes)

static int IS_ON_STACK(void*p)
{
    int stackref;
    int stackbase = ((int)&stackref) & 0xfffff000;
    return (int)p >= stackbase && (int)p < stackbase + 0x00100000;
}

#endif
