==============================
 Greenlets and Python Threads
==============================

Greenlets can be combined with Python threads; in this case, each thread
contains an independent "main" greenlet with a tree of sub-greenlets. It
is not possible to mix or switch between greenlets belonging to different
threads.

.. doctest::

   >>> from greenlet import getcurrent
   >>> from threading import Thread
   >>> from threading import Event
   >>> started = Event()
   >>> switched = Event()
   >>> class T(Thread):
   ...     def run(self):
   ...         self.glet = getcurrent()
   ...         started.set()
   ...         switched.wait()
   >>> t = T()
   >>> t.start()
   >>> _ = started.wait()
   >>> t.glet.switch()
   Traceback (most recent call last):
   ...
   greenlet.error: cannot switch to a different thread
   >>> switched.set()
   >>> t.join()

Note that when a thread dies, the thread's main greenlet is not
considered to be dead.

.. doctest::

   >>> t.glet.dead
   False

.. caution::

   For these reasons, it's best to not pass references to a greenlet
   running in one thread to another thread. If you do, take caution to
   carefully manage the lifetime of the references.
