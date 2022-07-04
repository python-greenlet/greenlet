==============================
 Greenlets and Python Threads
==============================

Greenlets can be combined with Python threads; in this case, each thread
contains an independent "main" greenlet with a tree of sub-greenlets. It
is not possible to mix or switch between greenlets belonging to different
threads.

.. doctest::

   >>> from greenlet import getcurrent
   >>> from greenlet import greenlet
   >>> from threading import Thread
   >>> from threading import Event
   >>> started = Event()
   >>> switched = Event()
   >>> class T(Thread):
   ...     def run(self):
   ...         self.main_glet = getcurrent()
   ...         self.child_glet = greenlet(lambda: None)
   ...         self.child_glet.switch()
   ...         started.set()
   ...         switched.wait()
   >>> t = T()
   >>> t.start()
   >>> _ = started.wait()
   >>> t.main_glet.switch()
   Traceback (most recent call last):
   ...
   greenlet.error: cannot switch to a different thread
   >>> switched.set()
   >>> t.join()

Prior to greenlet 2.0, when a thread dies, the thread's main greenlet was not
considered to be dead. This has been changed in greenlet 2.0; however,
observing this property is still a race condition, and, on some
platforms (those that cannot use the C runtime to detect when a thread
exits), not guaranteed (because of the potential for uncollectible
reference cycles to keep the Python thread state alive). In addition,
this is considered an implementation detail and may not be true in all
greenlet implementations.

.. doctest::

   >>> t.main_glet.dead
   True

.. caution::

   For these reasons, it's best to not pass references to a greenlet
   running in one thread to another thread. If you do, take caution to
   carefully manage the lifetime of the references. If greenlets that
   are suspended in one thread are referenced from another thread,
   row memory and Python objects can leak.

In greenlet 2.0, the error message for attempting to switch into a
dead thread makes that fact clear; again, this is an implementation
detail and should not be relied upon.

.. doctest::

   >>> t.child_glet.switch()
   Traceback (most recent call last):
   ...
   greenlet.error: cannot switch to a different thread (which happens to have exited)
   >>> t.main_glet.switch()
   Traceback (most recent call last):
   ...
   greenlet.error: cannot switch to a garbage collected greenlet
