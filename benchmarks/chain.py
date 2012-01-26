#!/usr/bin/env python

"""Create a chain of coroutines and pass a value from one end to the other,
where each coroutine will increment the value before passing it along.
"""

import optparse
import time

import greenlet


def link(next_greenlet):
    value = greenlet.getcurrent().parent.switch()
    while True:
        next_greenlet.switch(value + 1)


def chain(n):
    start_node = greenlet.getcurrent()
    for i in xrange(n):
        g = greenlet.greenlet(link)
        g.switch(start_node)
        start_node = g
    return start_node.switch(0)

if __name__ == '__main__':
    p = optparse.OptionParser(
        usage='%prog [-n NUM_COROUTINES]', description=__doc__)
    p.add_option(
        '-n', type='int', dest='num_greenlets', default=100000,
        help='The number of greenlets in the chain.')
    options, args = p.parse_args()

    if len(args) != 0:
        p.error('unexpected arguments: %s' % ', '.join(args))

    start_time = time.clock()
    print 'Result:', chain(options.num_greenlets)
    print time.clock() - start_time, 'seconds'
