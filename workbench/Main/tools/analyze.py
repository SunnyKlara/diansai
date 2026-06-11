#!/usr/bin/env python3
"""analyze.py - robust offline analyzer for ball-levitation telemetry CSVs.

Why this exists: piping live serial heartbeats to the terminal gets mangled by
cmd line-wrapping, so reading metrics off stdout is unreliable. The capture
scripts already write a clean CSV to tools/logs/. This tool reads that CSV and
writes a clean, structured summary file (<csv>.summary.txt) that the agent reads
back with read_file -- no terminal garbling in the loop.

Usage:
  python analyze.py                      # analyze newest CSV in tools/logs/
  python analyze.py path\to\run.csv      # analyze a specific CSV
  python analyze.py run.csv --target 15  # set band target (else inferred)
  python analyze.py run.csv --tail 0.5   # steady-state = last 50% of samples

Column names are normalized, so it works with both tune_step CSVs
(t_ms,H_cm,P,D_cms,RPM,RAW_cm,age_ms) and sysid CSVs (t_ms,H,P,D).

Stdlib only (no numpy) so it runs in any Python.
"""
import sys, os, csv, math, re, glob, argparse

# canonical name -> accepted aliases (case-insensitive)
ALIASES = {
    "t_ms": ["t_ms", "ms", "time_ms", "t"],
    "H":    ["h", "h_cm", "height", "height_cm"],
    "T":    ["t_cm", "target", "target_cm", "tgt"],
    "E":    ["e", "err", "error"],
    "P":    ["p", "pwm", "p_pwm"],
    "D":    ["d", "d_cms", "vel", "velocity"],
    "R":    ["r", "rpm"],
    "RAW":  ["raw", "raw_cm", "raw_mm"],
    "A":    ["a", "age", "age_ms"],
    "RS":   ["rs", "rpm_sp", "rpm_setpoint"],
}

def _norm_header(fields):
    """map raw csv headers to canonical names; return {canon: index}."""
    idx = {}
    low = [f.strip().lower() for f in fields]
    for canon, al in ALIASES.items():
        for a in al:
            if a in low:
                idx[canon] = low.index(a)
                break
    return idx

def _stats(xs):
    n = len(xs)
    if n == 0:
        return dict(n=0, avg=0, std=0, min=0, max=0, ptp=0)
    avg = sum(xs) / n
    var = sum((x - avg) ** 2 for x in xs) / n
    lo, hi = min(xs), max(xs)
    return dict(n=n, avg=avg, std=math.sqrt(var), min=lo, max=hi, ptp=hi - lo)

def _linfit_slope(t_s, y):
    """least-squares slope dy/dt (units of y per second)."""
    m = len(y)
    if m < 2:
        return 0.0
    sx = sum(t_s); sy = sum(y)
    sxx = sum(x * x for x in t_s); sxy = sum(t_s[i] * y[i] for i in range(m))
    den = m * sxx - sx * sx
    return (m * sxy - sx * sy) / den if abs(den) > 1e-12 else 0.0

def _zero_cross_period(y, dur_s):
    """detrended zero-crossing period estimate."""
    m = len(y)
    if m < 4 or dur_s <= 0:
        return None, 0
    avg = sum(y) / m
    zc = 0
    prev = y[0] - avg
    for i in range(1, m):
        cur = y[i] - avg
        if (prev <= 0 and cur > 0) or (prev >= 0 and cur < 0):
            zc += 1
        prev = cur
    if zc < 2:
        return None, zc
    return 2.0 * dur_s / zc, zc

def _dominant_period(t_ms, y):
    """crude DFT scan over physical periods 0.3..8s; return dominant period (s).
    Non-uniform sampling tolerated via direct sin/cos correlation."""
    m = len(y)
    if m < 8:
        return None
    avg = sum(y) / m
    yc = [v - avg for v in y]
    t_s = [t / 1000.0 for t in t_ms]
    best_p, best_pow = None, 0.0
    p = 0.3
    while p <= 8.0:
        w = 2 * math.pi / p
        re_ = sum(yc[i] * math.cos(w * t_s[i]) for i in range(m))
        im_ = sum(yc[i] * math.sin(w * t_s[i]) for i in range(m))
        power = (re_ * re_ + im_ * im_)
        if power > best_pow:
            best_pow, best_p = power, p
        p += 0.05
    return best_p

