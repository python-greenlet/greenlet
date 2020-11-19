======================
 Python API Reference
======================

.. currentmodule:: greenlet

Exceptions
==========

.. autoexception:: GreenletExit
.. autoexception:: error

Greenlets
=========

.. autofunction:: getcurrent

.. autoclass:: greenlet

   Greenlets support boolean tests: ``bool(g)`` is true if ``g`` is
   active and false if it is dead or not yet started.

   .. method:: switch(*args, **kwargs)

      Switches execution to this greenlet. See :ref:`switching`.

   .. automethod:: throw

   .. autoattribute:: dead

      True if this greenlet is dead (i.e., it finished its execution).

   .. autoattribute:: gr_context


      The :class:`contextvars.Context` in which ``g`` will run.
      Writable; defaults to ``None``, reflecting that a greenlet
      starts execution in an empty context unless told otherwise.
      Generally, this should only be set once, before a greenlet
      begins running. Accessing or modifying this attribute raises
      :exc:`AttributeError` on Python versions 3.6 and earlier (which
      don't natively support the `contextvars` module) or if
      ``greenlet`` was built without contextvars support.

      For more information, see :doc:`contextvars`.

      .. versionadded:: 1.0.0

   .. autoattribute:: gr_frame

      The frame that was active in this greenlet when it most recently
      called ``some_other_greenlet.switch()``, and that will resume
      execution when ``this_greenlet.switch()`` is next called. The remainder of
      the greenlet's stack can be accessed by following the frame
      object's ``f_back`` attributes. ``gr_frame`` is non-None only
      for suspended greenlets; it is None if the greenlet is dead, not
      yet started, or currently executing.

   .. autoattribute:: parent

      The parent greenlet. This is writable, but it is not allowed to create
      cycles of parents.

      A greenlet without a parent is the main greenlet of its thread.

      Cannot be set to anything except a greenlet.

   .. autoattribute:: run

      The callable that this greenlet will run when it starts. After
      it is started, this attribute no longer exists.

      Subclasses can define this as a method on the type.



Tracing
=======

For details on tracing, see :doc:`tracing`.

.. autofunction:: gettrace

.. autofunction:: settrace

   :param callback: A callable object with the signature
                    ``callback(event, args)``.
