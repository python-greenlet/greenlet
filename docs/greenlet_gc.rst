===================================
 Garbage-collecting Live greenlets
===================================

If all the references to a greenlet object go away (including the
references from the parent attribute of other greenlets), then there
is no way to ever switch back to this greenlet. In this case, a
:exc:`GreenletExit` exception is generated into the greenlet. This is
the only case where a greenlet receives the execution asynchronously
(without an explicit call to :meth:`greenlet.switch`). This gives
``try/finally`` blocks a chance to clean up resources held by the
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
