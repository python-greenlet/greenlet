/*
 * this is the internal transfer function.
 *
 * HISTORY
 * 30-Aug-13  Floris Bruynooghe <flub@devork.be>
        Clean the register windows again before returning.
        This does not clobber the PIC register as it leaves
        the current window intact and is required for multi-
        threaded code to work correctly.
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


#define STACK_MAGIC 0
#define ST_FLUSH_WINDOWS 0x03
#define ST_CLEAN_WINDOWS 0x04

static int
slp_switch(void)
{
    register int *stackref, stsizediff;

    /* Flush SPARC register windows onto the stack, so they can be used to
     * restore the registers after the stack has been switched out and
     * restored.  This also ensures the current window (pointed at by
     * the CWP register) is the only window left in the registers
     * (CANSAVE=0, CANRESTORE=0), that means the registers of our
     * caller are no longer there and when we return they will always
     * be loaded from the stack by a window underflow/fill trap.
     *
     * On SPARC v9 and above it might be more efficient to use the
     * FLUSHW instruction instead of TA ST_FLUSH_WINDOWS.  But that
     * requires the correct -mcpu flag to gcc.
     *
     * Then put the stack pointer into stackref. */
    __asm__ volatile (
#ifdef __sparcv9
        "flushw\n\t"
#else
        "ta %1\n\t"
#endif
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

        /* No need to set the return value register, the return
         * statement below does this just fine.  After returning a restore
         * instruction is given and a fill-trap will load all the registers
         * from the stack if needed.  However in a multi-threaded environment
         * we can't guarantee the other register windows are fine to use by
         * their threads anymore, so tell the CPU to clean them. */
        __asm__ volatile ("ta %0" : : "i" (ST_CLEAN_WINDOWS));

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
