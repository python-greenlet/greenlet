
.. _gui_example:

==================================================================
 Motivation: Treating an Asynchronous GUI Like a Synchronous Loop
==================================================================

.. currentmodule:: greenlet

In this document, we'll demonstrate how greenlet can be used to
connect synchronous and asynchronous operations, without introducing
any additional threads or race conditions. We'll use the example of
transforming a "pull"-based console application into an asynchronous
"push"-based GUI application *while still maintaining the simple
pull-based structure*.

Similar techniques work with XML expat parsers; in general, it can be
framework that issues asynchronous callbacks.

.. |--| unicode:: U+2013   .. en dash
.. |---| unicode:: U+2014  .. em dash, trimming surrounding whitespace
   :trim:


A Simple Terminal App
=====================

Let's consider a system controlled by a terminal-like console, where
the user types commands. Assume that the input comes character by
character. In such a system, there will typically be a loop like the
following one:

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

Here, we have an infinite loop. The job of the loop is to read characters
that the user types, accumulate that into a command line, and then
execute the command. The heart of the loop is around ``read_next_char()``:

.. doctest::

    >>> def read_next_char():
    ...    """
    ...    Called from `process_commands`;
    ...    blocks until a character has been typed and returns it.
    ...    """

This function might be implemented by simply reading from
:obj:`sys.stdin`, or by something more complex such as
:meth:`curses.window.getch`, but in any case, it doesn't return until
a key has been read from the user.

Competing Event Loops
=====================

Now assume that you want to plug this program into a GUI. Most GUI
toolkits are event-based. Internally, they run their own infinite loop
much like the one we wrote above, invoking a call-back for each
character the user presses (``event_keydown(key)``).

.. doctest::

    >>> def event_keydown(key):
    ...    "Called by the event system *asynchronously*."


In this setting, it is difficult to implement the ``read_next_char()``
function needed by the code above. We have two incompatible functions.
First, there's the function the GUI will call asynchronously to notify
about an event; it's important to stress that we're not in control of
when this function is called |---| in fact, our code isn't in the call
stack at all, the GUI's loop is the only thing running. But that
doesn't fit with our second function, ``read_next_char()`` which itself
is supposed to be blocking and called from the middle of its own loop.

How can we fit this asynchronous delivery mechanism together with our
synchronous, blocking function that reads the next character the user
types?

Enter greenlets: Dual Infinite Loops
====================================

You might consider doing that with :class:`threads <threading.Thread>`
[#f1]_, but that can get complicated rather quickly. greenlets are an
alternate solution that don't have the related locking and other
problems threads introduce.

By introducing a greenlet to run ``process_commands``, and having it
communicate with the greenlet running the GUI event loop, we can
effectively have a single thread be *in the middle of two infinite
loops at once* and switch between them as desired. Pretty cool.

It's even cooler when you consider that the GUI's loop is likely to be
implemented in C, not Python, so we'll be switching between infinite
loops both in native code and in the Python interpreter.

First, let's create a greenlet to run the ``process_commands`` function
(note that we're not starting it just yet, only defining it).

.. doctest::

    >>> from greenlet import greenlet
    >>> g_processor = greenlet(process_commands)

Now, we need to arrange for the communication between the GUI's event
loop and its callback ``event_keydown`` (running in the implicit main
greenlet) and this new greenlet. The changes to ``event_keydown`` are
pretty simple: just send the key the GUI gives us into the loop that
``process_commands`` is in using :meth:`greenlet.switch`.

.. doctest::

    >>> main_greenlet = greenlet.getcurrent()
    >>> def event_keydown(key): # running in main_greenlet
    ...     # jump into g_processor, sending it the key
    ...     g_processor.switch(key)

The other side of the coin is to define ``read_next_char`` to accept
this key event. We do this by letting the main greenlet run the GUI
loop until the GUI loop jumps back to is from ``event_keydown``:

.. doctest::

    >>> def read_next_char(): # running in g_processor
    ...     # jump to the main greenlet, where the GUI event
    ...     # loop is running, and wait for the next key
    ...     next_char = main_greenlet.switch('blocking in read_next_char')
    ...     return next_char

Having defined both functions, we can start the ``process_commands``
greenlet, which will make it to ``read_next_char()`` and immediately
switch back to the main greenlet:

.. doctest::

    >>> g_processor.switch()
    'blocking in read_next_char'

Now we can hand control over to the main event loop of the GUI. Of
course, in documentation we don't have a GUI, so we'll fake one that
feeds keys to ``event_keydown``; for demonstration purposes we'll also
fake a ``process_command`` function that just prints the line it got.

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
   >>> g_processor.dead
   True

.. sidebar:: Switching Isn't Contagious

   Notice how a single call to ``gui_mainloop`` successfully switched
   back and forth between two greenlets without the caller or author of
   ``gui_mainloop`` needing to be aware of that.

   Contrast this with :mod:`asyncio`, where the keywords ``async def`` and
   ``await`` often spread throughout the codebase once introduced.

   In fact, greenlets can be used to put a halt to that spread and
   execute ``async def`` code in a synchronous fashion.

   .. seealso::

      For the interactions between :mod:`contextvars` and greenlets.
          :doc:`contextvars`

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

Further Reading
===============

Continue reading with :doc:`greenlet`.

Curious how execution resumed in the main greenlet after
``process_commands`` exited its loop (and never explicitly switched
back to the main greenlet)? Read about :ref:`greenlet_parents`.

.. rubric:: Footnotes

.. [#f1]  You might try to run the GUI event loop in one thread, and
          the ``process_commands`` function in another thread. You
          could then use a thread-safe :class:`queue.Queue` to
          exchange keypresses between the two: write to the queue in
          ``event_keydown``, read from it in ``read_next_char``. One
          problem with this, though, is that many GUI toolkits are
          single-threaded and only run in the main thread, so we'd
          also need a way to communicate any results of
          ``process_command`` back to the main thread in order to
          update the GUI. We're now significantly diverging from our
          simple console-based application.