def _band_pct(xs, center, half):
    if not xs:
        return 0.0
    inb = sum(1 for x in xs if abs(x - center) <= half)
    return 100.0 * inb / len(xs)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv", nargs="?", default=None)
    ap.add_argument("--target", type=float, default=None,
                    help="band center (cm); inferred from filename t<N> or T column if omitted")
    ap.add_argument("--tail", type=float, default=0.5,
                    help="steady-state fraction from the end (default 0.5)")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    logdir = os.path.join(here, "logs")
    path = args.csv
    if path is None:
        cands = glob.glob(os.path.join(logdir, "*.csv"))
        if not cands:
            print("no CSV found in", logdir); return 1
        path = max(cands, key=os.path.getmtime)
    if not os.path.isabs(path) and not os.path.exists(path):
        alt = os.path.join(logdir, path)
        if os.path.exists(alt):
            path = alt
    if not os.path.exists(path):
        print("CSV not found:", path); return 1

    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    if len(rows) < 2:
        print("empty CSV:", path); return 1
    idx = _norm_header(rows[0])
    if "H" not in idx or "t_ms" not in idx:
        print("CSV missing H/t_ms columns; header:", rows[0]); return 1

    cols = {k: [] for k in idx}
    for r in rows[1:]:
        if len(r) <= max(idx.values()):
            continue
        ok = True
        vals = {}
        for k, j in idx.items():
            try:
                vals[k] = float(r[j])
            except ValueError:
                ok = False; break
        if ok:
            for k in idx:
                cols[k].append(vals[k])

    t = cols["t_ms"]; H = cols["H"]
    n = len(H)
    if n < 8:
        print(f"too few samples ({n}) in {path}"); return 1

    dur = (t[-1] - t[0]) / 1000.0 if n > 1 else 0.0
    fps = (n - 1) / dur if dur > 0 else 0.0

    # target inference
    target = args.target
    if target is None and "T" in cols and cols["T"]:
        target = sum(cols["T"]) / len(cols["T"])
    if target is None:
        mfn = re.search(r"[_/\\]t(\d+(?:\.\d+)?)", os.path.basename(path), re.I)
        if mfn:
            target = float(mfn.group(1))
        else:
            mfn = re.search(r"(\d+)to(\d+)", os.path.basename(path))
            if mfn:
                target = float(mfn.group(2))  # end of a step
    # tail window
    k0 = int(n * (1.0 - args.tail))
    if k0 < 0: k0 = 0
    tailH = H[k0:]; tailT = t[k0:]
    tailHs = _stats(tailH)
    tail_dur = (tailT[-1] - tailT[0]) / 1000.0 if len(tailT) > 1 else 0.0

    out = []
    w = out.append
    w("=" * 60)
    w(f"ANALYZE  {os.path.basename(path)}")
    w("=" * 60)
    w(f"samples={n}  duration={dur:.1f}s  fps={fps:.1f}  tail={int(args.tail*100)}% ({tailHs['n']} samp, {tail_dur:.1f}s)")
    if target is not None:
        w(f"target={target:.2f} cm")

    # feedback health
    if "A" in cols and cols["A"]:
        amax = max(cols["A"]); stall = sum(1 for a in cols["A"] if a > 150)
        pct = 100.0 * stall / len(cols["A"])
        verdict = "*** FEEDBACK STALLING - fix sensor before trusting tune ***" if amax > 200 else "feedback OK"
        w(f"feedback: maxAge={amax:.0f}ms  stalled(>150ms)={stall}/{len(cols['A'])} ({pct:.0f}%)  -> {verdict}")

    # H steady-state
    errs = f"  err={tailHs['avg']-target:+.2f}" if target is not None else ""
    w(f"tail H: avg={tailHs['avg']:.2f} std={tailHs['std']:.2f} min={tailHs['min']:.1f} max={tailHs['max']:.1f} ptp={tailHs['ptp']:.1f}{errs}")

    # the actual scoring criterion: time-in-band
    if target is not None:
        b1 = _band_pct(tailH, target, 1.0)
        b2 = _band_pct(tailH, target, 2.0)
        w(f"BAND (tail): within +/-1.0cm = {b1:.0f}%   within +/-2.0cm = {b2:.0f}%")
        grade1 = "PASS" if b1 >= 95 else ("close" if b1 >= 80 else "no")
        grade2 = "PASS" if b2 >= 95 else ("close" if b2 >= 80 else "no")
        w(f"   +/-1cm: {grade1}    +/-2cm: {grade2}   (PASS=>=95% in band)")

    # D-term (ball velocity) noise diagnosis
    if "D" in cols and cols["D"]:
        tailD = cols["D"][k0:]
        ds = _stats(tailD)
        w(f"tail D(vel cm/s): avg={ds['avg']:.2f} std={ds['std']:.2f} ptp={ds['ptp']:.1f}")
        if ds["std"] > 5.0 and tailHs["std"] < 2.0:
            w(f"   -> D NOISE-DOMINATED (vel std {ds['std']:.1f} while H std {tailHs['std']:.2f}): filter velocity (raise 'f'/observer) before adding Kd")

    # drift
    t_s_tail = [x / 1000.0 for x in tailT]
    slope = _linfit_slope(t_s_tail, tailH)
    w(f"drift: slope={slope:+.3f} cm/s  (over {tail_dur:.1f}s tail => {slope*tail_dur:+.2f} cm)")

    # oscillation
    zp, zc = _zero_cross_period(tailH, tail_dur)
    dp = _dominant_period(tailT, tailH)
    if zp:
        w(f"oscillation: zero-cross ~{zp:.2f}s  amp~{tailHs['std']*1.414:.2f}cm  (zc={zc}); FFT-dominant ~{dp:.2f}s")
    else:
        w(f"oscillation: none/flat (good); FFT-dominant ~{dp:.2f}s" if dp else "oscillation: none/flat (good)")

    # RPM (cascade lever)
    if "R" in cols and cols["R"] and max(cols["R"]) > 0:
        tailR = cols["R"][k0:]
        rs = _stats(tailR)
        w(f"RPM tail: avg={rs['avg']:.0f} std={rs['std']:.0f} min={rs['min']:.0f} max={rs['max']:.0f}")
        if "RS" in cols and cols["RS"] and max(cols["RS"]) > 0:
            tailRS = cols["RS"][k0:]
            track = [tailRS[i] - tailR[i] for i in range(min(len(tailRS), len(tailR)))]
            ts = _stats([abs(x) for x in track])
            w(f"RPM track err |sp-meas|: avg={ts['avg']:.0f} max={ts['max']:.0f}  (inner-loop tightness)")
    elif "R" in cols:
        w("RPM tail: tach reads 0 (check PC6 before trusting cascade)")

    # ranges
    ps = _stats(cols["P"]) if "P" in cols and cols["P"] else None
    if ps:
        w(f"P(PWM) range: {ps['min']:.0f}..{ps['max']:.0f}")

    # headline verdict
    w("-" * 60)
    if target is not None:
        b1 = _band_pct(tailH, target, 1.0)
        head = f"VERDICT: std={tailHs['std']:.2f}cm  in+/-1cm={b1:.0f}%  drift={slope:+.2f}cm/s"
        w(head)

    summary = "\n".join(out)
    sumpath = path + ".summary.txt"
    with open(sumpath, "w", encoding="utf-8") as f:
        f.write(summary + "\n")
    # also write a fixed-name copy so the latest result is always at a known path
    try:
        latest = os.path.join(logdir, "_latest.summary.txt")
        with open(latest, "w", encoding="utf-8") as f:
            f.write(f"(source: {os.path.basename(path)})\n" + summary + "\n")
    except OSError:
        pass
    # short headline to stdout (kept tiny to avoid terminal wrap garbling)
    print(f"OK -> {sumpath}")
    if target is not None:
        print(f"std={tailHs['std']:.2f} in1cm={_band_pct(tailH,target,1.0):.0f}% in2cm={_band_pct(tailH,target,2.0):.0f}% drift={slope:+.2f}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
