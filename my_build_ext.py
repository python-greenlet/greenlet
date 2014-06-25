# this code has been taken from gevent's setup.py file. it provides a
# build_ext command that puts .so/.pyd files in-place (like "setup.py
# build_ext -i"). it uses symlinks if possible and will rebuild when
# changing the python version (unlike "setup.py build_ext -i")

import sys, os, shutil

from distutils.command.build_ext import build_ext as _build_ext


def symlink_or_copy(src, dst):
    if hasattr(os, 'symlink'):
        try:
            os.symlink(src, dst)
            return
        except OSError:  # symbolic link privilege not held??
            pass
        except NotImplementedError:  # running on XP/'CreateSymbolicLinkW not found'
            pass

    shutil.copyfile(src, dst)


class build_ext(_build_ext):
    """Command for building extensions

    Prepends library directory to sys.path on normal builds (for tests).
    Otherwise it forces a non-inplace build and symlinks libraries instead.
    """

    def initialize_options(self):
        self.my_inplace = None
        _build_ext.initialize_options(self)

    def finalize_options(self):
        if self.my_inplace is None:
            self.my_inplace = self.inplace
            self.inplace = 0
        _build_ext.finalize_options(self)

    def build_extension(self, ext):
        _build_ext.build_extension(self, ext)
        if not self.my_inplace:
            build_lib = os.path.abspath(self.build_lib)
            if build_lib not in sys.path:
                sys.path.insert(0, build_lib)
            return
        filename = self.get_ext_filename(ext.name)
        build_path = os.path.abspath(os.path.join(self.build_lib, filename))
        src_path = os.path.abspath(filename)
        if build_path != src_path:
            try:
                os.unlink(src_path)
            except OSError:
                pass

            if self.verbose:
                sys.stderr.write('Linking %s to %s\n' % (build_path, src_path))

            symlink_or_copy(build_path, src_path)
