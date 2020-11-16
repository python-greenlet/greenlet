==============================================
 greenlet: Lightweight concurrent programming
==============================================

.. TODO: Refactor and share the opening paragraphs with README.rst
.. TODO: Break into a few pieces: Introduction, tutorial, API
   reference, etc.

Contents
========

.. toctree::
   :maxdepth: 1

   greenlet
   contextvars
   tracing
   api
   c_api
   changes
   development


.. |--| unicode:: U+2013   .. en dash
.. |---| unicode:: U+2014  .. em dash, trimming surrounding whitespace
   :trim:


Motivation
==========

The "greenlet" package is a spin-off of `Stackless`_, a version of CPython
that supports micro-threads called "tasklets". Tasklets run
pseudo-concurrently (typically in a single or a few OS-level threads) and
are synchronized with data exchanges on "channels".

A "greenlet", on the other hand, is a still more primitive notion of
micro-thread with no implicit scheduling; coroutines, in other words.
This is useful when you want to
control exactly when your code runs. You can build custom scheduled
micro-threads on top of greenlet; however, it seems that greenlets are
useful on their own as a way to make advanced control flow structures.
For example, we can recreate generators; the difference with Python's own
generators is that our generators can call nested functions and the nested
functions can yield values too. (Additionally, you don't need a "yield"
keyword. See the example in ``test/test_generator.py``).

Greenlets are provided as a C extension module for the regular unmodified
interpreter.

.. _`Stackless`: http://www.stackless.com

Example
-------

Let's consider a system controlled by a terminal-like console, where the user
types commands. Assume that the input comes character by character. In such
a system, there will typically be a loop like the following one:

.. doctest::

    >>> def echo_user_input(user_input):
    ...     print('    <<< ' + user_input.strip())
    ...     return user_input
    >>> def process_commands():
    ...    while True:
    ...        line = ''
    ...        while not line.endswith('\n'):
    ...            line += read_next_char()
    ...        echo_user_input(line)
    ...        if line == 'quit\n':
    ...            print("Are you sure?")
    ...            if echo_user_input(read_next_char()) != 'y':
    ...                continue    # ignore the command
    ...            print("(Exiting loop.)")
    ...            break # stop the command loop
    ...        process_command(line)

Now assume that you want to plug this program into a GUI. Most GUI
toolkits are event-based. They will invoke a call-back for each
character the user presses (``event_keydown(key)``) [#f1]_. In this setting,
it is difficult to implement the ``read_next_char()`` function needed
by the code above. We have two incompatible functions:

.. doctest::

    >>> def event_keydown(key):
    ...    "Called by the event system asynchronously."

    >>> def read_next_char():
    ...    """
    ...    Called from `process_commands`; should wait for
    ...    the next `event_keydown()` call.
    ...    """

You might consider doing that with threads. Greenlets are an alternate
solution that don't have the related locking and shutdown problems. You
start the ``process_commands()`` function in its own, separate greenlet, and
then you exchange the keypresses with it as follows:

.. doctest::

    >>> from greenlet import greenlet
    >>> g_processor = greenlet(process_commands)
    >>> def event_keydown(key):
    ...     # jump into g_processor, sending it the key
    ...     g_processor.switch(key)

    >>> def read_next_char():
    ...     # g_self is g_processor in this simple example
    ...     g_self = greenlet.getcurrent()
    ...     assert g_self is g_processor
    ...     # jump to the parent (main) greenlet, where the GUI event
    ...     # loop is running, and wait for the next key
    ...     main_greenlet = g_self.parent
    ...     next_char = main_greenlet.switch()
    ...     return next_char

Next, we can start the processor, which will immediately switch back
to the main greenlet:

.. doctest::

    >>> _ = g_processor.switch()

Now we can hand control over to the main event loop of the GUI. Of
course, in documentation we don't have a gui, so we'll fake one that
feeds keys to ``event_keydown``; we'll also fake a ``process_command``
function that just prints the line it got.

.. doctest::

   >>> def process_command(line):
   ...     print('(Processing command: ' + line.strip() + ')')

   >>> def gui_mainloop():
   ...    # The user types "hello"
   ...    for c in 'hello\n':
   ...        event_keydown(c)
   ...    # The user types "quit"
   ...    for c in 'quit\n':
   ...        event_keydown(c)
   ...    # The user responds to the prompt with 'y'
   ...    event_keydown('y')

   >>> gui_mainloop()
       <<< hello
   (Processing command: hello)
       <<< quit
   Are you sure?
       <<< y
   (Exiting loop.)

In this example, the execution flow is: when ``read_next_char()`` is called, it
is part of the ``g_processor`` greenlet, so when it switches to its parent
greenlet, it resumes execution in the top-level main loop (the GUI). When
the GUI calls ``event_keydown()``, it switches to ``g_processor``, which means that
the execution jumps back wherever it was suspended in that greenlet |---| in
this case, to the ``switch()`` instruction in ``read_next_char()`` |---| and the ``key``
argument in ``event_keydown()`` is passed as the return value of the switch() in
``read_next_char()``.

Note that ``read_next_char()`` will be suspended and resumed with its call stack
preserved, so that it will itself return to different positions in
``process_commands()`` depending on where it was originally called from. This
allows the logic of the program to be kept in a nice control-flow way; we
don't have to completely rewrite ``process_commands()`` to turn it into a state
machine.

Continue reading with :doc:`greenlet`.


.. rubric:: Footnotes

.. [#f1]  Replace "GUI" with "XML expat parser" if that rings more bells to
          you. In general, it can be framework that issues asynchronous callbacks.


Indices and tables
==================

* :ref:`search`
