from greenlet import greenlet

from . import TestCase

def Yield(value):
    """Pauses the current worker and sends a value to its parent greenlet."""
    parent = greenlet.getcurrent().parent
    if not isinstance(parent, genlet):
        raise RuntimeError("yield outside a genlet")
    parent.switch(value)

class _YieldFromMarker:
    """Internal object that signals a `yield from` request to the trampoline."""
    def __init__(self, task):
        self.task = task

def YieldFrom(func, *args, **kwargs):
    """
    Creates a marker for the trampoline to delegate to another generator.
    It unwraps the decorated function to get the raw logic.
    """
    # Access the original, undecorated function that the @generator stored.
    raw_func = getattr(func, '_raw_func', func)
    marker = _YieldFromMarker((raw_func, args, kwargs))
    Yield(marker)

class genlet(greenlet):
    """
    A greenlet that acts as a generator. It uses an internal trampoline to manage a stack of tasks,
    achieving O(1) performance for each deep delegated `yield from`.
    """
    def __init__(self, initial_task):
        super().__init__(self.run)
        self.initial_task = initial_task
        self.consumer = None

    def __iter__(self):
        return self

    def __next__(self):
        # The consumer is the greenlet that called `next()`.
        self.consumer = greenlet.getcurrent()

        # Switch to the `run` method to get the next value.
        result = self.switch()

        # After the switch, the trampoline either sends a value or finishes.
        if self.dead:
            raise StopIteration
        return result

    def run(self):
        """
        The trampoline. It manages a stack of worker greenlets and never builds
        a deep Python call stack itself.
        """
        worker_stack = []

        func, args, kwargs = self.initial_task
        # The `active_worker` is the greenlet executing user code. Its `parent`
        # is automatically set to `self` (this genlet instance) on creation.
        active_worker = greenlet(func)

        # Start the first worker and capture the first value it yields.
        yielded = active_worker.switch(*args, **kwargs)

        while True:
            # Case 1: Delegation (`yield from`).
            # The worker wants to delegate to a sub-generator.
            if isinstance(yielded, _YieldFromMarker):
                # Pause the current worker by pushing it onto the stack.
                worker_stack.append(active_worker)

                # Create and start the new child worker.
                child_func, child_args, child_kwargs = yielded.task
                active_worker = greenlet(child_func)
                yielded = active_worker.switch(*child_args, **child_kwargs)
                continue

            # Case 2: A worker has finished.
            # The worker function has returned, so its greenlet is now "dead".
            if active_worker.dead:
                # If there are no parent workers waiting, the whole process is done.
                if not worker_stack:
                    break

                # A sub-generator finished. Pop its parent from the stack
                # to make it the active worker again and resume it.
                active_worker = worker_stack.pop()
                yielded = active_worker.switch()
                continue

            # Case 3: A real value was yielded.
            # 1. Send the value to the consumer (the loop calling `next()`).
            self.consumer.switch(yielded)

            # 2. After the consumer gets the value, control comes back here.
            #    Resume the active worker to ask for the next value.
            yielded = active_worker.switch()

def generator(func):
    """
    Decorator that turns a function using `Yield`/`YieldFrom` into a generator.
    It stores a reference to the original function to allow `YieldFrom` to work.
    """
    def wrapper(*args, **kwargs):
        # This wrapper is what the user calls. It creates the main genlet.
        return genlet((func, args, kwargs))

    # Store the raw function so YieldFrom can access it and bypass this wrapper.
    wrapper._raw_func = func
    return wrapper


# =============================================================================
#  Test Cases
# =============================================================================


@generator
def hanoi(n, a, b, c):
    if n > 1:
        YieldFrom(hanoi, n - 1, a, c, b)
    Yield(f'{a} -> {c}')
    if n > 1:
        YieldFrom(hanoi, n - 1, b, a, c)

@generator
def make_integer_sequence(n):
    if n > 1:
        YieldFrom(make_integer_sequence, n - 1)
    Yield(n)

@generator
def empty_gen():
    pass

class DeeplyNestedGeneratorTests(TestCase):
    def test_hanoi(self):
        results = list(hanoi(3, 'A', 'B', 'C'))
        self.assertEqual(
                results,
                ['A -> C', 'A -> B', 'C -> B', 'A -> C', 'B -> A', 'B -> C', 'A -> C'],
        )

    def test_make_integer_sequence(self):
        # It does not require `sys.setrecursionlimit` to set the recursion limit to a higher value,
        # since the `yield from` logic is managed as a `task` variable on the heap, and
        # the control is passed via `greenlet.switch()` instead of recursive function calls.
        #
        # Besides, if we use the built-in `yield` and `yield from` instead in the function
        # `make_integer_sequence`, the time complexity will increase from O(n) to O(n^2).
        results = list(make_integer_sequence(2000))
        self.assertEqual(results, list(range(1, 2001)))

    def test_empty_gen(self):
        for _ in empty_gen():
            self.fail('empty generator should not yield anything')
