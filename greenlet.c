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

/* Python 2.3 support */
#ifndef Py_VISIT
#define Py_VISIT(o) \
	if (o) { \
		int err; \
		if ((err = visit((PyObject *)(o), arg))) { \
			return err; \
		} \
	}
#endif /* !Py_VISIT */

#ifndef Py_CLEAR
#define Py_CLEAR(op) \
	do { \
		if (op) { \
			PyObject *tmp = (PyObject *)(op); \
			(op) = NULL; \
			Py_DECREF(tmp); \
		} \
	} while (0)
#endif /* !Py_CLEAR */

/* Python <= 2.5 support */
#if PY_MAJOR_VERSION < 3
#ifndef Py_REFCNT
#  define Py_REFCNT(ob) (((PyObject *) (ob))->ob_refcnt)
#endif
#ifndef Py_TYPE
#  define Py_TYPE(ob)   (((PyObject *) (ob))->ob_type)
#endif
#endif

#if PY_VERSION_HEX < 0x02050000
typedef int Py_ssize_t;
#endif

extern PyTypeObject PyGreenlet_Type;

/* The current greenlet in this thread state (holds a reference) */
static PyGreenlet* volatile ts_current = NULL;
/* Holds a reference to the switching-to stack during the slp switch */
static PyGreenlet* volatile ts_target = NULL;
/* NULL if error, otherwise args tuple to pass around during slp switch */
static PyObject* volatile ts_passaround_args = NULL;
static PyObject* volatile ts_passaround_kwargs = NULL;

/*
 * Need to be careful when clearing passaround args and kwargs
 * If deallocating args or kwargs ever causes stack switch (which
 * currently shouldn't happen though), then args or kwargs might
 * be borrowed or no longer valid, so better be paranoid
 */
#define g_passaround_clear() do { \
	PyObject* args = ts_passaround_args; \
	PyObject* kwargs = ts_passaround_kwargs; \
	ts_passaround_args = NULL; \
	ts_passaround_kwargs = NULL; \
	Py_XDECREF(args); \
	Py_XDECREF(kwargs); \
} while(0)

#define g_passaround_return_args() do { \
	PyObject* args = ts_passaround_args; \
	PyObject* kwargs = ts_passaround_kwargs; \
	ts_passaround_args = NULL; \
	ts_passaround_kwargs = NULL; \
	Py_XDECREF(kwargs); \
	return args; \
} while(0)

#define g_passaround_return_kwargs() do { \
	PyObject* args = ts_passaround_args; \
	PyObject* kwargs = ts_passaround_kwargs; \
	ts_passaround_args = NULL; \
	ts_passaround_kwargs = NULL; \
	Py_XDECREF(args); \
	return kwargs; \
} while(0)

/***********************************************************/
/* Thread-aware routines, switching global variables when needed */

#define STATE_OK    (ts_current->run_info == PyThreadState_GET()->dict \
			|| !green_updatecurrent())

static PyObject* ts_curkey;
static PyObject* ts_delkey;
static PyObject* PyExc_GreenletError;
static PyObject* PyExc_GreenletExit;

#define GREENLET_USE_GC

#ifdef GREENLET_USE_GC
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
	PyThreadState* tstate;
	PyGreenlet* next;
	PyGreenlet* previous;
	PyObject* deleteme;

	/* save ts_current as the current greenlet of its own thread */
	previous = ts_current;
	if (PyDict_SetItem(previous->run_info, ts_curkey, (PyObject*) previous))
		return -1;

	/* get ts_current from the active tstate */
	tstate = PyThreadState_GET();
	if (tstate->dict && (next =
	    (PyGreenlet*) PyDict_GetItem(tstate->dict, ts_curkey))) {
		/* found -- remove it, to avoid keeping a ref */
		Py_INCREF(next);
		if (PyDict_DelItem(tstate->dict, ts_curkey))
			PyErr_Clear();
	}
	else {
		/* first time we see this tstate */
		next = green_create_main();
		if (next == NULL)
			return -1;
	}
	ts_current = next;
	Py_DECREF(previous);
	/* green_dealloc() cannot delete greenlets from other threads, so
	   it stores them in the thread dict; delete them now. */
	deleteme = PyDict_GetItem(tstate->dict, ts_delkey);
	if (deleteme != NULL) {
		PyList_SetSlice(deleteme, 0, INT_MAX, NULL);
	}
	return 0;
}

