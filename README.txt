
This code is by Armin Rigo with the core pieces by Christian Tismer.
These pieces are:

- slp_platformselect.h
- switch_*.h, included from the previous one.

All additional credits for the general idea of stack switching also go to
Christian.  In other words, if it works it is thanks to Christian's work,
and if it crashes it is because of a bug of mine :-)

This code is going to be integrated in the 'py' lib.  The test file only runs
if you got greenlets from there!  See http://codespeak.net/py/ and try
'from py.magic import greenlet'.


-- Armin
