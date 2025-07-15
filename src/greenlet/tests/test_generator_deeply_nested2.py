from greenlet import greenlet

from . import TestCase

class YieldFromMarker:
    """A special object to signal a `yield from` request."""
    def __init__(self, iterable):
        self.iterable = iterable

class genlet(greenlet):
    """
    A greenlet that is also an iterator, designed to wrap a function
    and turn it into a generator that can be driven by a for loop.
    This version includes a trampoline to handle `yield from` efficiently.
    """
    def __init__(self, func, *args, **kwargs):
        # We need to capture the function to run, which is stored on the class
        # by the `generator` decorator.
        self.func = func
        self.args = args
        self.kwargs = kwargs
        # The stack of active iterators for the trampoline.
        self.iter_stack = []
        super().__init__(self.run)

    def __iter__(self):
        return self

    def __next__(self):
        # Set the parent to the consumer.
        self.parent = greenlet.getcurrent() # pylint:disable=attribute-defined-outside-init
        # Switch to the `run` method to get the next value.
        result = self.switch()

        if self:
            return result
        raise StopIteration

    def run(self):
        """
        The trampoline. This loop drives the user's generator logic. It manages
        a stack of iterators to achieve O(1) performance for each deep delegated `yield from`.
        """
        # Create the top-level generator from the user's function.
        top_level_generator = self.func(*self.args, **self.kwargs)
        self.iter_stack.append(top_level_generator)

        while self.iter_stack:
            try:
                # Get the value from the top-most generator on our stack.
                value = next(self.iter_stack[-1])

                if isinstance(value, YieldFromMarker):
                    # It's a `yield from` request.
                    sub_iterable = value.iterable
                    # Crucially, unpack the genlet into a simple generator
                    # to avoid nested trampolines.
                    if isinstance(sub_iterable, genlet):
                        sub_generator = sub_iterable.func(*sub_iterable.args, **sub_iterable.kwargs)
                        self.iter_stack.append(sub_generator)
                    else:
                        # Support yielding from standard iterables as well,
                        # e.g. `yield YieldFromMarker([1, 2, 3])`.
                        self.iter_stack.append(iter(sub_iterable))
                else:
                    # It's a regular value. Pass it back to the consumer
                    # (which is waiting in `__next__`).
                    self.parent.switch(value)

            except StopIteration:
                # The top-most generator is exhausted. Pop it from the stack
                # and continue with the one below it.
                self.iter_stack.pop()

        # If the stack is empty, the entire process is complete.
        # The greenlet will die, and `__next__` will raise StopIteration.

def generator(func):
    """A decorator to create a genlet class from a function."""
    def wrapper(*args, **kwargs):
        return genlet(func, *args, **kwargs)

    return wrapper


# =============================================================================
#  Test Cases
# =============================================================================


@generator
def hanoi(n, a, b, c):
    if n > 1:
        yield YieldFromMarker(hanoi(n - 1, a, c, b))
    yield f'{a} -> {c}'
    if n > 1:
        yield YieldFromMarker(hanoi(n - 1, b, a, c))

@generator
def make_integer_sequence(n):
    if n > 1:
        yield YieldFromMarker(make_integer_sequence(n - 1))
    yield n

@generator
def empty_gen():
    # The function body should contain at least one `yield` to make it a generator.
    if False: # pylint:disable=using-constant-test
        yield 1

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
        # Besides, if we use the built-in `yield from` instead of `yield YieldFromMarker` in the
        # function `make_integer_sequence`, the time complexity will increase from O(n) to O(n^2).
        results = list(make_integer_sequence(2000))
        self.assertEqual(results, list(range(1, 2001)))

    def test_empty_gen(self):
        for _ in empty_gen():
            self.fail('empty generator should not yield anything')