static PyObject* green_statedict(PyGreenlet* g)
{
	while (!PyGreenlet_STARTED(g))
		g = g->parent;
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
static void GREENLET_NOINLINE(g_initialstub)(void*);
#define GREENLET_NOINLINE_INIT() do {} while(0)
#else
/* force compiler to call functions via pointers */
static void (*slp_restore_state)(void);
static int (*slp_save_state)(char*);
static int (*slp_switch)(void);
static void (*g_initialstub)(void*);
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
	/* perform a stack switch according to some global variables
	   that must be set before:
	   - ts_current: current greenlet (holds a reference)
	   - ts_target: greenlet to switch to
	   - ts_passaround_args: NULL if PyErr_Occurred(),
	             else a tuple of args sent to ts_target (holds a reference)
	   - ts_passaround_kwargs: same as ts_passaround_args
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
		g_passaround_clear();
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

		ts_current = target;
		Py_INCREF(target);
		Py_DECREF(origin);
	}
	return err;
}

static PyObject *
g_switch(PyGreenlet* target, PyObject* args, PyObject* kwargs)
{
	/* _consumes_ a reference to the args tuple and kwargs dict,
	   and return a new tuple reference */

	/* check ts_current */
	if (!STATE_OK) {
		Py_DECREF(args);
		Py_XDECREF(kwargs);
		return NULL;
	}
	if (green_statedict(target) != ts_current->run_info) {
		PyErr_SetString(PyExc_GreenletError,
				"cannot switch to a different thread");
		Py_DECREF(args);
		Py_XDECREF(kwargs);
		return NULL;
	}

	ts_passaround_args = args;
	ts_passaround_kwargs = kwargs;

	/* find the real target by ignoring dead greenlets,
	   and if necessary starting a greenlet. */
	while (1) {
		if (PyGreenlet_ACTIVE(target)) {
			ts_target = target;
			g_switchstack();
			break;
		}
		if (!PyGreenlet_STARTED(target)) {
			void* dummymarker;
			ts_target = target;
			g_initialstub(&dummymarker);
			break;
		}
		target = target->parent;
	}

	/* We need to figure out what values to pass to the target greenlet
	   based on the arguments that have been passed to greenlet.switch(). If
	   switch() was just passed an arg tuple, then we'll just return that.
	   If only keyword arguments were passed, then we'll pass the keyword
	   argument dict. Otherwise, we'll create a tuple of (args, kwargs) and
	   return both. */
	if (ts_passaround_kwargs == NULL)
	{
		g_passaround_return_args();
	}
	else if (PyDict_Size(ts_passaround_kwargs) == 0)
	{
		g_passaround_return_args();
	}
	else if (PySequence_Length(ts_passaround_args) == 0)
	{
		g_passaround_return_kwargs();
	}
	else
	{
		PyObject *tuple = PyTuple_New(2);
		PyTuple_SetItem(tuple, 0, ts_passaround_args);
		PyTuple_SetItem(tuple, 1, ts_passaround_kwargs);
		ts_passaround_args = NULL;
		ts_passaround_kwargs = NULL;
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

static void GREENLET_NOINLINE(g_initialstub)(void* mark)
{
	int err;
	PyObject* o;

	/* ts_target.run is the object to call in the new greenlet */
	PyObject* run = PyObject_GetAttrString((PyObject*) ts_target, "run");
	if (run == NULL) {
		g_passaround_clear();
		return;
	}
	/* now use run_info to store the statedict */
	o = ts_target->run_info;
	ts_target->run_info = green_statedict(ts_target->parent);
	Py_INCREF(ts_target->run_info);
	Py_XDECREF(o);

	/* start the greenlet */
	ts_target->stack_start = NULL;
	ts_target->stack_stop = (char*) mark;
	if (ts_current->stack_start == NULL) {
		/* ts_current is dying */
		ts_target->stack_prev = ts_current->stack_prev;
	}
	else {
		ts_target->stack_prev = ts_current;
	}
	ts_target->top_frame = NULL;
	ts_target->exc_type = NULL;
	ts_target->exc_value = NULL;
	ts_target->exc_traceback = NULL;
	ts_target->recursion_depth = PyThreadState_GET()->recursion_depth;
	err = g_switchstack();
	/* returns twice!
	   The 1st time with err=1: we are in the new greenlet
	   The 2nd time with err=0: back in the caller's greenlet
	*/
	if (err == 1) {
		/* in the new greenlet */
		PyObject* args;
		PyObject* kwargs;
		PyObject* result;
		PyGreenlet* ts_self = ts_current;
		ts_self->stack_start = (char*) 1;  /* running */

		args = ts_passaround_args;
		kwargs = ts_passaround_kwargs;
		if (args == NULL)    /* pending exception */
			result = NULL;
		else {
			/* call g.run(*args, **kwargs) */
			result = PyEval_CallObjectWithKeywords(
				run, args, kwargs);
			Py_DECREF(args);
			Py_XDECREF(kwargs);
		}
		Py_DECREF(run);
		result = g_handle_exit(result);

		/* jump back to parent */
		ts_self->stack_start = NULL;  /* dead */
		g_switch(ts_self->parent, result, NULL);
		/* must not return from here! */
		PyErr_WriteUnraisable((PyObject *) ts_self);
		Py_FatalError("greenlets cannot continue");
	}
	/* back in the parent */
}


/***********************************************************/


static PyObject* green_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PyObject* o;
	if (!STATE_OK)
		return NULL;
	
	o = type->tp_alloc(type, 0);
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
		if (!STATE_OK) {
			return -1;
		}
		oldparent = self->parent;
		self->parent = ts_current;
		Py_INCREF(ts_current);
		Py_XDECREF(oldparent);
		/* Send the greenlet a GreenletExit exception. */
		PyErr_SetNone(PyExc_GreenletExit);
		result = g_switch(self, NULL, NULL);
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

#ifdef GREENLET_USE_GC
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
	return 0;
}

static int green_is_gc(PyGreenlet* self)
{
	int rval;
	/* Main and alive greenlets are not garbage collectable */
	rval = (self->stack_stop == (char *)-1 || self->stack_start != NULL) ? 0 : 1;
	return rval;
}

static int green_clear(PyGreenlet* self)
{
	return 0; /* greenlet is not alive, so there's nothing to clear */
}
#endif

static void green_dealloc(PyGreenlet* self)
{
	PyObject *error_type, *error_value, *error_traceback;

#ifdef GREENLET_USE_GC
	PyObject_GC_UnTrack((PyObject *)self);
	Py_TRASHCAN_SAFE_BEGIN(self);
#endif /* GREENLET_USE_GC */
	if (PyGreenlet_ACTIVE(self)) {
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
		/* Restore the saved exception. */
		PyErr_Restore(error_type, error_value, error_traceback);
		/* Undo the temporary resurrection; can't use DECREF here,
		 * it would cause a recursive call.
		 */
		assert(Py_REFCNT(self) > 0);
		--Py_REFCNT(self);
		if (Py_REFCNT(self) == 0 && PyGreenlet_ACTIVE(self)) {
			/* Not resurrected, but still not dead!
			   XXX what else should we do? we complain. */
			PyObject* f = PySys_GetObject("stderr");
			if (f != NULL) {
				PyFile_WriteString("GreenletExit did not kill ",
						   f);
				PyFile_WriteObject((PyObject*) self, f, 0);
				PyFile_WriteString("\n", f);
			}
			Py_INCREF(self);   /* leak! */
		}
		if (Py_REFCNT(self) != 0) {
			/* Resurrected! */
			Py_ssize_t refcnt = Py_REFCNT(self);
			_Py_NewReference((PyObject*) self);
#ifdef GREENLET_USE_GC
			PyObject_GC_Track((PyObject *)self);
#endif
			Py_REFCNT(self) = refcnt;
#ifdef COUNT_ALLOCS
			--Py_TYPE(self)->tp_frees;
			--Py_TYPE(self)->tp_allocs;
#endif /* COUNT_ALLOCS */
			goto green_dealloc_end;
		}
	}
	Py_CLEAR(self->parent);
	Py_CLEAR(self->exc_type);
	Py_CLEAR(self->exc_value);
	Py_CLEAR(self->exc_traceback);
	if (self->weakreflist != NULL)
		PyObject_ClearWeakRefs((PyObject *) self);
	Py_CLEAR(self->run_info);
	Py_TYPE(self)->tp_free((PyObject*) self);
green_dealloc_end:
#ifdef GREENLET_USE_GC
	Py_TRASHCAN_SAFE_END(self);
#endif /* GREENLET_USE_GC */
	return;
}

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
	if (!STATE_OK)
		return NULL;
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

	if (!STATE_OK)
		goto failed_throw;
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

static PyObject* green_getdead(PyGreenlet* self, void* c)
{
	PyObject* res;
	if (PyGreenlet_ACTIVE(self) || !PyGreenlet_STARTED(self))
		res = Py_False;
	else
		res = Py_True;
	Py_INCREF(res);
	return res;
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
}

static int
PyGreenlet_SetParent(PyGreenlet *g, PyGreenlet *nparent)
{
	if (!STATE_OK) {
		return -1;
	}

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
		parent = PyGreenlet_GetCurrent();
	}
	PyGreenlet_SetParent(g, parent);
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
    {NULL,     NULL}		/* sentinel */
};

static PyGetSetDef green_getsets[] = {
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
#if PY_MAJOR_VERSION >= 3
	PyVarObject_HEAD_INIT(NULL, 0)
#else
	PyObject_HEAD_INIT(NULL)
	0,					/* ob_size */
#endif
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
	0,					/* tp_dictoffset */
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

static PyMethodDef GreenMethods[] = {
	{"getcurrent", (PyCFunction)mod_getcurrent, METH_NOARGS, /*XXX*/ NULL},
	{NULL,     NULL}        /* Sentinel */
};

static char* copy_on_greentype[] = {
	"getcurrent", "error", "GreenletExit", NULL
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
#else
	ts_curkey = PyString_InternFromString("__greenlet_ts_curkey");
	ts_delkey = PyString_InternFromString("__greenlet_ts_delkey");
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
#ifdef GREENLET_USE_GC
	PyModule_AddObject(m, "GREENLET_USE_GC", PyBool_FromLong(1));
#else
	PyModule_AddObject(m, "GREENLET_USE_GC", PyBool_FromLong(0));
#endif

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
