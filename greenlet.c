/* vim:set noet ts=8 sw=8 : */

#define GREENLET_MODULE

#include "greenlet.h"
#include "structmember.h"


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

/* Python <= 2.5 support */
#if PY_MAJOR_VERSION < 3
#ifndef Py_REFCNT
#  define Py_REFCNT(ob) (((PyObject *) (ob))->ob_refcnt)
#endif
#ifndef Py_TYPE
#  define Py_TYPE(ob)   (((PyObject *) (ob))->ob_type)
#endif
#ifndef PyVarObject_HEAD_INIT
#  define PyVarObject_HEAD_INIT(type, size) \
    PyObject_HEAD_INIT(type) size,
#endif
#endif

#if PY_VERSION_HEX < 0x02050000
typedef int Py_ssize_t;
#endif

extern PyTypeObject PyGreenlet_Type;

/* Defines that customize greenlet module behaviour */
#ifndef GREENLET_USE_GC
#define GREENLET_USE_GC 1
#endif

#ifndef GREENLET_USE_TRACING
#define GREENLET_USE_TRACING 1
#endif

/* Weak reference to the switching-to greenlet during the slp switch */
static PyGreenlet* volatile ts_target = NULL;
/* Strong reference to the switching from greenlet after the switch */
static PyGreenlet* volatile ts_origin = NULL;
/* Strong reference to the current greenlet in this thread state */
static PyGreenlet* volatile ts_current = NULL;
/* NULL if error, otherwise args tuple to pass around during slp switch */
static PyObject* volatile ts_passaround_args = NULL;
static PyObject* volatile ts_passaround_kwargs = NULL;

/***********************************************************/
/* Thread-aware routines, switching global variables when needed */

#define STATE_OK    (ts_current->run_info == PyThreadState_GET()->dict \
			|| !green_updatecurrent())

static PyObject* ts_curkey;
static PyObject* ts_delkey;
#if GREENLET_USE_TRACING
static PyObject* ts_tracekey;
static PyObject* ts_event_switch;
static PyObject* ts_event_throw;
#endif
static PyObject* PyExc_GreenletError;
static PyObject* PyExc_GreenletExit;
static PyObject* ts_empty_tuple;
static PyObject* ts_empty_dict;

#if GREENLET_USE_GC
#define GREENLET_GC_FLAGS Py_TPFLAGS_HAVE_GC
#define GREENLET_tp_alloc PyType_GenericAlloc
#define GREENLET_tp_free PyObject_GC_Del
#define GREENLET_tp_traverse green_traverse
#define GREENLET_tp_clear green_clear
#define GREENLET_tp_is_gc green_is_gc
#else /* GREENLET_USE_GC */
#define GREENLET_GC_FLAGS 0
#define GREENLET_tp_alloc 0
#define GREENLET_tp_free 0
#define GREENLET_tp_traverse 0
#define GREENLET_tp_clear 0
#define GREENLET_tp_is_gc 0
#endif /* !GREENLET_USE_GC */

static PyGreenlet* green_create_main(void)
{
	PyGreenlet* gmain;
	PyObject* dict = PyThreadState_GetDict();
	if (dict == NULL) {
		if (!PyErr_Occurred())
			PyErr_NoMemory();
		return NULL;
	}

	/* create the main greenlet for this thread */
	gmain = (PyGreenlet*) PyType_GenericAlloc(&PyGreenlet_Type, 0);
	if (gmain == NULL)
		return NULL;
	gmain->stack_start = (char*) 1;
	gmain->stack_stop = (char*) -1;
	gmain->run_info = dict;
	Py_INCREF(dict);
	return gmain;
}

static int green_updatecurrent(void)
{
	PyObject *exc, *val, *tb;
	PyThreadState* tstate;
	PyGreenlet* current;
	PyGreenlet* previous;
	PyObject* deleteme;

green_updatecurrent_restart:
	/* save current exception */
	PyErr_Fetch(&exc, &val, &tb);

	/* get ts_current from the active tstate */
	tstate = PyThreadState_GET();
	if (tstate->dict && (current =
	    (PyGreenlet*) PyDict_GetItem(tstate->dict, ts_curkey))) {
		/* found -- remove it, to avoid keeping a ref */
		Py_INCREF(current);
		PyDict_DelItem(tstate->dict, ts_curkey);
	}
	else {
		/* first time we see this tstate */
		current = green_create_main();
		if (current == NULL) {
			Py_XDECREF(exc);
			Py_XDECREF(val);
			Py_XDECREF(tb);
			return -1;
		}
	}
	assert(current->run_info == tstate->dict);

green_updatecurrent_retry:
	/* update ts_current as soon as possible, in case of nested switches */
	Py_INCREF(current);
	previous = ts_current;
	ts_current = current;

	/* save ts_current as the current greenlet of its own thread */
	if (PyDict_SetItem(previous->run_info, ts_curkey, (PyObject*) previous)) {
		Py_DECREF(previous);
		Py_DECREF(current);
		Py_XDECREF(exc);
		Py_XDECREF(val);
		Py_XDECREF(tb);
		return -1;
	}
	Py_DECREF(previous);

	/* green_dealloc() cannot delete greenlets from other threads, so
	   it stores them in the thread dict; delete them now. */
	deleteme = PyDict_GetItem(tstate->dict, ts_delkey);
	if (deleteme != NULL) {
		PyList_SetSlice(deleteme, 0, INT_MAX, NULL);
	}

	if (ts_current != current) {
		/* some Python code executed above and there was a thread switch,
		 * so ts_current points to some other thread again. We need to
		 * delete ts_curkey (it's likely there) and retry. */
		PyDict_DelItem(tstate->dict, ts_curkey);
		goto green_updatecurrent_retry;
	}

	/* release an extra reference */
	Py_DECREF(current);

	/* restore current exception */
	PyErr_Restore(exc, val, tb);

	/* thread switch could happen during PyErr_Restore, in that
	   case there's nothing to do except restart from scratch. */
	if (ts_current->run_info != tstate->dict)
		goto green_updatecurrent_restart;

	return 0;
}

