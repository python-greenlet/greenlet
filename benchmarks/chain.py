#!/usr/bin/env python
"""
Create a chain of coroutines and pass a value from one end to the
other, where each coroutine will increment the value before passing it
along.
"""

import pyperf
import greenlet



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
def bm_switch(loops):
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
        gl1.other = gl2
        gl2.other = gl1
        gl1.switch()
    end = pyperf.perf_counter()
    return end - begin

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

if __name__ == '__main__':
    runner = pyperf.Runner()
    runner.bench_time_func(
        'create a greenlet',
        bm_create,
        inner_loops=CREATE_INNER_LOOPS
    )

    runner.bench_time_func(
        'switch between two greenlets',
        bm_switch,
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
