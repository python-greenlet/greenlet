==============================================
 greenlet: Lightweight concurrent programming
==============================================

..
   TODO: Divide into a few different kinds of documentation
   (https://documentation.divio.com/explanation/):

   - Tutorial,
   - API reference
   - how-to.
   - Explanation.

   Each document should identify what role it fulfills.

.. |--| unicode:: U+2013   .. en dash
.. |---| unicode:: U+2014  .. em dash, trimming surrounding whitespace
   :trim:

.. sidebar:: Contents

   If this page has piqued your interest in greenlets,
   continue reading by seeing :ref:`an example transforming an
   asynchronous GUI into a simple synchronous loop <gui_example>`.

   To get started building your own code with greenlets, read
   :doc:`greenlet`, and then :doc:`creating_executing_greenlets`.

   .. toctree::
      :caption: Getting Started
      :maxdepth: 2

      gui_example
      greenlet
      creating_executing_greenlets
      switching

   .. toctree::
      :maxdepth: 1
      :caption: Reference Material

      api
      c_api
      changes
      development
      history

   .. toctree::
      :maxdepth: 1
      :caption: Advanced Usage

      python_threads
      contextvars
      greenlet_gc
      tracing
      caveats

.. rubric:: What are greenlets?

greenlets are lightweight coroutines for in-process sequential concurrent
programming.

greenlets can be used on their own, but they are frequently used with
frameworks such as `gevent`_ to provide higher-level abstractions and
asynchronous I/O.

greenlets are frequently defined by analogy to :mod:`threads
<threading>` or Python's built-in coroutines (generators and ``async
def`` functions). The rest of this section will explore those
analogies. For a more precise introduction, see :ref:`what_is_greenlet`.

See :doc:`history` for how the greenlet library was created, and its
relation to other similar concepts.

.. rubric:: Are greenlets similar to threads?

For many purposes, you can usually think of greenlets as cooperatively
scheduled :mod:`threads <threading>`. The major differences are
that since they're cooperatively scheduled, you are in control of
when they execute, and since they are coroutines, many greenlets can
exist in a single native thread.

.. rubric:: How are greenlets different from threads?

Threads (in theory) are preemptive and parallel [#f1]_, meaning that multiple
threads can be processing work at the same time, and it's impossible
to say in what order different threads will proceed or see the effects
of other threads. This necessitates careful programming using
:class:`locks <threading.Lock>`, :class:`queues <queue.Queue>`, or
other approaches to avoid `race conditions`_, `deadlocks`_, or other
bugs.

In contrast, greenlets are cooperative and sequential. This means that
when one greenlet is running, no other greenlet can be running; the
programmer is fully in control of when execution switches between
greenlets. This can eliminate race conditions and greatly simplify the
programming task.

Also, threads require resources from the operating system (the thread
stack, and bookkeeping in the kernel). Because greenlets are
implemented entirely without involving the operating system, they can
require fewer resources; it is often practical to have many more
greenlets than it is threads.

.. _race conditions: https://en.wikipedia.org/wiki/Race_condition
.. _deadlocks: https://docs.microsoft.com/en-us/troubleshoot/dotnet/visual-basic/race-conditions-deadlocks#when-deadlocks-occur

.. rubric:: How else can greenlets be used?

greenlets have many uses:

- They can be treated like cooperative threads. You can implement any
  scheduling policy you desire.
- Because greenlets work well with C libraries (greenlets can switch
  even with C functions in the call stack), they are well suited for
  integrating with GUIs or other event loops.

  `gevent`_ is an example of using greenlets to integrate with IO
  event loops (`libev`_ and `libuv`_) to provide a complete
  asynchronous environment using familiar programming patterns.
- Similar to the above, greenlets can be used to transform apparently
  asynchronous tasks into a simple synchronous style. See
  :ref:`gui_example` for an example of writing an asynchronous event-driven GUI app
  in a simple synchronous style.
- In general, greenlets can be used for advanced control flow. For
  example, you can :doc:`create generators <history>` |---| without
  the use of the ``yield`` keyword!


.. _gevent: https://www.gevent.org
.. _libev: http://software.schmorp.de/pkg/libev.html
.. _libuv: http://libuv.org/

.. rubric:: Are greenlets similar to generators? What about asyncio?

All three of greenlets, generators, and asyncio use a concept of
coroutines. However, greenlets, unlike the other two, require no
special keywords or support from the Python language. In addition,
greenlets are capable of switching between stacks that feature C
libraries, whereas the other two are not.


.. rubric:: Footnotes

.. [#f1] In CPython, the `global interpreter lock (GIL)
         <https://wiki.python.org/moin/GlobalInterpreterLock>`_
         generally prevents two threads from executing Python code at
         the same time. Parallelism is thus limited to code sections
         that release the GIL, i.e., C code.

Indices and tables
==================

* :ref:`search`
* :ref:`genindex`
* :ref:`modindex`