static PyObject* green_statedict(PyGreenlet* g)
{
	while (!PyGreenlet_STARTED(g)) {
		g = g->parent;
		if (g == NULL) {
			/* garbage collected greenlet in chain */
			return NULL;
		}
	}
	return g->run_info;
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

#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
#define GREENLET_NOINLINE_SUPPORTED
#define GREENLET_NOINLINE(name) __attribute__((noinline)) name
#elif defined(_MSC_VER) && (_MSC_VER >= 1300)
#define GREENLET_NOINLINE_SUPPORTED
#define GREENLET_NOINLINE(name) __declspec(noinline) name
#endif

#ifdef GREENLET_NOINLINE_SUPPORTED
/* add forward declarations */
static void GREENLET_NOINLINE(slp_restore_state)(void);
static int GREENLET_NOINLINE(slp_save_state)(char*);
#if !(defined(MS_WIN64) && defined(_M_X64))
static int GREENLET_NOINLINE(slp_switch)(void);
#endif
static int GREENLET_NOINLINE(g_initialstub)(void*);
#define GREENLET_NOINLINE_INIT() do {} while(0)
#else
/* force compiler to call functions via pointers */
static void (*slp_restore_state)(void);
static int (*slp_save_state)(char*);
static int (*slp_switch)(void);
static int (*g_initialstub)(void*);
#define GREENLET_NOINLINE(name) cannot_inline_ ## name
#define GREENLET_NOINLINE_INIT() do { \
	slp_restore_state = GREENLET_NOINLINE(slp_restore_state); \
	slp_save_state = GREENLET_NOINLINE(slp_save_state); \
	slp_switch = GREENLET_NOINLINE(slp_switch); \
	g_initialstub = GREENLET_NOINLINE(g_initialstub); \
} while(0)
#endif

/***********************************************************/

static int g_save(PyGreenlet* g, char* stop)
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
		memcpy(c+sz1, g->stack_start+sz1, sz2-sz1);
		g->stack_copy = c;
		g->stack_saved = sz2;
	}
	return 0;
}

static void GREENLET_NOINLINE(slp_restore_state)(void)
{
	PyGreenlet* g = ts_target;
	PyGreenlet* owner = ts_current;
	
	/* Restore the heap copy back into the C stack */
	if (g->stack_saved != 0) {
		memcpy(g->stack_start, g->stack_copy, g->stack_saved);
		PyMem_Free(g->stack_copy);
		g->stack_copy = NULL;
		g->stack_saved = 0;
	}
	if (owner->stack_start == NULL)
		owner = owner->stack_prev; /* greenlet is dying, skip it */
	while (owner && owner->stack_stop <= g->stack_stop)
		owner = owner->stack_prev; /* find greenlet with more stack */
	g->stack_prev = owner;
}

static int GREENLET_NOINLINE(slp_save_state)(char* stackref)
{
	/* must free all the C stack up to target_stop */
	char* target_stop = ts_target->stack_stop;
	PyGreenlet* owner = ts_current;
	assert(owner->stack_saved == 0);
	if (owner->stack_start == NULL)
		owner = owner->stack_prev;  /* not saved if dying */
	else
		owner->stack_start = stackref;
	
	while (owner->stack_stop < target_stop) {
		/* ts_current is entierely within the area to free */
		if (g_save(owner, owner->stack_stop))
			return -1;  /* XXX */
		owner = owner->stack_prev;
	}
	if (owner != ts_target) {
		if (g_save(owner, target_stop))
			return -1;  /* XXX */
	}
	return 0;
}


/*
 * the following macros are spliced into the OS/compiler
 * specific code, in order to simplify maintenance.
 */

#define SLP_SAVE_STATE(stackref, stsizediff)		\
  stackref += STACK_MAGIC;				\
  if (slp_save_state((char*)stackref)) return -1;	\
  if (!PyGreenlet_ACTIVE(ts_target)) return 1;		\
  stsizediff = ts_target->stack_start - (char*)stackref

#define SLP_RESTORE_STATE()			\
  slp_restore_state()


#define SLP_EVAL
#define slp_switch GREENLET_NOINLINE(slp_switch)
#include "slp_platformselect.h"
#undef slp_switch

#ifndef STACK_MAGIC
#error "greenlet needs to be ported to this platform,\
 or teached how to detect your compiler properly."
#endif /* !STACK_MAGIC */

#ifdef EXTERNAL_ASM
/* CCP addition: Make these functions, to be called from assembler.
 * The token include file for the given platform should enable the
 * EXTERNAL_ASM define so that this is included.
 */

intptr_t slp_save_state_asm(intptr_t *ref) {
    intptr_t diff;
    SLP_SAVE_STATE(ref, diff);
    return diff;
}

void slp_restore_state_asm(void) {
    SLP_RESTORE_STATE();
}

extern int slp_switch(void);

#endif

static int g_switchstack(void)
{
	/* Perform a stack switch according to some global variables
	   that must be set before:
	   - ts_current: current greenlet (holds a reference)
	   - ts_target: greenlet to switch to (weak reference)
	   - ts_passaround_args: NULL if PyErr_Occurred(),
	       else a tuple of args sent to ts_target (holds a reference)
	   - ts_passaround_kwargs: switch kwargs (holds a reference)
	   On return results are passed via global variables as well:
	   - ts_origin: originating greenlet (holds a reference)
	   - ts_current: current greenlet (holds a reference)
	   - ts_passaround_args: NULL if PyErr_Occurred(),
	       else a tuple of args sent to ts_current (holds a reference)
	   - ts_passaround_kwargs: switch kwargs (holds a reference)
	   It is very important that stack switch is 'atomic', i.e. no
	   calls into other Python code allowed (except very few that
	   are safe), because global variables are very fragile.
	*/
	int err;
	{   /* save state */
		PyGreenlet* current = ts_current;
		PyThreadState* tstate = PyThreadState_GET();
		current->recursion_depth = tstate->recursion_depth;
		current->top_frame = tstate->frame;
		current->exc_type = tstate->exc_type;
		current->exc_value = tstate->exc_value;
		current->exc_traceback = tstate->exc_traceback;
	}
	err = slp_switch();
	if (err < 0) {   /* error */
		PyGreenlet* current = ts_current;
		current->top_frame = NULL;
		current->exc_type = NULL;
		current->exc_value = NULL;
		current->exc_traceback = NULL;

		assert(ts_origin == NULL);
		ts_target = NULL;
	}
	else {
		PyGreenlet* target = ts_target;
		PyGreenlet* origin = ts_current;
		PyThreadState* tstate = PyThreadState_GET();
		tstate->recursion_depth = target->recursion_depth;
		tstate->frame = target->top_frame;
		target->top_frame = NULL;
		tstate->exc_type = target->exc_type;
		target->exc_type = NULL;
		tstate->exc_value = target->exc_value;
		target->exc_value = NULL;
		tstate->exc_traceback = target->exc_traceback;
		target->exc_traceback = NULL;

		assert(ts_origin == NULL);
		Py_INCREF(target);
		ts_current = target;
		ts_origin = origin;
		ts_target = NULL;
	}
	return err;
}

