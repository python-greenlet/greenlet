.. _switching:

==========================================================
 Switching Between Greenlets: Passing Objects and Control
==========================================================

.. This is an "explanation" document.

.. currentmodule:: greenlet

Switches between greenlets occur when:

-  The method `greenlet.switch` of a greenlet is
   called, in which case execution jumps to the greenlet whose ``switch()`` is
   called; or
-  When the method `greenlet.throw` is used to raise an exception in
   the target greenlet, in which case execution jumps to the greenlet
   whose ``throw`` was called; or
-  When a greenlet dies, in which case execution jumps to the
   parent greenlet.

During a switch, an object or an exception is "sent" to the target
greenlet; this can be used as a convenient way to pass information
between greenlets. For example:

.. doctest::

    >>> from greenlet import greenlet
    >>> def test1(x, y):
    ...     z = gr2.switch(x + y)
    ...     print(z)

    >>> def test2(u):
    ...     print(u)
    ...     gr1.switch(42)

    >>> gr1 = greenlet(test1)
    >>> gr2 = greenlet(test2)
    >>> gr1.switch("hello", " world")
    hello world
    42

This prints "hello world" and 42. Note that the arguments of
``test1()`` and ``test2()`` are not provided when the greenlet is
created, but only the first time someone switches to it.

Here are the precise rules for sending objects around:

``g.switch(*args, **kwargs)``
    Switches execution to the greenlet ``g``, sending it the given
    arguments. As a special case, if ``g`` did not start yet, then it
    will start to run now; ``args`` and ``kwargs`` are passed to the
    greenlet's ``run()`` function as its arguments.

Dying greenlet
    If a greenlet's ``run()`` finishes, its return value is the object
    sent to its parent. If ``run()`` terminates with an exception, the
    exception is propagated to its parent (unless it is a
    ``greenlet.GreenletExit`` exception, in which case the exception
    object is caught and *returned* to the parent).

Apart from the cases described above, the target greenlet normally
receives the object as the return value of the call to ``switch()`` in
which it was previously suspended. Indeed, although a call to
``switch()`` does not return immediately, it will still return at some
point in the future, when some other greenlet switches back. When this
occurs, then execution resumes just after the ``switch()`` where it was
suspended, and the ``switch()`` itself appears to return the object that
was just sent. This means that ``x = g.switch(y)`` will send the object
``y`` to ``g``, and will later put the (unrelated) object that some
(unrelated) greenlet passes back to us into ``x``.

Multiple And Keyword Arguments
==============================

You can pass multiple or keyword arguments to ``switch()``. If the
greenlet hasn't begun running, those are passed as function arguments
to ``run`` as usual in Python. If the greenlet *was* running, multiple
arguments will be a :class:`tuple`, and keyword arguments will be a
:class:`dict`; any number of positional arguments with keyword
arguments will have the entire set in a tuple, with positional
arguments in their own nested tuple, and keyword arguments as a `dict`
in the the last element of the tuple:

.. doctest::

    >>> def test1(x, y, **kwargs):
    ...     while 1:
    ...         z = gr2.switch(x + y + ' ' + str(kwargs))
    ...         if not z: break
    ...         print(z)

    >>> def test2(u):
    ...     print(u)
    ...     # A single argument -> itself
    ...     gr1.switch(42)
    ...     # Multiple positional args -> a tuple
    ...     gr1.switch("how", "are", "you")
    ...     # Only keyword arguments -> a dict
    ...     gr1.switch(language='en')
    ...     # one positional and keywords -> ((tuple,), dict)
    ...     gr1.switch("howdy", language='en_US')
    ...     # multiple positional and keywords -> ((tuple,), dict)
    ...     gr1.switch("all", "y'all", language='en_US_OK')
    ...     gr1.switch(None) # terminate

    >>> gr1 = greenlet(test1)
    >>> gr2 = greenlet(test2)
    >>> gr1.switch("hello", " world", language='en')
    hello world {'language': 'en'}
    42
    ('how', 'are', 'you')
    {'language': 'en'}
    (('howdy',), {'language': 'en_US'})
    (('all', "y'all"), {'language': 'en_US_OK'})

.. _switch_to_dead:

Switching To Dead Greenlets
===========================

Note that any attempt to switch to a dead greenlet actually goes to the
dead greenlet's parent, or its parent's parent, and so on. (The final
parent is the "main" greenlet, which is never dead.)


.. doctest::

   >>> def inner():
   ...     print("Entering inner.")
   ...     print("Returning from inner.")
   ...     return 42
   >>> def outer():
   ...     print("Entering outer and spawning inner.")
   ...     inner_glet = greenlet(inner)
   ...     print("Switching to inner.")
   ...     result = inner_glet.switch()
   ...     print("Got from inner value: %s" % (result,))
   ...     print("Switching to inner again.")
   ...     result = inner_glet.switch()
   ...     print("Got from inner value: %s" % (result,))
   ...     return inner_glet
   >>> outer_glet = greenlet(outer)

Here, our main greenlet has created another greenlet (``outer``), which in turn
creates a greenlet (``inner``). The outer greenlet switches to the
inner greenlet, which immediately finishes and dies; the outer greenlet
attempts to switch back to the inner greenlet, but since the inner
greenlet is dead, it just switches...to itself (since it was the
parent). Note how the second switch (to the dead greenlet) returns an
empty tuple.

.. doctest::

    >>> inner_glet = outer_glet.switch()
    Entering outer and spawning inner.
    Switching to inner.
    Entering inner.
    Returning from inner.
    Got from inner value: 42
    Switching to inner again.
    Got from inner value: ()

We can similarly ask the main greenlet to switch to the (dead) inner
greenlet and its (dead) parent and wind up still in the main greenlet.

   >>> inner_glet.switch()
   ()
