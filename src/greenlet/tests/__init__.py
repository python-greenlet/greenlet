# -*- coding: utf-8 -*-
"""
Tests for greenlet.

"""
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import sys

from gc import collect
from gc import get_objects
from threading import active_count as active_thread_count
from time import sleep
from time import time

from greenlet import greenlet as RawGreenlet
from greenlet import getcurrent

from greenlet._greenlet import get_pending_cleanup_count
from greenlet._greenlet import get_total_main_greenlets

class CleanupMixin(object):

    def wait_for_pending_cleanups(self,
                                  initial_active_threads=None,
                                  initial_main_greenlets=None):
        initial_active_threads = initial_active_threads or self.threads_before_test
        initial_main_greenlets = initial_main_greenlets or self.main_greenlets_before_test
        sleep_time = 0.001
        # NOTE: This is racy! A Python-level thread object may be dead
        # and gone, but the C thread may not yet have fired its
        # destructors and added to the queue. There's no particular
        # way to know that's about to happen. We try to watch the
        # Python threads to make sure they, at least, have gone away.
        # Counting the main greenlets, which we can easily do deterministically,
        # also helps.

        # Always sleep at least once to let other threads run
        sleep(sleep_time)
        quit_after = time() + 5
        # TODO: We could add an API that calls us back when a particular main greenlet is deleted?
        # It would have to drop the GIL
        while (
                get_pending_cleanup_count()
                or active_thread_count() > initial_active_threads
                or (not self.expect_greenlet_leak
                    and get_total_main_greenlets() > initial_main_greenlets)):
            sleep(sleep_time)
            if time() > quit_after:
                print("Time limit exceeded.")
                print("Threads: Waiting for only", initial_active_threads,
                      "-->", active_thread_count())
                print("MGlets : Waiting for only", initial_main_greenlets,
                      "-->", get_total_main_greenlets())
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

    greenlets_before_test = 0
    threads_before_test = 0
    main_greenlets_before_test = 0
    expect_greenlet_leak = False

    def count_greenlets(self):
        """
        Find all the greenlets and subclasses tracked by the GC.
        """
        return self.count_objects(RawGreenlet, False)

    def setUp(self):
        # Ensure the main greenlet exists, otherwise the first test
        # gets a false positive leak
        getcurrent()
        self.threads_before_test = active_thread_count()
        self.main_greenlets_before_test = get_total_main_greenlets()
        self.wait_for_pending_cleanups(self.threads_before_test, self.main_greenlets_before_test)
        self.greenlets_before_test = self.count_greenlets()

    def tearDown(self):
        self.wait_for_pending_cleanups(self.threads_before_test, self.main_greenlets_before_test)
        greenlets_after_test = self.count_greenlets()
        if self.expect_greenlet_leak:
            if greenlets_after_test <= self.greenlets_before_test:
                # Turn this into self.fail for debugging
                print("WARNING:"
                      "Expected to leak greenlets but did not; expected more than %d but found %d"
                      % (self.greenlets_before_test, greenlets_after_test),
                      file=sys.stderr
                )
        else:
            if greenlets_after_test > self.greenlets_before_test:
                self.fail(
                    "Leaked greenlets; expected no more than %d but found %d: %s"
                    % (self.greenlets_before_test, greenlets_after_test,
                       [x for x in get_objects() if isinstance(x, RawGreenlet)])
                )

Cleanup = CleanupMixin