#if GREENLET_USE_TRACING
static int
g_calltrace(PyObject* tracefunc, PyObject* event, PyGreenlet* origin, PyGreenlet* target)
{
	PyObject *retval;
	PyObject *exc_type, *exc_val, *exc_tb;
	PyThreadState *tstate;
	PyErr_Fetch(&exc_type, &exc_val, &exc_tb);
	tstate = PyThreadState_GET();
	tstate->tracing++;
	tstate->use_tracing = 0;
	retval = PyObject_CallFunction(tracefunc, "O(OO)", event, origin, target);
	tstate->tracing--;
	tstate->use_tracing = (tstate->tracing <= 0 &&
		((tstate->c_tracefunc != NULL) ||
		(tstate->c_profilefunc != NULL)));
	if (retval == NULL) {
		/* In case of exceptions trace function is removed */
		if (PyDict_GetItem(tstate->dict, ts_tracekey))
			PyDict_DelItem(tstate->dict, ts_tracekey);
		Py_XDECREF(exc_type);
		Py_XDECREF(exc_val);
		Py_XDECREF(exc_tb);
		return -1;
	} else
		Py_DECREF(retval);
	PyErr_Restore(exc_type, exc_val, exc_tb);
	return 0;
}
#endif

static PyObject *
g_switch(PyGreenlet* target, PyObject* args, PyObject* kwargs)
{
	/* _consumes_ a reference to the args tuple and kwargs dict,
	   and return a new tuple reference */
	int err = 0;
	PyObject* run_info;

	/* check ts_current */
	if (!STATE_OK) {
		Py_XDECREF(args);
		Py_XDECREF(kwargs);
		return NULL;
	}
	run_info = green_statedict(target);
	if (run_info == NULL || run_info != ts_current->run_info) {
		Py_XDECREF(args);
		Py_XDECREF(kwargs);
		PyErr_SetString(PyExc_GreenletError, run_info
				? "cannot switch to a different thread"
				: "cannot switch to a garbage collected greenlet");
		return NULL;
	}

	ts_passaround_args = args;
	ts_passaround_kwargs = kwargs;

	/* find the real target by ignoring dead greenlets,
	   and if necessary starting a greenlet. */
	while (target) {
		if (PyGreenlet_ACTIVE(target)) {
			ts_target = target;
			err = g_switchstack();
			break;
		}
		if (!PyGreenlet_STARTED(target)) {
			void* dummymarker;
			ts_target = target;
			err = g_initialstub(&dummymarker);
			if (err == 1) {
				continue; /* retry the switch */
			}
			break;
		}
		target = target->parent;
	}

	/* For a very short time, immediately after the 'atomic'
	   g_switchstack() call, global variables are in a known state.
	   We need to save everything we need, before it is destroyed
	   by calls into arbitrary Python code. */
	args = ts_passaround_args;
	ts_passaround_args = NULL;
	kwargs = ts_passaround_kwargs;
	ts_passaround_kwargs = NULL;
	if (err < 0) {
		/* Turn switch errors into switch throws */
		assert(ts_origin == NULL);
		Py_CLEAR(kwargs);
		Py_CLEAR(args);
	} else {
		PyGreenlet *origin;
#if GREENLET_USE_TRACING
		PyGreenlet *current;
		PyObject *tracefunc;
#endif
		origin = ts_origin;
		ts_origin = NULL;
#if GREENLET_USE_TRACING
		current = ts_current;
		if ((tracefunc = PyDict_GetItem(current->run_info, ts_tracekey)) != NULL) {
			Py_INCREF(tracefunc);
			if (g_calltrace(tracefunc, args ? ts_event_switch : ts_event_throw, origin, current) < 0) {
				/* Turn trace errors into switch throws */
				Py_CLEAR(kwargs);
				Py_CLEAR(args);
			}
			Py_DECREF(tracefunc);
		}
#endif
		Py_DECREF(origin);
	}

	/* We need to figure out what values to pass to the target greenlet
	   based on the arguments that have been passed to greenlet.switch(). If
	   switch() was just passed an arg tuple, then we'll just return that.
	   If only keyword arguments were passed, then we'll pass the keyword
	   argument dict. Otherwise, we'll create a tuple of (args, kwargs) and
	   return both. */
	if (kwargs == NULL)
	{
		return args;
	}
	else if (PyDict_Size(kwargs) == 0)
	{
		Py_DECREF(kwargs);
		return args;
	}
	else if (PySequence_Length(args) == 0)
	{
		Py_DECREF(args);
		return kwargs;
	}
	else
	{
		PyObject *tuple = PyTuple_New(2);
		if (tuple == NULL) {
			Py_DECREF(args);
			Py_DECREF(kwargs);
			return NULL;
		}
		PyTuple_SET_ITEM(tuple, 0, args);
		PyTuple_SET_ITEM(tuple, 1, kwargs);
		return tuple;
	}
}

static PyObject *
g_handle_exit(PyObject *result)
{
	if (result == NULL && PyErr_ExceptionMatches(PyExc_GreenletExit))
	{
		/* catch and ignore GreenletExit */
		PyObject *exc, *val, *tb;
		PyErr_Fetch(&exc, &val, &tb);
		if (val == NULL)
		{
			Py_INCREF(Py_None);
			val = Py_None;
		}
		result = val;
		Py_DECREF(exc);
		Py_XDECREF(tb);
	}
	if (result != NULL)
	{
		/* package the result into a 1-tuple */
		PyObject *r = result;
		result = PyTuple_New(1);
		if (result)
		{
			PyTuple_SET_ITEM(result, 0, r);
		}
		else
		{
			Py_DECREF(r);
		}
	}
	return result;
}

