=================
 Using greenlets
=================

.. currentmodule:: greenlet

.. |--| unicode:: U+2013   .. en dash
.. |---| unicode:: U+2014  .. em dash, trimming surrounding whitespace
   :trim:


Introduction
============

A "greenlet" is a small independent pseudo-thread. Think about it as a
small stack of frames; the outermost (bottom) frame is the initial
function you called, and the innermost frame is the one in which the
greenlet is currently paused. You work with greenlets by creating a
number of such stacks and jumping execution between them. Jumps are never
implicit: a greenlet must choose to jump to another greenlet, which will
cause the former to suspend and the latter to resume where it was
suspended. Jumping between greenlets is called "switching".

When you create a greenlet, it gets an initially empty stack; when you
first switch to it, it starts to run a specified function, which may call
other functions, switch out of the greenlet, etc. When eventually the
outermost function finishes its execution, the greenlet's stack becomes
empty again and the greenlet is "dead". Greenlets can also die of an
uncaught exception.

For example:

.. doctest::

    >>> from greenlet import greenlet

    >>> def test1():
    ...     print(12)
    ...     gr2.switch()
    ...     print(34)
    ...     return 'test1 done'

    >>> def test2():
    ...     print(56)
    ...     gr1.switch()
    ...     print(78)

    >>> gr1 = greenlet(test1)
    >>> gr2 = greenlet(test2)
    >>> gr1.switch()
    12
    56
    34
    'test1 done'

The last line jumps to ``test1``, which prints 12, jumps to ``test2``, prints 56,
jumps back into ``test1``, prints 34; and then ``test1`` finishes and ``gr1`` dies.
At this point, the execution comes back to the original ``gr1.switch()``
call, which returns the value that ``test1`` returned. Note that 78 is never printed.

Parents
=======

Let's see where execution goes when a greenlet dies. Every greenlet has a
"parent" greenlet. The parent greenlet is initially the one in which the
greenlet was created (this can be changed at any time). The parent is
where execution continues when a greenlet dies. In this way, greenlets are
organized in a tree. Top-level code that doesn't run in a user-created
greenlet runs in the implicit "main" greenlet, which is the root of the
tree.

In the above example, both ``gr1`` and ``gr2`` have the main greenlet as a parent.
Whenever one of them dies, the execution comes back to "main".

Uncaught exceptions are propagated into the parent, too. For example, if
the above ``test2()`` contained a typo, it would generate a :exc:`NameError` that
would kill ``gr2``, and the exception would go back directly into "main".
The traceback would show ``test2``, but not test1. Remember, switches are not
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

Instantiation
=============

:class:`greenlet.greenlet` is the greenlet type, which supports the following
operations:

``greenlet(run=None, parent=None)``
    Create a new greenlet object (without running it). ``run`` is the
    callable to invoke, and ``parent`` is the parent greenlet, which
    defaults to the current greenlet.

:func:`greenlet.getcurrent`
    Returns the current greenlet (i.e. the one which called this
    function).

:exc:`greenlet.GreenletExit`
    This special exception does not propagate to the parent greenlet; it
    can be used to kill a single greenlet.

The ``greenlet`` type can be subclassed, too. A greenlet runs by calling
its ``run`` attribute, which is normally set when the greenlet is
created; but for subclasses it also makes sense to define a ``run`` method
instead of giving a ``run`` argument to the constructor.

.. _switching:

Switching
=========

Switches between greenlets occur when the method ``switch()`` of a greenlet is
called, in which case execution jumps to the greenlet whose ``switch()`` is
called, or when a greenlet dies, in which case execution jumps to the
parent greenlet. During a switch, an object or an exception is "sent" to
the target greenlet; this can be used as a convenient way to pass
information between greenlets. For example:

.. doctest::

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

This prints "hello world" and 42, with the same order of execution as the
previous example. Note that the arguments of ``test1()`` and ``test2()`` are not
provided when the greenlet is created, but only the first time someone
switches to it.

Here are the precise rules for sending objects around:

