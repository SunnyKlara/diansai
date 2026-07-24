#!/usr/bin/env python3
"""Least-squares fit of true ball-bottom height vs ToF raw, from hand-held cal points."""
# (raw_cm, true_ball_bottom_cm) measured 2026-06-12, fan off, ball hand-held
pts = [(41.5,5),(36.8,10),(32.7,15),(26.1,20),(21.7,25),(16.6,30),(10.6,35),(8.0,40)]

def lsq(data):
    n=len(data); sx=sum(p[0] for p in data); sy=sum(p[1] for p in data)
    sxx=sum(p[0]**2 for p in data); sxy=sum(p[0]*p[1] for p in data)
    a=(n*sxy-sx*sy)/(n*sxx-sx*sx); b=(sy-a*sx)/n
    return a,b

def report(name,data):
    a,b=lsq(data)
    print(f"=== {name}: H = {a:.4f}*raw + {b:.3f}  (slope {a:.3f}) ===")
    print("  raw   true   fit   err")
    for raw,t in data:
        f=a*raw+b
        print(f"  {raw:5.1f} {t:5.1f} {f:6.2f} {f-t:+5.2f}")
    # also the simple slope=-1 constant K
    Ks=[raw+t for raw,t in data]
    print(f"  [slope=-1] mean K={sum(Ks)/len(Ks):.2f}  (H=K-raw)")

report("ALL 8 points", pts)
print()
report("OPERATING 5-30cm (drop 35,40 nonlinear top)", pts[:6])