static int GREENLET_NOINLINE(g_initialstub)(void* mark)
{
	int err;
	PyObject *o, *run;
	PyObject *exc, *val, *tb;
	PyObject *run_info;
	PyGreenlet* self = ts_target;
	PyObject* args = ts_passaround_args;
	PyObject* kwargs = ts_passaround_kwargs;

	/* save exception in case getattr clears it */
	PyErr_Fetch(&exc, &val, &tb);
	/* self.run is the object to call in the new greenlet */
	run = PyObject_GetAttrString((PyObject*) self, "run");
	if (run == NULL) {
		Py_XDECREF(exc);
		Py_XDECREF(val);
		Py_XDECREF(tb);
		return -1;
	}
	/* restore saved exception */
	PyErr_Restore(exc, val, tb);

	/* recheck the state in case getattr caused thread switches */
	if (!STATE_OK) {
		Py_DECREF(run);
		return -1;
	}

	/* recheck run_info in case greenlet reparented anywhere above */
	run_info = green_statedict(self);
	if (run_info == NULL || run_info != ts_current->run_info) {
		Py_DECREF(run);
		PyErr_SetString(PyExc_GreenletError, run_info
				? "cannot switch to a different thread"
				: "cannot switch to a garbage collected greenlet");
		return -1;
	}

	/* by the time we got here another start could happen elsewhere,
	 * that means it should now be a regular switch
	 */
	if (PyGreenlet_STARTED(self)) {
		Py_DECREF(run);
		ts_passaround_args = args;
		ts_passaround_kwargs = kwargs;
		return 1;
	}

	/* start the greenlet */
	self->stack_start = NULL;
	self->stack_stop = (char*) mark;
	if (ts_current->stack_start == NULL) {
		/* ts_current is dying */
		self->stack_prev = ts_current->stack_prev;
	}
	else {
		self->stack_prev = ts_current;
	}
	self->top_frame = NULL;
	self->exc_type = NULL;
	self->exc_value = NULL;
	self->exc_traceback = NULL;
	self->recursion_depth = PyThreadState_GET()->recursion_depth;

	/* restore arguments in case they are clobbered */
	ts_target = self;
	ts_passaround_args = args;
	ts_passaround_kwargs = kwargs;

	/* perform the initial switch */
	err = g_switchstack();

	/* returns twice!
	   The 1st time with err=1: we are in the new greenlet
	   The 2nd time with err=0: back in the caller's greenlet
	*/
	if (err == 1) {
		/* in the new greenlet */
		PyGreenlet* origin;
#if GREENLET_USE_TRACING
		PyObject* tracefunc;
#endif
		PyObject* result;
		PyGreenlet* parent;
		self->stack_start = (char*) 1;  /* running */

		/* grab origin while we still can */
		origin = ts_origin;
		ts_origin = NULL;

		/* now use run_info to store the statedict */
		o = self->run_info;
		self->run_info = green_statedict(self->parent);
		Py_INCREF(self->run_info);
		Py_XDECREF(o);

#if GREENLET_USE_TRACING
		if ((tracefunc = PyDict_GetItem(self->run_info, ts_tracekey)) != NULL) {
			Py_INCREF(tracefunc);
			if (g_calltrace(tracefunc, args ? ts_event_switch : ts_event_throw, origin, self) < 0) {
				/* Turn trace errors into switch throws */
				Py_CLEAR(kwargs);
				Py_CLEAR(args);
			}
			Py_DECREF(tracefunc);
		}
#endif
		Py_DECREF(origin);

		if (args == NULL) {
			/* pending exception */
			result = NULL;
		} else {
			/* call g.run(*args, **kwargs) */
			result = PyEval_CallObjectWithKeywords(
				run, args, kwargs);
			Py_DECREF(args);
			Py_XDECREF(kwargs);
		}
		Py_DECREF(run);
		result = g_handle_exit(result);

		/* jump back to parent */
		self->stack_start = NULL;  /* dead */
		for (parent = self->parent; parent != NULL; parent = parent->parent) {
			result = g_switch(parent, result, NULL);
			/* Return here means switch to parent failed,
			 * in which case we throw *current* exception
			 * to the next parent in chain.
			 */
			assert(result == NULL);
		}
		/* We ran out of parents, cannot continue */
		PyErr_WriteUnraisable((PyObject *) self);
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


static PyObject* green_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject* o;
	if (!STATE_OK)
		return NULL;
	
	o = PyBaseObject_Type.tp_new(type, ts_empty_tuple, ts_empty_dict);
	if (o != NULL) {
		Py_INCREF(ts_current);
		((PyGreenlet*) o)->parent = ts_current;
	}
	return o;
}

static int green_setrun(PyGreenlet* self, PyObject* nparent, void* c);
static int green_setparent(PyGreenlet* self, PyObject* nparent, void* c);

static int green_init(PyGreenlet *self, PyObject *args, PyObject *kwargs)
{
	PyObject *run = NULL;
	PyObject* nparent = NULL;
	static char *kwlist[] = {"run", "parent", 0};
	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OO:green", kwlist,
					 &run, &nparent))
		return -1;

	if (run != NULL) {
		if (green_setrun(self, run, NULL))
			return -1;
	}
	if (nparent != NULL && nparent != Py_None)
		return green_setparent(self, nparent, NULL);
	return 0;
}

static int kill_greenlet(PyGreenlet* self)
{
	/* Cannot raise an exception to kill the greenlet if
	   it is not running in the same thread! */
	if (self->run_info == PyThreadState_GET()->dict) {
		/* The dying greenlet cannot be a parent of ts_current
		   because the 'parent' field chain would hold a
		   reference */
		PyObject* result;
		PyGreenlet* oldparent;
		PyGreenlet* tmp;
		if (!STATE_OK) {
			return -1;
		}
		oldparent = self->parent;
		self->parent = ts_current;
		Py_INCREF(self->parent);
		/* Send the greenlet a GreenletExit exception. */
		PyErr_SetNone(PyExc_GreenletExit);
		result = g_switch(self, NULL, NULL);
		tmp = self->parent;
		self->parent = oldparent;
		Py_XDECREF(tmp);
		if (result == NULL)
			return -1;
		Py_DECREF(result);
		return 0;
	}
	else {
		/* Not the same thread! Temporarily save the greenlet
		   into its thread's ts_delkey list. */
		PyObject* lst;
		lst = PyDict_GetItem(self->run_info, ts_delkey);
		if (lst == NULL) {
			lst = PyList_New(0);
			if (lst == NULL || PyDict_SetItem(self->run_info,
							  ts_delkey, lst) < 0)
				return -1;
		}
		if (PyList_Append(lst, (PyObject*) self) < 0)
			return -1;
		if (!STATE_OK)  /* to force ts_delkey to be reconsidered */
			return -1;
		return 0;
	}
}

