=============================
 Context Variables (asyncio)
=============================

.. versionadded:: 1.0.0

On Python versions (3.7 and above) that natively support context
variables as defined in :pep:`567`, each greenlet runs by default in
its own :class:`contextvars.Context`, enabling
:class:`~contextvars.ContextVar`\s to be used for "greenlet-local
storage". (If you need to support earlier Python versions, you can use
attributes on the greenlet object instead.)

A new greenlet's context is initially empty, i.e., all
:class:`~contextvars.ContextVar`\s have their default values. This
matches the behavior of a new thread, but differs from that of a new
:class:`asyncio.Task`, which inherits a copy of the context that was
active when it was spawned. You can assign to a greenlet's
``gr_context`` attribute to change the context that it will use. For
example:

.. doctest::
    :pyversion: > 3.7

    >>> import greenlet
    >>> import contextvars

    >>> example = contextvars.ContextVar("example", default=0)

    >>> def set_it(next_value):
    ...    previous_value = example.get()
    ...    print("Value of example in greenlet  :", previous_value)
    ...    print("Setting example in greenlet to:", next_value)
    ...    example.set(next_value)

    >>> _ = example.set(1)

By default, a new greenlet gets an empty context, unrelated to the
current context:

.. doctest::
    :pyversion: > 3.7

    >>> gr1 = greenlet.greenlet(set_it)
    >>> gr1.switch(2)
    Value of example in greenlet  : 0
    Setting example in greenlet to: 2
    >>> example.get()
    1

You can make a greenlet get a copy of the current context when it is
created, like asyncio:

.. doctest::
    :pyversion: > 3.7

    >>> gr2 = greenlet.greenlet(set_it)
    >>> gr2.gr_context = contextvars.copy_context()
    >>> gr2.switch(2)
    Value of example in greenlet  : 1
    Setting example in greenlet to: 2

You can also make a greenlet *share* the current context, like older,
non-contextvars-aware versions of greenlet:

.. doctest::
    :pyversion: > 3.7

    >>> gr3 = greenlet.greenlet(set_it)
    >>> gr3.gr_context = greenlet.getcurrent().gr_context
    >>> gr3.switch(2)
    Value of example in greenlet  : 1
    Setting example in greenlet to: 2

You can alternatively set a new greenlet's context by surrounding its
top-level function in a call to :meth:`Context.run()
<contextvars.Context.run>`:

.. doctest::
    :pyversion: > 3.7

    >>> _ = example.set(1)
    >>> gr4 = greenlet.greenlet(contextvars.copy_context().run)
    >>> gr4.switch(set_it, 2)
    Value of example in greenlet  : 1
    Setting example in greenlet to: 2
    >>> example.get()
    1

However, contextvars were not designed with greenlets in mind, so
using :meth:`Context.run() <contextvars.Context.run>` becomes
challenging in an environment with arbitrary greenlet-to-greenlet
control transfers. The :meth:`~contextvars.Context.run` calls across
all greenlets in a thread must effectively form a stack, where the
last context entered is the first one to be exited. Also, it's
not possible to have two calls to :meth:`~contextvars.Context.run` for
the same context active in two different greenlets at the same
time. Assigning to ``gr_context`` does not share these
restrictions.

You can access and change a greenlet's context almost no matter what
state the greenlet is in. It can be dead, not yet started, or
suspended (on any thread), or running (on the current thread only).
Accessing or modifying ``gr_context`` of a greenlet running on a
different thread raises :exc:`ValueError`.

.. warning::

   Changing the ``gr_context`` after a greenlet has begun
   running is not recommended for reasons outlined below.

Once a greenlet has started running, ``gr_context`` tracks its
*current* context: the one that would be active if you switched to the
greenlet right now. This may not be the same as the value of
``gr_context`` before the greenlet started running. One potential
difference occurs if a greenlet running in the default empty context
(represented as ``None``) sets any context variables: a new
:class:`~contextvars.Context` will be implicitly created to hold them,
which will be reflected in ``gr_context``. Another one occurs if a
greenlet makes a call to ``Context.run(some_inner, func)``: its
``gr_context`` will be ``some_inner`` until ``func()`` returns.

.. warning::

   Assigning to ``gr_context`` of an active greenlet that might be
   inside a call to :meth:`Context.run() <contextvars.Context.run>` is
   not recommended, because :meth:`~contextvars.Context.run` will
   raise an exception if the current context when it exits doesn't
   match the context that it set upon entry. The safest thing to do is
   set ``gr_context`` once, before starting the greenlet; then there's
   no potential conflict with :meth:`Context.run()
   <contextvars.Context.run>` calls.
