
def raises(exc, fn, *args, **kw):
    try:
        fn(*args, **kw)
    except exc:
        return
    assert False, "did not raise " + exc.__name__