``g.switch(*args, **kwargs)``
    Switches execution to the greenlet ``g``, sending it the given
    arguments. As a special case, if ``g`` did not start yet, then it
    will start to run now.

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

You can pass multiple or keyword arguments to ``switch()``. if the
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
---------------------------

Note that any attempt to switch to a dead greenlet actually goes to the
dead greenlet's parent, or its parent's parent, and so on. (The final
parent is the "main" greenlet, which is never dead.)



Greenlets and Python threads
============================

Greenlets can be combined with Python threads; in this case, each thread
contains an independent "main" greenlet with a tree of sub-greenlets. It
is not possible to mix or switch between greenlets belonging to different
threads.

.. doctest::

   >>> from greenlet import getcurrent
   >>> from threading import Thread
   >>> main = getcurrent()
   >>> def run():
   ...      try:
   ...          main.switch()
   ...      except greenlet.error as e:
   ...          print(e)
   >>> t = Thread(target=run)
   >>> t.start()
   cannot switch to a different thread
   >>> t.join()

Garbage-collecting live greenlets
=================================

If all the references to a greenlet object go away (including the
references from the parent attribute of other greenlets), then there is no
way to ever switch back to this greenlet. In this case, a :exc:`GreenletExit`
exception is generated into the greenlet. This is the only case where a
greenlet receives the execution asynchronously. This gives
``try:finally:`` blocks a chance to clean up resources held by the
greenlet. This feature also enables a programming style in which
greenlets are infinite loops waiting for data and processing it. Such
loops are automatically interrupted when the last reference to the
greenlet goes away.

.. doctest::

   >>> from greenlet import getcurrent, greenlet, GreenletExit
   >>> def run():
   ...     print("Beginning greenlet")
   ...     try:
   ...         while 1:
   ...             print("Switching to parent")
   ...             getcurrent().parent.switch()
   ...     except GreenletExit:
   ...          print("Got GreenletExit; quitting")

   >>> glet = greenlet(run)
   >>> _ = glet.switch()
   Beginning greenlet
   Switching to parent
   >>> glet = None
   Got GreenletExit; quitting

The greenlet is expected to either die or be resurrected by having a
new reference to it stored somewhere; just catching and ignoring the
`GreenletExit` is likely to lead to an infinite loop.

Greenlets participate in garbage collection in a limited fashion;
cycles involving data that is present in a greenlet's frames may not
be detected. Storing references to other greenlets cyclically may lead
to leaks.

Here, we define a function that creates a cycle; when we run it and
then collect garbage, the cycle is found and cleared, even while the
function is running.

.. note:: These examples require Python 3 to run; Python 2 won't
          collect cycles if the ``__del__`` method is defined.

.. doctest::
   :pyversion: >= 3.5

   >>> import gc
   >>> class Cycle(object):
   ...    def __del__(self):
   ...         print("(Running finalizer)")

   >>> def collect_it():
   ...      print("Collecting garbage")
   ...      gc.collect()
   >>> def run(collect=collect_it):
   ...      cycle1 = Cycle()
   ...      cycle2 = Cycle()
   ...      cycle1.cycle = cycle2
   ...      cycle2.cycle = cycle1
   ...      print("Deleting cycle vars")
   ...      del cycle1
   ...      del cycle2
   ...      collect()
   ...      print("Returning")
   >>> run()
   Deleting cycle vars
   Collecting garbage
   (Running finalizer)
   (Running finalizer)
   Returning

If we use the same function in a greenlet, the cycle is also found
while the greenlet is active:

.. doctest::
   :pyversion: >= 3.5

   >>> glet = greenlet(run)
   >>> _ = glet.switch()
   Deleting cycle vars
   Collecting garbage
   (Running finalizer)
   (Running finalizer)
   Returning

If we tweak the function to return control to a different
greenlet (the main greenlet) and then run garbage collection, the
cycle is also found:

.. doctest::
   :pyversion: >= 3.5

   >>> glet = greenlet(run)
   >>> _ = glet.switch(getcurrent().switch)
   Deleting cycle vars
   >>> collect_it()
   Collecting garbage
   (Running finalizer)
   (Running finalizer)
   >>> del glet