#if GREENLET_USE_GC
static int
green_traverse(PyGreenlet *self, visitproc visit, void *arg)
{
	/* We must only visit referenced objects, i.e. only objects
	   Py_INCREF'ed by this greenlet (directly or indirectly):
	   - stack_prev is not visited: holds previous stack pointer, but it's not referenced
	   - frames are not visited: alive greenlets are not garbage collected anyway */
	Py_VISIT((PyObject*)self->parent);
	Py_VISIT(self->run_info);
	Py_VISIT(self->exc_type);
	Py_VISIT(self->exc_value);
	Py_VISIT(self->exc_traceback);
	Py_VISIT(self->dict);
	return 0;
}

static int green_is_gc(PyGreenlet* self)
{
	/* Main greenlet can be garbage collected since it can only
	   become unreachable if the underlying thread exited.
	   Active greenlet cannot be garbage collected, however. */
	if (PyGreenlet_MAIN(self) || !PyGreenlet_ACTIVE(self))
		return 1;
	return 0;
}

static int green_clear(PyGreenlet* self)
{
	/* Greenlet is only cleared if it is about to be collected.
	   Since active greenlets are not garbage collectable, we can
	   be sure that, even if they are deallocated during clear,
	   nothing they reference is in unreachable or finalizers,
	   so even if it switches we are relatively safe. */
	Py_CLEAR(self->parent);
	Py_CLEAR(self->run_info);
	Py_CLEAR(self->exc_type);
	Py_CLEAR(self->exc_value);
	Py_CLEAR(self->exc_traceback);
	Py_CLEAR(self->dict);
	return 0;
}
#endif

static void green_dealloc_safe(PyGreenlet* self)
{
	PyObject *error_type, *error_value, *error_traceback;

	if (PyGreenlet_ACTIVE(self) && self->run_info != NULL && !PyGreenlet_MAIN(self)) {
		/* Hacks hacks hacks copied from instance_dealloc() */
		/* Temporarily resurrect the greenlet. */
		assert(Py_REFCNT(self) == 0);
		Py_REFCNT(self) = 1;
		/* Save the current exception, if any. */
		PyErr_Fetch(&error_type, &error_value, &error_traceback);
		if (kill_greenlet(self) < 0) {
			PyErr_WriteUnraisable((PyObject*) self);
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
				PyFile_WriteString("GreenletExit did not kill ",
						   f);
				PyFile_WriteObject((PyObject*) self, f, 0);
				PyFile_WriteString("\n", f);
			}
		}
		/* Restore the saved exception. */
		PyErr_Restore(error_type, error_value, error_traceback);
		/* Undo the temporary resurrection; can't use DECREF here,
		 * it would cause a recursive call.
		 */
		assert(Py_REFCNT(self) > 0);
		if (--Py_REFCNT(self) != 0) {
			/* Resurrected! */
			Py_ssize_t refcnt = Py_REFCNT(self);
			_Py_NewReference((PyObject*) self);
			Py_REFCNT(self) = refcnt;
#if GREENLET_USE_GC
			PyObject_GC_Track((PyObject *)self);
#endif
			_Py_DEC_REFTOTAL;
#ifdef COUNT_ALLOCS
			--Py_TYPE(self)->tp_frees;
			--Py_TYPE(self)->tp_allocs;
#endif /* COUNT_ALLOCS */
			return;
		}
	}
	if (self->weakreflist != NULL)
		PyObject_ClearWeakRefs((PyObject *) self);
	Py_CLEAR(self->parent);
	Py_CLEAR(self->run_info);
	Py_CLEAR(self->exc_type);
	Py_CLEAR(self->exc_value);
	Py_CLEAR(self->exc_traceback);
	Py_CLEAR(self->dict);
	Py_TYPE(self)->tp_free((PyObject*) self);
}

#if GREENLET_USE_GC
static void green_dealloc(PyGreenlet* self)
{
	PyObject_GC_UnTrack((PyObject *)self);
	if (PyObject_IS_GC((PyObject *)self)) {
		Py_TRASHCAN_SAFE_BEGIN(self);
		green_dealloc_safe(self);
		Py_TRASHCAN_SAFE_END(self);
	} else {
		/* This object cannot be garbage collected, so trashcan is not allowed */
		green_dealloc_safe(self);
	}
}
#else
#define green_dealloc green_dealloc_safe
#endif

static PyObject* single_result(PyObject* results)
{
	if (results != NULL && PyTuple_Check(results) &&
	    PyTuple_GET_SIZE(results) == 1) {
		PyObject *result = PyTuple_GET_ITEM(results, 0);
		Py_INCREF(result);
		Py_DECREF(results);
		return result;
	}
	else
		return results;
}

static PyObject *
throw_greenlet(PyGreenlet *self, PyObject *typ, PyObject *val, PyObject *tb)
{
	/* Note: _consumes_ a reference to typ, val, tb */
	PyObject *result = NULL;
	PyErr_Restore(typ, val, tb);
	if (PyGreenlet_STARTED(self) && !PyGreenlet_ACTIVE(self))
	{
		/* dead greenlet: turn GreenletExit into a regular return */
		result = g_handle_exit(result);
	}
	return single_result(g_switch(self, result, NULL));
}

PyDoc_STRVAR(green_switch_doc,
"switch(*args, **kwargs)\n\
\n\
Switch execution to this greenlet.\n\
\n\
If this greenlet has never been run, then this greenlet\n\
will be switched to using the body of self.run(*args, **kwargs).\n\
\n\
If the greenlet is active (has been run, but was switch()'ed\n\
out before leaving its run function), then this greenlet will\n\
be resumed and the return value to its switch call will be\n\
None if no arguments are given, the given argument if one\n\
argument is given, or the args tuple and keyword args dict if\n\
multiple arguments are given.\n\
\n\
If the greenlet is dead, or is the current greenlet then this\n\
function will simply return the arguments using the same rules as\n\
above.");

