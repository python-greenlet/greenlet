#!/usr/bin/env python
"""
Create a chain of coroutines and pass a value from one end to the
other, where each coroutine will increment the value before passing it
along.
"""

import os
import pyperf
import greenlet

# This is obsolete now, we always expose frames for Python 3.12.
# See https://github.com/python-greenlet/greenlet/pull/393/
# for a complete discussion of performance.
EXPOSE_FRAMES = 'EXPOSE_FRAMES' in os.environ

# Exposing
# 100 frames Mean +- std dev: 5.62 us +- 0.10 us
# 200 frames Mean +- std dev: 14.0 us +- 0.6 us
# 300 frames Mean +- std dev: 22.7 us +- 0.4 us
#
# Non-exposing
# 100 frames Mean +- std dev: 3.64 us +- 0.06 us -> 1.54/1.98us
# 200 frames Mean +- std dev: 9.49 us +- 0.13 us -> 1.47/4.51us
# 300 frames Mean +- std dev: 15.7 us +- 0.3 us  -> 1.45/7us

def link(next_greenlet):
    value = greenlet.getcurrent().parent.switch()
    next_greenlet.switch(value + 1)


CHAIN_GREENLET_COUNT = 100000

def bm_chain(loops):
    begin = pyperf.perf_counter()
    for _ in range(loops):
        start_node = greenlet.getcurrent()
        for _ in range(CHAIN_GREENLET_COUNT):
            g = greenlet.greenlet(link)
            g.gr_frames_always_exposed = EXPOSE_FRAMES
            g.switch(start_node)
            start_node = g
        x = start_node.switch(0)
        assert x == CHAIN_GREENLET_COUNT
    end = pyperf.perf_counter()
    return end - begin

GETCURRENT_INNER_LOOPS = 10
def bm_getcurrent(loops):
    getcurrent = greenlet.getcurrent
    getcurrent() # Factor out the overhead of creating the initial main greenlet
    begin = pyperf.perf_counter()
    for _ in range(loops):
        # Manual unroll
        getcurrent()
        getcurrent()
        getcurrent()
        getcurrent()
        getcurrent()
        getcurrent()
        getcurrent()
        getcurrent()
        getcurrent()
        getcurrent()
    end = pyperf.perf_counter()
    return end - begin

SWITCH_INNER_LOOPS = 10000
def bm_switch_shallow(loops):
    # pylint:disable=attribute-defined-outside-init
    class G(greenlet.greenlet):
        other = None
        def run(self):
            o = self.other
            for _ in range(SWITCH_INNER_LOOPS):
                o.switch()

    begin = pyperf.perf_counter()

    for _ in range(loops):
        gl1 = G()
        gl2 = G()
        gl1.gr_frames_always_exposed = EXPOSE_FRAMES
        gl2.gr_frames_always_exposed = EXPOSE_FRAMES
        gl1.other = gl2
        gl2.other = gl1
        gl1.switch()

        gl1.switch()
        gl2.switch()
        gl1.other = gl2.other = None
        assert gl1.dead
        assert gl2.dead

    end = pyperf.perf_counter()
    return end - begin

def bm_switch_deep(loops, _MAX_DEPTH=200):
    # pylint:disable=attribute-defined-outside-init
    class G(greenlet.greenlet):
        other = None
        def run(self):
            for _ in range(SWITCH_INNER_LOOPS):
                self.recur_then_switch()

        def recur_then_switch(self, depth=_MAX_DEPTH):
            if not depth:
                self.other.switch()
            else:
                self.recur_then_switch(depth - 1)

    begin = pyperf.perf_counter()

    for _ in range(loops):
        gl1 = G()
        gl2 = G()
        gl1.gr_frames_always_exposed = EXPOSE_FRAMES
        gl2.gr_frames_always_exposed = EXPOSE_FRAMES
        gl1.other = gl2
        gl2.other = gl1
        gl1.switch()

        gl1.switch()
        gl2.switch()
        gl1.other = gl2.other = None
        assert gl1.dead
        assert gl2.dead

    end = pyperf.perf_counter()
    return end - begin

def bm_switch_deeper(loops):
    return bm_switch_deep(loops, 400)


CREATE_INNER_LOOPS = 10
def bm_create(loops):
    gl = greenlet.greenlet
    begin = pyperf.perf_counter()
    for _ in range(loops):
        gl()
        gl()
        gl()
        gl()
        gl()
        gl()
        gl()
        gl()
        gl()
        gl()
    end = pyperf.perf_counter()
    return end - begin




def _bm_recur_frame(loops, RECUR_DEPTH):

    def recur(depth):
        if not depth:
            return greenlet.getcurrent().parent.switch(greenlet.getcurrent())
        return recur(depth - 1)


    begin = pyperf.perf_counter()
    for _ in range(loops):

        for _ in range(CHAIN_GREENLET_COUNT):
            g = greenlet.greenlet(recur)
            g.gr_frames_always_exposed = EXPOSE_FRAMES
            g2 = g.switch(RECUR_DEPTH)
            assert g2 is g, (g2, g)
            f = g2.gr_frame
            assert f is not None, "frame is none"
            count = 0
            while f:
                count += 1
                f = f.f_back
            # This assertion fails with the released versions of greenlet
            # on Python 3.12
            #assert count == RECUR_DEPTH + 1, (count, RECUR_DEPTH)
            # Switch back so it can be collected; otherwise they build
            # up forever.
            g.switch()
            # fall off the end of it and back to us.
            del g
            del g2
            del f


    end = pyperf.perf_counter()
    return end - begin

def bm_recur_frame_2(loops):
    return _bm_recur_frame(loops, 2)

def bm_recur_frame_20(loops):
    return _bm_recur_frame(loops, 20)

def bm_recur_frame_200(loops):
    return _bm_recur_frame(loops, 200)

if __name__ == '__main__':
    runner = pyperf.Runner()

    runner.bench_time_func(
        'create a greenlet',
        bm_create,
        inner_loops=CREATE_INNER_LOOPS
    )

    runner.bench_time_func(
        'switch between two greenlets (shallow)',
        bm_switch_shallow,
        inner_loops=SWITCH_INNER_LOOPS
    )

    runner.bench_time_func(
        'switch between two greenlets (deep)',
        bm_switch_deep,
        inner_loops=SWITCH_INNER_LOOPS
    )

    runner.bench_time_func(
        'switch between two greenlets (deeper)',
        bm_switch_deeper,
        inner_loops=SWITCH_INNER_LOOPS
    )
    runner.bench_time_func(
        'getcurrent single thread',
        bm_getcurrent,
        inner_loops=GETCURRENT_INNER_LOOPS
    )
    runner.bench_time_func(
        'chain(%s)' % CHAIN_GREENLET_COUNT,
        bm_chain,
    )

    runner.bench_time_func(
        'read 2 nested frames',
        bm_recur_frame_2,
    )

    runner.bench_time_func(
        'read 20 nested frames',
        bm_recur_frame_20,
    )
    runner.bench_time_func(
        'read 200 nested frames',
        bm_recur_frame_200,
    )
