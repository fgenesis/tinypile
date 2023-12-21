
def aa(dx, dy):
    c = dx&dy&1
    a = (dx&1) + ((dx>>31)&1)
    b = ((dy&1) ^ ((dy>>31)&1)) << 1
    d = (~c)&1&(dx>>31)
    return (a ^ b) + d
    
def rr(dx, dy):
    a = (dx & 1) | (((dx >> 31) & 1) << 1)
    b = (dy & 1) | (((dy >> 31) & 1) << 1)
    one = (a ^ dy) >> 1
    both = dx&dy&1
    nneg = ((dx >> 31) & 1) + ((dy >> 31) & 1)
    
    print(a ^ (b << 1))
    
def r(dx, dy):
    one = (dx&dy&1) ^ 1
    a = dx + 1
    b = dy + 2
    #print(a ^ (b << 1) ^ (dy&1) ^ ((dx&1)<<1) ^ b)
    k = a+(b>>1)
    q = k << (one << 1)
    print(q)

r(0, -1)
r(0, 1)
r(-1, 0)
r(1, 0)
print()
r(-1, -1)
r(-1, 1)
r(1, -1)
r(1, 1)