static PyObject* green_switch(
	PyGreenlet* self,
	PyObject* args,
	PyObject* kwargs)
{
	Py_INCREF(args);
	Py_XINCREF(kwargs);
	return single_result(g_switch(self, args, kwargs));
}

/* Macros required to support Python < 2.6 for green_throw() */
#ifndef PyExceptionClass_Check
#  define PyExceptionClass_Check    PyClass_Check
#endif
#ifndef PyExceptionInstance_Check
#  define PyExceptionInstance_Check PyInstance_Check
#endif
#ifndef PyExceptionInstance_Class
#  define PyExceptionInstance_Class(x) \
	((PyObject *) ((PyInstanceObject *)(x))->in_class)
#endif

PyDoc_STRVAR(green_throw_doc,
"Switches execution to the greenlet ``g``, but immediately raises the\n"
"given exception in ``g``.  If no argument is provided, the exception\n"
"defaults to ``greenlet.GreenletExit``.  The normal exception\n"
"propagation rules apply, as described above.  Note that calling this\n"
"method is almost equivalent to the following::\n"
"\n"
"    def raiser():\n"
"        raise typ, val, tb\n"
"    g_raiser = greenlet(raiser, parent=g)\n"
"    g_raiser.switch()\n"
"\n"
"except that this trick does not work for the\n"
"``greenlet.GreenletExit`` exception, which would not propagate\n"
"from ``g_raiser`` to ``g``.\n");

