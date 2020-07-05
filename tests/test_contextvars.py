from functools import partial
import greenlet
import unittest

SKIP_TESTS = False
try:
    from contextvars import ContextVar
    from contextvars import copy_context
except ImportError:
    SKIP_TESTS = True


class ContextVarsTests(unittest.TestCase):
    def _new_ctx_run(self, *args, **kwargs):
        return copy_context().run(*args, **kwargs)

    def _increment(self, greenlet_id, ctx_var, callback, counts, expect):
        if expect is None:
            self.assertIsNone(ctx_var.get())
        else:
            self.assertEqual(ctx_var.get(), expect)
        ctx_var.set(greenlet_id)
        for i in range(2):
            counts[ctx_var.get()] += 1
            callback()

    def _test_context(self, propagate):
        id_var = ContextVar("id", default=None)
        id_var.set(0)

        callback = greenlet.getcurrent().switch
        counts = dict((i, 0) for i in range(5))

        lets = [
            greenlet.greenlet(partial(
                partial(
                    copy_context().run,
                    self._increment
                ) if propagate else self._increment,
                greenlet_id=i,
                ctx_var=id_var,
                callback=callback,
                counts=counts,
                expect=0 if propagate else None,
            ))
            for i in range(1, 5)
        ]

        for i in range(2):
            counts[id_var.get()] += 1
            for let in lets:
                let.switch()

        self.assertEqual(set(counts.values()), {2})

    @unittest.skipIf(SKIP_TESTS, "python runtime doesn't support contextvars")
    def test_context_propagated(self):
        self._new_ctx_run(self._test_context, True)

    @unittest.skipIf(SKIP_TESTS, "python runtime doesn't support contextvars")
    def test_context_not_propagated(self):
        self._new_ctx_run(self._test_context, False)
