import os
import glob
import unittest

def test_collector():
    """Collect all tests under the tests directory and return a
    unittest.TestSuite
    """
    tests_dir = os.path.realpath(os.path.dirname(__file__))
    test_module_list = [
        'tests.%s' % os.path.splitext(os.path.basename(t))[0]
        for t in glob.glob(os.path.join(tests_dir, 'test_*.py'))
    ]
    return unittest.TestLoader().loadTestsFromNames(test_module_list)
