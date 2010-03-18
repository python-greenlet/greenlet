import greenlet
import _test_extension

def test_switch():
    assert 50 == _test_extension.test_switch(greenlet.greenlet(lambda: 50))

def test_switch_kwargs():
    def foo(x, y):
        return x * y
    g = greenlet.greenlet(foo)
    assert 6 == _test_extension.test_switch_kwargs(g, x=3, y=2)

def test_setparent():
    def foo():
        def bar():
            greenlet.getcurrent().parent.switch()

            # This final switch should go back to the main greenlet, since the
            # test_setparent() function in the C extension should have
            # reparented this greenlet.
            greenlet.getcurrent().parent.switch()
            raise AssertionError("Should never have reached this code")
        child = greenlet.greenlet(bar)
        child.switch()
        greenlet.getcurrent().parent.switch(child)
        greenlet.getcurrent().parent.throw(
            AssertionError("Should never reach this code"))
    foo_child = greenlet.greenlet(foo).switch()
    assert None == _test_extension.test_setparent(foo_child)

def test_getcurrent():
    _test_extension.test_getcurrent()

def test_new_greenlet():
    assert -15 == _test_extension.test_new_greenlet(lambda: -15)

def test_raise_greenlet_dead():
    try:
        _test_extension.test_raise_dead_greenlet()
    except greenlet.GreenletExit:
        return
    raise AssertionError("greenlet.GreenletExit exception was not raised.")

def test_raise_greenlet_error():
    try:
        _test_extension.test_raise_greenlet_error()
    except greenlet.error:
        return
    raise AssertionError("greenlet.error exception was not raised.")

def test_throw():
    seen = []
    def foo():
        try:
            greenlet.getcurrent().parent.switch()
        except ValueError, e:
            seen.append(e)
        except greenlet.GreenletExit:
            raise AssertionError
    g = greenlet.greenlet(foo)
    g.switch()
    _test_extension.test_throw(g)
    assert len(seen) == 1 and isinstance(seen[0], ValueError), \
           "ValueError was not raised in foo()"
    assert str(seen[0]) == 'take that sucka!', "message doesn't match"
