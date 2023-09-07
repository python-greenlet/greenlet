"""
Like fail_switch_three_greenlets, but the call into g1_run would actually be
valid.
"""
import greenlet

g1 = None
g2 = None

switch_to_g2 = False

def tracefunc(*args):
    print('TRACE', *args)
    global switch_to_g2
    if switch_to_g2:
        switch_to_g2 = False
        g2.switch()
    print('\tLEAVE TRACE', *args)

def g1_run():
    print('In g1_run')
    global switch_to_g2
    switch_to_g2 = True
    from_parent = greenlet.getcurrent().parent.switch()
    print('Return to g1_run')
    print('From parent', from_parent)

def g2_run():
    #g1.switch()
    greenlet.getcurrent().parent.switch()

greenlet.settrace(tracefunc)

g1 = greenlet.greenlet(g1_run)
g2 = greenlet.greenlet(g2_run)

g1.switch()
print('Back in main')
g1.switch(2)
