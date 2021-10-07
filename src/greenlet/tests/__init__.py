# -*- coding: utf-8 -*-
"""
Tests for greenlet.

"""
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

from gc import collect
from gc import get_objects

class Cleanup(object):

    def wait_for_pending_cleanups(self):
        from time import sleep
        from time import time
        from greenlet._greenlet import get_pending_cleanup_count
        sleep_time = 0.001
        # Always sleep at least once to let other threads run
        sleep(sleep_time)
        quit_after = time() + 10
        while get_pending_cleanup_count():
            sleep(sleep_time)
            if time() > quit_after:
                break
        collect()

    def count_objects(self, kind=list, exact_kind=True):
        # pylint:disable=unidiomatic-typecheck
        # Collect the garbage.
        for _ in range(3):
            collect()
        if exact_kind:
            return sum(
                1
                for x in get_objects()
                if type(x) is kind
            )
        # instances
        return sum(
            1
            for x in get_objects()
            if isinstance(x, kind)
        )

    # TODO: Ensure we don't leak greenlets, everything gets GC'd.
    greenlets_before = 0

    def setUp(self):
        self.wait_for_pending_cleanups()

    def tearDown(self):
        self.wait_for_pending_cleanups()
