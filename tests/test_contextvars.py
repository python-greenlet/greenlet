from functools import partial
from greenlet import greenlet
from greenlet import getcurrent
from greenlet import GREENLET_USE_CONTEXT_VARS
import unittest

if GREENLET_USE_CONTEXT_VARS:
    from contextvars import Context
    from contextvars import ContextVar
    from contextvars import copy_context

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

            callback = getcurrent().switch
            counts = dict((i, 0) for i in range(5))

            lets = [
                greenlet(partial(
                    self._increment,
                    greenlet_id=i,
                    ctx_var=id_var,
                    callback=callback,
                    counts=counts,
                    expect=0 if propagate else None,
                ), context=propagate)
                for i in range(1, 5)
            ]

            for i in range(2):
                counts[id_var.get()] += 1
                for let in lets:
                    let.switch()

            self.assertEqual(set(counts.values()), set([2]))

        def test_context_propagated(self):
            self._new_ctx_run(self._test_context, True)

        def test_context_not_propagated(self):
            self._new_ctx_run(self._test_context, False)

        def test_break_ctxvars(self):
            let1 = greenlet(copy_context().run)
            let2 = greenlet(copy_context().run)
            let1.switch(getcurrent().switch)
            let2.switch(getcurrent().switch)
            # Since let2 entered the current context and let1 exits its own, the
            # interpreter emits:
            # RuntimeError: cannot exit context: thread state references a different context object
            let1.switch()

        def test_context_kwarg(self):
            var = ContextVar("var", default=123)

            ctx = Context()
            ctx.run(var.set, 456)

            self.assertEqual(greenlet(var.get).switch(), 123)
            self.assertEqual(greenlet(var.get, context=None).switch(), 123)
            self.assertEqual(greenlet(var.get, context=True).switch(), 123)
            self.assertEqual(greenlet(var.get, context=False).switch(), 123)
            self.assertEqual(greenlet(var.get, context=ctx).switch(), 456)
            self.assertEqual(greenlet(var.get, getcurrent(), ctx).switch(), 456)

            var.set(789)

            self.assertEqual(greenlet(var.get).switch(), 123)
            self.assertEqual(greenlet(var.get, context=None).switch(), 123)
            self.assertEqual(greenlet(var.get, context=True).switch(), 789)
            self.assertEqual(greenlet(var.get, context=False).switch(), 123)
            self.assertEqual(greenlet(var.get, context=ctx).switch(), 456)
            self.assertEqual(greenlet(var.get, getcurrent(), ctx).switch(), 456)

            with self.assertRaises(TypeError):
                greenlet(var.get, getcurrent(), 123)

            with self.assertRaises(TypeError):
                greenlet(context=123)

            with self.assertRaises(TypeError):
                greenlet(context={})
