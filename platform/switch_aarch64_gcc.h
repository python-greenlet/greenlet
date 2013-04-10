/*
 * this is the internal transfer function.
 *
 * HISTORY
 * 08-Apr-13 File creation. Michael Matz
 *
 * NOTES
 *
 * Simply save all callee saved registers
 *
 */

#define STACK_REFPLUS 1

#ifdef SLP_EVAL
#define STACK_MAGIC 0
#define REGS_TO_SAVE "r19", "r20", "r21", "r22", "r23", "r24", "r25", "r26", \
                     "r27", "r28", "r30" /* aka lr */, \
                     "v8", "v9", "v10", "v11", \
                     "v12", "v13", "v14", "v15"

static int
slp_switch(void)
{
	void *fp;
        register int *stackref, stsizediff;
        __asm__ volatile ("" : : : REGS_TO_SAVE);
	__asm__ volatile ("str x29, %0" : "=m"(fp) : :);
        __asm__ ("mov %0, sp" : "=r" (stackref));
        {
                SLP_SAVE_STATE(stackref, stsizediff);
                __asm__ volatile (
                    "add sp,sp,%0\n"
		    "add x29,x29,%0\n"
                    :
                    : "r" (stsizediff)
                    );
                SLP_RESTORE_STATE();
        }
	__asm__ volatile ("ldr x29, %0" : : "m" (fp) :);
        __asm__ volatile ("" : : : REGS_TO_SAVE);
        return 0;
}

#endif
