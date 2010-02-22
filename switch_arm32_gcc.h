/*
 * this is the internal transfer function.
 *
 * HISTORY
 * 14-Aug-06 File creation. Ported from Arm Thumb. Sylvain Baro
 *  3-Sep-06 Commented out saving of r1-r3 (r4 already commented out) as I
 *           read that these do not need to be saved.  Also added notes and
 *           errors related to the frame pointer. Richard Tew.
 *
 * NOTES
 *
 *   It is not possible to detect if fp is used or not, so the supplied
 *   switch function needs to support it, so that you can remove it if
 *   it does not apply to you.
 *
 * POSSIBLE ERRORS
 *
 *   "fp cannot be used in asm here"
 *
 *   - Try commenting out "fp" in REGS_TO_SAVE.
 *
 */

#define STACK_REFPLUS 1

#ifdef SLP_EVAL
#define STACK_MAGIC 0
#define REGS_TO_SAVE /*"r1", "r2", "r3", "r4",*/ "r5", "r6", "fp", "ip", "lr"

static int
slp_switch(void)
{
        register int *stackref, stsizediff;
        __asm__ volatile ("" : : : REGS_TO_SAVE);
        __asm__ ("mov %0,sp" : "=g" (stackref));
        {
                SLP_SAVE_STATE(stackref, stsizediff);
                __asm__ volatile (
                    "add sp,sp,%0\n"
                    "add fp,fp,%0\n"
                    :
                    : "r" (stsizediff)
                    );
                SLP_RESTORE_STATE();
                return 0;
        }
        __asm__ volatile ("" : : : REGS_TO_SAVE);
}

#endif