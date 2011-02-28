/*
 * this is the internal transfer function.
 *
 * HISTORY
 * 08-Mar-11  Floris Bruynooghe <flub@devork.be>
 *      No need to set return value register explicitly
 *      before the stack and framepointer are adjusted
 *      as none of the other registers are influenced by
 *      this.  Also don't needlessly clean the windows
 *      ('ta %0" :: "i" (ST_CLEAN_WINDOWS)') as that
 *      clobbers the gcc PIC register (%l7).
 * 24-Nov-02  Christian Tismer  <tismer@tismer.com>
 *      needed to add another magic constant to insure
 *      that f in slp_eval_frame(PyFrameObject *f)
 *      STACK_REFPLUS will probably be 1 in most cases.
 *      gets included into the saved stack area.
 * 17-Sep-02  Christian Tismer  <tismer@tismer.com>
 *      after virtualizing stack save/restore, the
 *      stack size shrunk a bit. Needed to introduce
 *      an adjustment STACK_MAGIC per platform.
 * 15-Sep-02  Gerd Woetzel       <gerd.woetzel@GMD.DE>
 *      added support for SunOS sparc with gcc
 */

#define STACK_REFPLUS 1

#ifdef SLP_EVAL

#include <sys/trap.h>

#define STACK_MAGIC 0

static int
slp_switch(void)
{
    register int *stackref, stsizediff;

    /* Flush SPARC register windows onto the stack, so they can be used to
     * restore the registers after the stack has been switched out and
     * restored.  Then put the stack pointer into stackref. */
    __asm__ volatile (
        "ta %1\n\t"
        "mov %%sp, %0"
        : "=r" (stackref) :  "i" (ST_FLUSH_WINDOWS));

    {
        /* Thou shalt put SLP_SAVE_STATE into a local block */
        /* Copy the current stack onto the heap */
        SLP_SAVE_STATE(stackref, stsizediff);

        /* Increment stack and frame pointer by stsizediff */
        __asm__ volatile (
            "add %0, %%sp, %%sp\n\t"
            "add %0, %%fp, %%fp"
            : : "r" (stsizediff));

        /* Copy new stack from it's save store on the heap */
        SLP_RESTORE_STATE();

        /* No need to restore any registers from the stack nor clear them: the
         * frame pointer has just been set and the return value register is
         * also being set by the return statement below.  After returning a
         * restore instruction is given and the frame below us will load all
         * it's registers using a fill_trap if required. */

        return 0;
    }
}

#endif

/*
 * further self-processing support
 */

/*
 * if you want to add self-inspection tools, place them
 * here. See the x86_msvc for the necessary defines.
 * These features are highly experimental und not
 * essential yet.
 */
