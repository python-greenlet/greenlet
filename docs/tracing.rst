=======================
 Tracing And Profiling
=======================

.. currentmodule:: greenlet

Standard Python tracing and profiling doesn't work as expected when used with
greenlet since stack and frame switching happens on the same Python thread.
It is difficult to detect greenlet switching reliably with conventional
methods, so to improve support for debugging, tracing and profiling greenlet
based code there are new functions in the greenlet module, `gettrace`
and `settrace`.

Trace Callback Functions
========================

Trace callback functions are installed using `settrace` and must have
the signature ``callback(event: str, args: Any)``.

.. important::
    For compatibility it is very important to unpack ``args`` tuple only when
    ``event`` is one of those defined here, and not when ``event`` is
    potentially something else. This way API can be extended to new events
    similar to :func:`sys.settrace()`.

The parameter *event* is a string naming what happened. The following
events are defined:

``switch``
  In this case, ``args`` is a two-tuple ``(origin, target)``.

  Called to handle a switch from ``origin`` to ``target``.

  Note that callback is running in the context of the ``target``
  greenlet and any exceptions will be passed as if
  ``target.throw()`` was used instead of a switch.

``throw``
  In this case, ``args`` is a two-tuple ``(origin, target)``.

  Called to handle a throw from ``origin`` to ``target``.

  Note that callback is running in the context of ``target``
  greenlet and any exceptions will replace the original, as
  if ``target.throw()`` was used with the replacing exception.

For example:

.. doctest::

    >>> import greenlet
    >>> def callback(event, args):
    ...     if event in ('switch', 'throw'):
    ...         origin, target = args
    ...         print("Transfer from %s to %s with %s"
    ...               % (origin, target, event))

    >>> class Origin(greenlet.greenlet):
    ...     def run(self):
    ...         print("In origin")
    ...         target.switch()
    ...         print("Returned to origin")
    ...         target.throw()
    ...     def __str__(self):
    ...        return "<origin>"

    >>> class Target(greenlet.greenlet):
    ...     def run(self):
    ...         origin.switch()
    ...     def __str__(self):
    ...        return "<target>"

    >>> old_trace = greenlet.settrace(callback)
    >>> origin = Origin()
    >>> target = Target()
    >>> _ = origin.switch()
    Transfer from <greenlet.greenlet object ...> to <origin> with switch
    In origin
    Transfer from <origin> to <target> with switch
    Transfer from <target> to <origin> with switch
    Returned to origin
    Transfer from <origin> to <target> with throw
    Transfer from <target> to <greenlet.greenlet object ...> with switch

Of course, when we're done, it's important to restore the previous
state:

.. doctest::

   >>> _ = greenlet.settrace(old_trace)
