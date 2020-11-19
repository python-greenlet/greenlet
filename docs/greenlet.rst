.. _what_is_greenlet:

===================
 greenlet Concepts
===================

.. currentmodule:: greenlet

.. |--| unicode:: U+2013   .. en dash
.. |---| unicode:: U+2014  .. em dash, trimming surrounding whitespace
   :trim:


A "greenlet" is a small independent pseudo-thread. Think about it as a
small stack of frames; the outermost (bottom) frame is the initial
function you called, and the innermost frame is the one in which the
greenlet is currently paused.

In code, greenlets are represented by objects of class
:class:`greenlet`. These objects have a few defined attributes, and
also have a ``__dict__``, allowing for arbitrary user-defined
attributes.

.. warning::

   Attribute names beginning with ``gr_`` are reserved for this
   library.

Switching greenlets
===================

.. seealso:: :doc:`switching`

You work with greenlets by creating a number of such stacks and
jumping execution between them. Jumps are never implicit: a greenlet
must choose to jump to another greenlet, which will cause the former
to suspend and the latter to resume where it was suspended. Jumping
between greenlets is called "switching". Similarly to
``generator.send(val)``, switching may pass objects between greenlets.

The greenlet Lifecycle
======================

.. seealso::

   Details And Examples
      :doc:`creating_executing_greenlets`
   Where does execution go when a greenlet dies?
      :ref:`greenlet_parents`

When you create a greenlet, it gets an initially empty stack; when you
first switch to it, it starts to run a specified function, which may call
other functions, switch out of the greenlet, etc. When eventually the
outermost function finishes its execution, the greenlet's stack becomes
empty again and the greenlet is "dead". Greenlets can also die of an
uncaught exception, or be :doc:`garbage collected <greenlet_gc>`
(which raises an exception).

.. rubric:: Example

Let's quickly pull together an example demonstrating those concepts
before continuing with a few more concepts.

.. doctest::

    >>> from greenlet import greenlet

    >>> def test1():
    ...     print("[gr1] main  -> test1")
    ...     gr2.switch()
    ...     print("[gr1] test1 <- test2")
    ...     return 'test1 done'

    >>> def test2():
    ...     print("[gr2] test1 -> test2")
    ...     gr1.switch()
    ...     print("This is never printed.")

    >>> gr1 = greenlet(test1)
    >>> gr2 = greenlet(test2)
    >>> gr1.switch()
    [gr1] main  -> test1
    [gr2] test1 -> test2
    [gr1] test1 <- test2
    'test1 done'
    >>> gr1.dead
    True
    >>> gr2.dead
    False


The line ``gr1.switch()`` jumps to ``test1``, which prints that, jumps
to ``test2``, and prints that, jumps back into ``test1``, prints that;
and then ``test1`` finishes and ``gr1`` dies. At this point, the
execution comes back to the original ``gr1.switch()`` call, which
returns the value that ``test1`` returned. Note that ``test2`` is
never switched back to and so doesn't print its final line; it is also
not dead.

Having seen that, we can continue with a few more concepts.

The Current greenlet
====================

The greenlet that is actively running code is called the "current
greenlet." The :class:`greenlet` object representing the current
greenlet can be obtained by calling :func:`getcurrent`. (Note that
:ref:`this could be a subclass <subclassing_greenlet>`.)

As long as a greenlet is running, no other greenlet can be running.
Execution must be explicitly transferred by switching to a different
greenlet.

The Main greenlet
=================

Initially, there is one greenlet that you don't have to create: the
main greenlet. This is the only greenlet that can ever have :ref:`a
parent of None <greenlet_parents>`. The main greenlet can never be
dead. This is true for :doc:`every thread in a process
<python_threads>`.

.. rubric:: Example

.. doctest::

   >>> from greenlet import getcurrent
   >>> def am_i_main():
   ...     current = getcurrent()
   ...     return current.parent is None
   >>> am_i_main()
   True
   >>> glet = greenlet(am_i_main)
   >>> glet.switch()
   False

.. _greenlet_parents:

Greenlet Parents
================

Every greenlet, except the main greenlet, has a "parent" greenlet. The
parent greenlet defaults to being the one in which the greenlet was
created (this can be :ref:`changed at any time
<changing_the_parent>`). In this way, greenlets are organized in a
tree. Top-level code that doesn't run in a user-created greenlet runs
in the implicit main greenlet, which is the root of the tree.

The parent is where execution continues when a greenlet dies, whether
by explicitly returning from its function, "falling off the end" of
its function, or by raising an uncaught exception.

In the above example, both ``gr1`` and ``gr2`` have the main greenlet
as a parent. Whenever one of them dies, the execution comes back to
"main".

Uncaught Exceptions are Raised In the Parent
--------------------------------------------

Uncaught exceptions are propagated into the parent, too. For example, if
the above ``test2()`` contained a typo, it would generate a :exc:`NameError` that
would kill ``gr2``, and the exception would go back directly into "main".
The traceback would show ``test2``, but not ``test1``. Remember, switches are not
calls, but transfer of execution between parallel "stack containers", and
the "parent" defines which stack logically comes "below" the current one.

.. doctest::

    >>> def test2():
    ...    print(this_should_be_a_name_error)
    >>> gr1 = greenlet(test1)
    >>> gr2 = greenlet(test2)
    >>> gr1.switch()
    Traceback (most recent call last):
      ...
      File "<doctest default[3]>", line 1, in <module>
        gr1.switch()
      File "<doctest default[0]>", line 2, in test2
        print(this_should_be_a_name_error)
    NameError: name 'this_should_be_a_name_error' is not defined
