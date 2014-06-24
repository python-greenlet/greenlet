import sys, os
from distutils.command.build_ext import build_ext


class build_test_ext(build_ext):
    def build_extension(self, ext):
        self.inplace = 0
        build_ext.build_extension(self, ext)
        build_lib = os.path.abspath(self.build_lib)
        if build_lib not in sys.path:
            if self.verbose:
                sys.stderr.write('Adding %s to sys.path\n' % (build_lib,))
            sys.path.insert(0, build_lib)
