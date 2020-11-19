==================================
 Creating And Executing Greenlets
==================================

.. This document is a mess. It's a cross between how-to and API
   reference.

.. currentmodule:: greenlet

To create a new greenlet, simply instantiate a new object of class
:class:`greenlet.greenlet`, passing it the initial function to run.

.. tip::

   If you're using a framework built on greenlets, such as
   :mod:`gevent`, consult its documentation. Some frameworks have
   other ways of creating new greenlets (for example,
   :func:`gevent.spawn`) or prefer a different greenlet class (for
   example, :class:`gevent.Greenlet`).

.. doctest::

   >>> import greenlet
   >>> def run():
   ...     print("Running in the greenlet function.")
   >>> glet = greenlet.greenlet(run)

The greenlet will have its ``run`` attribute set to the function you
passed, and its :ref:`parent <greenlet_parents>` will be the
:func:`current greenlet <getcurrent>`.

.. doctest::

   >>> glet.run is run
   True
   >>> glet.parent is greenlet.getcurrent()
   True

Execution of the greenlet begins when :meth:`greenlet.switch` is
called on it.

.. doctest::

   >>> glet.switch()
   Running in the greenlet function.

The ``run`` attribute is deleted at that time.

.. doctest::

   >>> glet.run
   Traceback (most recent call last):
   ...
   AttributeError: run

.. _subclassing_greenlet:

Subclassing greenlet
====================

You can also subclass :class:`greenlet.greenlet` and define ``run`` as
a method. This is useful to store additional state with the greenlet.

.. doctest::

   >>> import time
   >>> class MyGreenlet(greenlet.greenlet):
   ...     created_at = None
   ...     finished_at = None
   ...     def run(self):
   ...          self.created_at = time.time()
   ...          print("Running in the greenlet subclass.")
   ...          self.finished_at = time.time()
   >>> glet = MyGreenlet()
   >>> glet.switch()
   Running in the greenlet subclass.
   >>> glet.finished_at >= glet.created_at
   True

See :ref:`switching` for more information about switching into greenlets.

.. _changing_the_parent:

Changing The Parent
===================

When a greenlet finishes, :ref:`execution resumes with its parent
<greenlet_parents>`. This defaults to the current greenlet when the
object was instantiated, but can be changed either at that time or any
time later. To set it at creation time, pass the desired parent as the
second argument:

.. doctest::

   >>> def parent(child_result):
   ...     print("In the parent.")
   >>> parent_glet = greenlet.greenlet(parent)
   >>> def child():
   ...      print("In the child.")
   >>> child_glet = greenlet.greenlet(child, parent_glet)
   >>> child_glet.switch()
   In the child.
   In the parent.

To change it later, assign to the ``greenlet.parent`` attribute.

.. doctest::

   >>> parent_glet = greenlet.greenlet(parent)
   >>> child_glet = greenlet.greenlet(child)
   >>> child_glet.parent = parent_glet
   >>> child_glet.switch()
   In the child.
   In the parent.

Of course, cycles are not permitted.

.. doctest::

   >>> parent_glet = greenlet.greenlet(parent)
   >>> child_glet = greenlet.greenlet(child)
   >>> child_glet.parent = parent_glet
   >>> parent_glet.parent = child_glet
   Traceback (most recent call last):
   ...
   ValueError: cyclic parent chain

The parent must be a greenlet.

.. doctest::

   >>> parent_glet.parent = 42
   Traceback (most recent call last):
   ...
   TypeError: parent must be a greenlet

Interrupting Greenlets by Throwing Exceptions
=============================================

Besides simply :meth:`switching <greenlet.switch>` into a greenlet,
you can also have it resume execution by throwing an exception into
it. This is useful to interrupt a loop in the greenlet, for instance.

.. doctest::

   >>> main = greenlet.getcurrent()
   >>> class MyException(Exception):
   ...     pass
   >>> def run():
   ...     try:
   ...         main.switch()
   ...     except MyException:
   ...         print("Caught exception in greenlet.")
   >>> glet = greenlet.greenlet(run)
   >>> _ = glet.switch()
   >>> glet.throw(MyException)
   Caught exception in greenlet.

Uncaught exceptions thrown into the greenlet will propagate into the
parent greenlet.

.. doctest::

   >>> glet = greenlet.greenlet(run)
   >>> _ = glet.switch()
   >>> glet.throw(ValueError)
   Traceback (most recent call last):
   ...
   ValueError

As a special case, if the uncaught exception is
:exc:`greenlet.GreenletExit`, it will *not* propagate but instead be
returned. This is commonly used to signal an "expected exit".

.. doctest::

   >>> glet = greenlet.greenlet(run)
   >>> _ = glet.switch()
   >>> glet.throw(greenlet.GreenletExit)
   GreenletExit()
