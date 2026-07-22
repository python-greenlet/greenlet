"""Regression test for issue #515: GC of a suspended greenlet's C-stack refs.

A greenlet that suspends mid attribute-resolution holds a ``_PyCStackRef`` to
the looked-up object on its C stack. The free-threaded collector only walks the
running thread's refs, so greenlet must visit a suspended greenlet's from
``tp_traverse``, or an object reachable only through them is freed early.

The test pins a deferred-refcounted class (a metaclass ``__get__`` makes the
class act as a descriptor), switches away from inside ``__get__``, drops every
other reference, and collects. It passes only if the class is still alive and
the suspended greenlet is what keeps it alive. Prints "C STACK REFS GC OK" and
exits 0 on a fixed build, non-zero on a regressed one.
"""
import gc
import sys
import sysconfig
import weakref

import greenlet

# Only free-threaded builds have the C-stack-ref / deferred-refcount machinery.
FREETHREAD = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))

parent = greenlet.getcurrent()
observed = {}


class Meta(type):
    def __get__(cls, obj, objtype=None):
        # ``cls`` is the class being resolved, pinned in a _PyCStackRef across
        # this call. Drop the descriptor-protocol locals so a suspended frame
        # can't keep the class alive on its own and mask the bug.
        del cls, obj, objtype
        child.switch()
        return 42


pinned = Meta('pinned', (), {})                    # a class => deferred-refcounted
holder = type('holder', (), {'attr': pinned})      # holder.attr invokes Meta.__get__
box = [pinned]
del pinned


def child_work():
    # parent is suspended inside Meta.__get__ holding a C-stack ref to the class.
    # Drop every other reference, then collect.
    ref = weakref.ref(box[0])
    box[0] = None
    del holder.attr
    for _ in range(5):
        gc.collect()
    cls = ref()
    if cls is None:
        observed['status'] = 'collected'
    elif not FREETHREAD or any(r is parent for r in gc.get_referrers(cls)):
        # Kept alive by the suspended greenlet (or, with the GIL, by refcounting).
        observed['status'] = 'ok'
    else:
        # Alive, but not because of the greenlet: a masking reference hid the
        # C-stack-ref path (the class's own mro/bases are a self-cycle, not one).
        observed['status'] = 'masked'
        own = (cls.__mro__, cls.__bases__)
        observed['maskers'] = sorted(
            {type(r).__name__ for r in gc.get_referrers(cls)
             if r is not parent and r is not own[0] and r is not own[1]}
        ) or ['<invisible to gc; strong C-stack ref / non-deferred class>']
    del cls
    parent.switch()


child = greenlet.greenlet(child_work)
result = holder().attr
assert result == 42, result

status = observed.get('status')
print(f"py={sys.version.split()[0]} gil={getattr(sys, '_is_gil_enabled', lambda: True)()} "
      f"greenlet={greenlet.__version__} status={status}", flush=True)
if status == 'collected':
    raise SystemExit("REGRESSED: class reachable only through a suspended "
                     "greenlet's C-stack ref was collected early")
if status == 'masked':
    raise SystemExit("REGRESSED: class stayed alive but not through the suspended "
                     "greenlet; a masking reference (%s) hid the C-stack-ref path"
                     % ', '.join(observed.get('maskers', ())))
print("C STACK REFS GC OK", flush=True)