static PyObject *
green_throw(PyGreenlet *self, PyObject *args)
{
	PyObject *typ = PyExc_GreenletExit;
	PyObject *val = NULL;
	PyObject *tb = NULL;

	if (!PyArg_ParseTuple(args, "|OOO:throw", &typ, &val, &tb))
	{
		return NULL;
	}

	/* First, check the traceback argument, replacing None, with NULL */
	if (tb == Py_None)
	{
		tb = NULL;
	}
	else if (tb != NULL && !PyTraceBack_Check(tb))
	{
		PyErr_SetString(
			PyExc_TypeError,
			"throw() third argument must be a traceback object");
		return NULL;
	}

	Py_INCREF(typ);
	Py_XINCREF(val);
	Py_XINCREF(tb);

	if (PyExceptionClass_Check(typ))
	{
		PyErr_NormalizeException(&typ, &val, &tb);
	}
	else if (PyExceptionInstance_Check(typ))
	{
		/* Raising an instance. The value should be a dummy. */
		if (val && val != Py_None)
		{
			PyErr_SetString(
				PyExc_TypeError,
				"instance exception may not have a separate value");
			goto failed_throw;
		}
		else
		{
			/* Normalize to raise <class>, <instance> */
			Py_XDECREF(val);
			val = typ;
			typ = PyExceptionInstance_Class(typ);
			Py_INCREF(typ);
		}
	}
	else
	{
		/* Not something you can raise. throw() fails. */
		PyErr_Format(
			PyExc_TypeError,
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

static int green_bool(PyGreenlet* self)
{
	return PyGreenlet_ACTIVE(self);
}

static PyObject* green_getdict(PyGreenlet* self, void* c)
{
	if (self->dict == NULL) {
		self->dict = PyDict_New();
		if (self->dict == NULL)
			return NULL;
	}
	Py_INCREF(self->dict);
	return self->dict;
}

static int green_setdict(PyGreenlet* self, PyObject* val, void* c)
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

static PyObject* green_getdead(PyGreenlet* self, void* c)
{
	if (PyGreenlet_ACTIVE(self) || !PyGreenlet_STARTED(self))
		Py_RETURN_FALSE;
	else
		Py_RETURN_TRUE;
}

static PyObject* green_getrun(PyGreenlet* self, void* c)
{
	if (PyGreenlet_STARTED(self) || self->run_info == NULL) {
		PyErr_SetString(PyExc_AttributeError, "run");
		return NULL;
	}
	Py_INCREF(self->run_info);
	return self->run_info;
}

static int green_setrun(PyGreenlet* self, PyObject* nrun, void* c)
{
	PyObject* o;
	if (PyGreenlet_STARTED(self)) {
		PyErr_SetString(PyExc_AttributeError,
				"run cannot be set "
				"after the start of the greenlet");
		return -1;
	}
	o = self->run_info;
	self->run_info = nrun;
	Py_XINCREF(nrun);
	Py_XDECREF(o);
	return 0;
}

static PyObject* green_getparent(PyGreenlet* self, void* c)
{
	PyObject* result = self->parent ? (PyObject*) self->parent : Py_None;
	Py_INCREF(result);
	return result;
}

static int green_setparent(PyGreenlet* self, PyObject* nparent, void* c)
{
	PyGreenlet* p;
	PyObject* run_info = NULL;
	if (nparent == NULL) {
		PyErr_SetString(PyExc_AttributeError, "can't delete attribute");
		return -1;
	}
	if (!PyGreenlet_Check(nparent)) {
		PyErr_SetString(PyExc_TypeError, "parent must be a greenlet");
		return -1;
	}
	for (p=(PyGreenlet*) nparent; p; p=p->parent) {
		if (p == self) {
			PyErr_SetString(PyExc_ValueError, "cyclic parent chain");
			return -1;
		}
		run_info = PyGreenlet_ACTIVE(p) ? p->run_info : NULL;
	}
	if (run_info == NULL) {
		PyErr_SetString(PyExc_ValueError, "parent must not be garbage collected");
		return -1;
	}
	if (PyGreenlet_STARTED(self) && self->run_info != run_info) {
		PyErr_SetString(PyExc_ValueError, "parent cannot be on a different thread");
		return -1;
	}
	p = self->parent;
	self->parent = (PyGreenlet*) nparent;
	Py_INCREF(nparent);
	Py_XDECREF(p);
	return 0;
}

static PyObject* green_getframe(PyGreenlet* self, void* c)
{
	PyObject* result = self->top_frame ? (PyObject*) self->top_frame : Py_None;
	Py_INCREF(result);
	return result;
}

static PyObject* green_getstate(PyGreenlet* self)
{
	PyErr_Format(PyExc_TypeError,
		"cannot serialize '%s' object", Py_TYPE(self)->tp_name);
	return NULL;
}


/*****************************************************************************
 * C interface
 *
 * These are exported using the CObject API
 */

static PyGreenlet *
PyGreenlet_GetCurrent(void)
{
	if (!STATE_OK) {
		return NULL;
	}
	Py_INCREF(ts_current);
	return ts_current;
	/* Shouldn't this return a borrowed reference instead? See comments in `PyGreenlet_New` */
}

static int
PyGreenlet_SetParent(PyGreenlet *g, PyGreenlet *nparent)
{
	if (!PyGreenlet_Check(g)) {
		PyErr_SetString(PyExc_TypeError, "parent must be a greenlet");
		return -1;
	}

	return green_setparent((PyGreenlet*) g, (PyObject *) nparent, NULL);
}

static PyGreenlet *
PyGreenlet_New(PyObject *run, PyGreenlet *parent)
{
	PyGreenlet* g = NULL;

	g = (PyGreenlet *) PyType_GenericAlloc(&PyGreenlet_Type, 0);
	if (g == NULL) {
		return NULL;
	}

	if (run != NULL) {
		Py_INCREF(run);
		g->run_info = run;
	}
	if (parent == NULL) {
		parent = PyGreenlet_GetCurrent(); // `parent` ref += 1
	}
	PyGreenlet_SetParent(g, parent); // `parent` ref += 1
	return g;
}

static PyObject *
PyGreenlet_Switch(PyGreenlet *g, PyObject *args, PyObject *kwargs)
{
	PyGreenlet *self = (PyGreenlet *) g;

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

static PyObject *
PyGreenlet_Throw(PyGreenlet *self, PyObject *typ, PyObject *val, PyObject *tb)
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
    {"switch", (PyCFunction)green_switch,
     METH_VARARGS | METH_KEYWORDS, green_switch_doc},
    {"throw",  (PyCFunction)green_throw,  METH_VARARGS, green_throw_doc},
    {"__getstate__", (PyCFunction)green_getstate, METH_NOARGS, NULL},
    {NULL,     NULL}		/* sentinel */
};

static PyGetSetDef green_getsets[] = {
	{"__dict__", (getter)green_getdict,
		     (setter)green_setdict, /*XXX*/ NULL},
	{"run",    (getter)green_getrun,
		   (setter)green_setrun, /*XXX*/ NULL},
	{"parent", (getter)green_getparent,
		   (setter)green_setparent, /*XXX*/ NULL},
	{"gr_frame", (getter)green_getframe,
	             NULL, /*XXX*/ NULL},
	{"dead",   (getter)green_getdead,
	             NULL, /*XXX*/ NULL},
	{NULL}
};

static PyNumberMethods green_as_number = {
	NULL,		/* nb_add */
	NULL,		/* nb_subtract */
	NULL,		/* nb_multiply */
#if PY_MAJOR_VERSION < 3
	NULL,		/* nb_divide */
#endif
	NULL,		/* nb_remainder */
	NULL,		/* nb_divmod */
	NULL,		/* nb_power */
	NULL,		/* nb_negative */
	NULL,		/* nb_positive */
	NULL,		/* nb_absolute */
	(inquiry)green_bool,	/* nb_bool */
};


PyTypeObject PyGreenlet_Type = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"greenlet.greenlet",			/* tp_name */
	sizeof(PyGreenlet),			/* tp_basicsize */
	0,					/* tp_itemsize */
	/* methods */
	(destructor)green_dealloc,		/* tp_dealloc */
	0,					/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	0,					/* tp_compare */
	0,					/* tp_repr */
	&green_as_number,			/* tp_as _number*/
	0,					/* tp_as _sequence*/
	0,					/* tp_as _mapping*/
	0, 					/* tp_hash */
	0,					/* tp_call */
	0,					/* tp_str */
	0,					/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer*/
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | GREENLET_GC_FLAGS,	/* tp_flags */
	"greenlet(run=None, parent=None) -> greenlet\n\n"
	"Creates a new greenlet object (without running it).\n\n"
	" - *run* -- The callable to invoke.\n"
	" - *parent* -- The parent greenlet. The default is the current "
	"greenlet.",                            /* tp_doc */
	(traverseproc)GREENLET_tp_traverse,	/* tp_traverse */
	(inquiry)GREENLET_tp_clear,		/* tp_clear */
	0,					/* tp_richcompare */
	offsetof(PyGreenlet, weakreflist),	/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	green_methods,				/* tp_methods */
	0,					/* tp_members */
	green_getsets,				/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	offsetof(PyGreenlet, dict),		/* tp_dictoffset */
	(initproc)green_init,			/* tp_init */
	GREENLET_tp_alloc,			/* tp_alloc */
	green_new,				/* tp_new */
	GREENLET_tp_free,			/* tp_free */        
	(inquiry)GREENLET_tp_is_gc,		/* tp_is_gc */
};

static PyObject* mod_getcurrent(PyObject* self)
{
	if (!STATE_OK)
		return NULL;
	Py_INCREF(ts_current);
	return (PyObject*) ts_current;
}

#if GREENLET_USE_TRACING
static PyObject* mod_settrace(PyObject* self, PyObject* args)
{
	int err;
	PyObject* previous;
	PyObject* tracefunc;
	PyGreenlet* current;
	if (!PyArg_ParseTuple(args, "O", &tracefunc))
		return NULL;
	if (!STATE_OK)
		return NULL;
	current = ts_current;
	previous = PyDict_GetItem(current->run_info, ts_tracekey);
	if (previous == NULL)
		previous = Py_None;
	Py_INCREF(previous);
	if (tracefunc == Py_None)
		err = previous != Py_None ? PyDict_DelItem(current->run_info, ts_tracekey) : 0;
	else
		err = PyDict_SetItem(current->run_info, ts_tracekey, tracefunc);
	if (err < 0)
		Py_CLEAR(previous);
	return previous;
}

static PyObject* mod_gettrace(PyObject* self)
{
	PyObject* tracefunc;
	if (!STATE_OK)
		return NULL;
	tracefunc = PyDict_GetItem(ts_current->run_info, ts_tracekey);
	if (tracefunc == NULL)
		tracefunc = Py_None;
	Py_INCREF(tracefunc);
	return tracefunc;
}
#endif

static PyMethodDef GreenMethods[] = {
	{"getcurrent", (PyCFunction)mod_getcurrent, METH_NOARGS, /*XXX*/ NULL},
#if GREENLET_USE_TRACING
	{"settrace", (PyCFunction)mod_settrace, METH_VARARGS, NULL},
	{"gettrace", (PyCFunction)mod_gettrace, METH_NOARGS, NULL},
#endif
	{NULL,     NULL}        /* Sentinel */
};

static char* copy_on_greentype[] = {
	"getcurrent",
	"error",
	"GreenletExit",
#if GREENLET_USE_TRACING
	"settrace",
	"gettrace",
#endif
	NULL
};

#if PY_MAJOR_VERSION >= 3
#define INITERROR return NULL

static struct PyModuleDef greenlet_module_def = {
	PyModuleDef_HEAD_INIT,
	"greenlet",
	NULL,
	-1,
	GreenMethods,
};

PyMODINIT_FUNC
PyInit_greenlet(void)
#else
#define INITERROR return

PyMODINIT_FUNC
initgreenlet(void)
#endif
{
	PyObject* m = NULL;
	char** p = NULL;
	PyObject *c_api_object;
	static void *_PyGreenlet_API[PyGreenlet_API_pointers];

	GREENLET_NOINLINE_INIT();

#if PY_MAJOR_VERSION >= 3
	m = PyModule_Create(&greenlet_module_def);
#else
	m = Py_InitModule("greenlet", GreenMethods);
#endif
	if (m == NULL)
	{
		INITERROR;
	}

	if (PyModule_AddStringConstant(m, "__version__", GREENLET_VERSION) < 0)
	{
		INITERROR;
	}

#if PY_MAJOR_VERSION >= 3
	ts_curkey = PyUnicode_InternFromString("__greenlet_ts_curkey");
	ts_delkey = PyUnicode_InternFromString("__greenlet_ts_delkey");
#if GREENLET_USE_TRACING
	ts_tracekey = PyUnicode_InternFromString("__greenlet_ts_tracekey");
	ts_event_switch = PyUnicode_InternFromString("switch");
	ts_event_throw = PyUnicode_InternFromString("throw");
#endif
#else
	ts_curkey = PyString_InternFromString("__greenlet_ts_curkey");
	ts_delkey = PyString_InternFromString("__greenlet_ts_delkey");
#if GREENLET_USE_TRACING
	ts_tracekey = PyString_InternFromString("__greenlet_ts_tracekey");
	ts_event_switch = PyString_InternFromString("switch");
	ts_event_throw = PyString_InternFromString("throw");
#endif
#endif
	if (ts_curkey == NULL || ts_delkey == NULL)
	{
		INITERROR;
	}
	if (PyType_Ready(&PyGreenlet_Type) < 0)
	{
		INITERROR;
	}
	PyExc_GreenletError = PyErr_NewException("greenlet.error", NULL, NULL);
	if (PyExc_GreenletError == NULL)
	{
		INITERROR;
	}
#if PY_MAJOR_VERSION >= 3 || (PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 5)
	PyExc_GreenletExit = PyErr_NewException("greenlet.GreenletExit",
						PyExc_BaseException, NULL);
#else
	PyExc_GreenletExit = PyErr_NewException("greenlet.GreenletExit",
						NULL, NULL);
#endif
	if (PyExc_GreenletExit == NULL)
	{
		INITERROR;
	}

	ts_empty_tuple = PyTuple_New(0);
	if (ts_empty_tuple == NULL)
	{
		INITERROR;
	}

	ts_empty_dict = PyDict_New();
	if (ts_empty_dict == NULL)
	{
		INITERROR;
	}

	ts_current = green_create_main();
	if (ts_current == NULL)
	{
		INITERROR;
	}

	Py_INCREF(&PyGreenlet_Type);
	PyModule_AddObject(m, "greenlet", (PyObject*) &PyGreenlet_Type);
	Py_INCREF(PyExc_GreenletError);
	PyModule_AddObject(m, "error", PyExc_GreenletError);
	Py_INCREF(PyExc_GreenletExit);
	PyModule_AddObject(m, "GreenletExit", PyExc_GreenletExit);
	PyModule_AddObject(m, "GREENLET_USE_GC", PyBool_FromLong(GREENLET_USE_GC));
	PyModule_AddObject(m, "GREENLET_USE_TRACING", PyBool_FromLong(GREENLET_USE_TRACING));

        /* also publish module-level data as attributes of the greentype. */
	for (p=copy_on_greentype; *p; p++) {
		PyObject* o = PyObject_GetAttrString(m, *p);
		if (!o) continue;
		PyDict_SetItemString(PyGreenlet_Type.tp_dict, *p, o);
		Py_DECREF(o);
	}

	/*
	 * Expose C API
	 */

	/* types */
	_PyGreenlet_API[PyGreenlet_Type_NUM] = (void *) &PyGreenlet_Type;

	/* exceptions */
	_PyGreenlet_API[PyExc_GreenletError_NUM] = (void *) PyExc_GreenletError;
	_PyGreenlet_API[PyExc_GreenletExit_NUM] = (void *) PyExc_GreenletExit;

	/* methods */
	_PyGreenlet_API[PyGreenlet_New_NUM] = (void *) PyGreenlet_New;
	_PyGreenlet_API[PyGreenlet_GetCurrent_NUM] =
		(void *) PyGreenlet_GetCurrent;
	_PyGreenlet_API[PyGreenlet_Throw_NUM] = (void *) PyGreenlet_Throw;
	_PyGreenlet_API[PyGreenlet_Switch_NUM] = (void *) PyGreenlet_Switch;
	_PyGreenlet_API[PyGreenlet_SetParent_NUM] =
		(void *) PyGreenlet_SetParent;

#ifdef GREENLET_USE_PYCAPSULE
	c_api_object = PyCapsule_New((void *) _PyGreenlet_API, "greenlet._C_API", NULL);
#else
	c_api_object = PyCObject_FromVoidPtr((void *) _PyGreenlet_API, NULL);
#endif
	if (c_api_object != NULL)
	{
		PyModule_AddObject(m, "_C_API", c_api_object);
	}

#if PY_MAJOR_VERSION >= 3
	return m;
#endif
}
