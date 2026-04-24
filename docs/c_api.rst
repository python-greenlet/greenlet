=================
 C API Reference
=================

Greenlets can be created and manipulated from extension modules written in C or
C++, or from applications that embed Python. The ``greenlet.h`` header is
provided, and exposes the entire API available to pure Python modules.

Note that much of the API is implemented in terms of macros, meaning
that it is not necessarily ABI stable.

Types
=====

.. c:type:: PyGreenlet

   The C name corresponding to the Python :class:`greenlet.greenlet`.

Exceptions
==========

.. c:type:: PyExc_GreenletError

   The C name corresponding to the Python :exc:`greenlet.error`

.. c:type:: PyExc_GreenletExit

   The C name corresponding to the Python :exc:`greenlet.GreenletExit`

Functions
=========

.. important::

   Because the order in which extension modules are destroyed when the
   Python interpreter is finalized is undefined, it is undefined
   behaviour to call these APIs when ``Py_IsFinalizing`` returns true,
   unless otherwise documented. This is because the internal state of
   the greenlet module may have been torn down already.

.. c:function:: void PyGreenlet_Import(void)

   A macro that imports the greenlet module and initializes the C API. This
   must be called once for each extension module that uses the greenlet C API,
   usually in the module's init function.

.. c:function:: int PyGreenlet_Check(PyObject* p)

   Macro that returns true if the argument is a :c:type:`PyGreenlet`.

.. c:function:: int PyGreenlet_STARTED(PyGreenlet* g)

   Macro that returns true if the greenlet *g* has started.

.. c:function:: int PyGreenlet_ACTIVE(PyGreenlet* g)

    Macro that returns true if the greenlet *g* has started and has not died.

.. c:function:: PyGreenlet* PyGreenlet_GetParent(PyGreenlet* g)

    Macro that returns the parent greenlet of *g*. Returns a non-null
    pointer if there is a parent, or a null pointer on an error or if
    there is no parent. If this returns a non-null pointer, you must
    decrement its reference.

.. c:function:: int PyGreenlet_SetParent(PyGreenlet* g, PyGreenlet* nparent)

    Set the parent greenlet of *g*.

    :return: 0 for success, or -1 on error. When an error is returned,
             *g* is not a pointer to a greenlet, and an
             :exc:`AttributeError` has been raised.

.. c:function:: PyGreenlet* PyGreenlet_GetCurrent(void)

    Returns the currently active greenlet object.

    If called during interpreter finalization, returns ``NULL``
    and raises a :exc:`RuntimeError`.

    .. versionchanged:: 3.4.0
       Began returning ``NULL`` during interpreter shutdown.
       This implementation returned ``NULL`` too early, while the
       interpreter state was still guaranteed to be valid (during
       ``atexit`` handlers). This has been corrected in 3.5.
    .. versionchanged:: 3.5.0
       Now sets an exception before returning ``NULL``. This prevents
       a :exc:`SystemError` from being generated if this API was
       exposed directly to Python, and prevents a crash if this API
       was being called by Cython-generated code.


.. c:function:: PyGreenlet* PyGreenlet_New(PyObject* run, PyObject* parent)

    Creates a new greenlet object with the callable *run* and parent
    *parent*. Both parameters are optional and may be ``NULL``.

    :param run: If ``NULL``, the greenlet will be created, but will
                fail when switched to.
    :param parent: If ``NULL``, the parent is automatically set to the
                   current greenlet.

.. c:function:: PyObject* PyGreenlet_Switch(PyGreenlet* g, PyObject* args, PyObject* kwargs)

    Switches to the greenlet *g*. Besides *g*, the remaining
    parameters are optional and may be ``NULL``.

    Returns a new reference.

    :param args: If ``args`` is NULL, an empty tuple is passed to the
                 target greenlet. If given, must be a :class:`tuple`.

    :param kwargs: If kwargs is ``NULL``, no keyword arguments are
                   passed to the target greenlet. If given, must be a
                   :class:`dict`.

.. c:function:: PyObject* PyGreenlet_Throw(PyGreenlet* g, PyObject* typ, PyObject* val, PyObject* tb)

    Switches to greenlet *g*, but immediately raise an exception of type
    *typ* with the value *val*, and optionally, the traceback object
    *tb*. *tb* can be ``NULL``.

    The arguments *typ*, *val* and *tb* are interpreted as for :c:func:`PyErr_Restore`.
