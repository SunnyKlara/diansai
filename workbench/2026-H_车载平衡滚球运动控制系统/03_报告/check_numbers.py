# -*- coding: utf-8 -*-
"""
check_numbers.py - recompute every derived number quoted in 设计报告.tex from
first principles and compare against what the report actually says.

WHY THIS EXISTS
    The report is dense with derived quantities (theta = K_p*e/K, lap time
    L/v + v/a, RSS error budgets, sampling-rate margins). Every time one input
    changes, several printed numbers must change with it. Doing that by hand
    silently rots: this script already caught two real defects on 2026-07-30 ---
      * the 18.4 mm budget figure was attributed to "camera fixed to the body"
        when that case alone is 17.1 mm; 18.4 mm is the comparison with a
        body-fixed camera and pitch compensation disabled while keeping the
        command-acceleration residual assumption unchanged;
      * 205 mm/s was left in the wheel-frequency formula after the verified
        cruise speed became 202.4 mm/s.

USAGE
    <repo>/.venv/Scripts/python.exe check_numbers.py
    exit 0 = every quoted number reproduces; exit 1 = at least one does not.

RULE OF USE
    If a check FAILS, do not "fix" this file to match the report. Recompute,
    decide which one is right, and fix the wrong one. The whole point is that
    the two are derived independently.

SCOPE NOTE (2026-07-31)
    Not every check below still corresponds to a number in 设计报告.tex. The
    formal report was cut down to the scoring rubric, so the lap-time model,
    the RSS error budget, the parallax model and the QVGA scale now live only
    in _设计报告_技术长版.tex (internal, not submitted). Those checks are kept
    because they still guard the derivations behind the design decisions.
    Where the two documents disagree, the FORMAL report wins: the trajectory
    timing here is the measured-authority version (1.6/2.0 s), while the long
    version still carries the superseded paper values (1.2/1.8 s).
"""
import math
import sys

# g = 9.81 throughout, matching the firmware constant
# BALL_K_MM_S2_PER_RAD = 7007.14 = (5/7)*9810. Using 9.807 here instead makes
# K come out 7005 and this script fails on the very first line -- which is how
# the report's one remaining g inconsistency was found on 2026-07-30.
G = 9.81e3           # mm/s^2
K = 5.0 / 7.0 * G    # mm/s^2 per rad, solid sphere rolling in a groove
A_LIM = 300.0        # mm/s^2, chassis longitudinal acceleration clamp
KP = 9.0             # 1/s^2
KD = 6.0             # 1/s
ENC_CPR = 954.75     # counts per output-shaft revolution
CPMM = 5.109         # counts per mm

fails = []
rows = []


def chk(name, claimed, computed, tol, unit=""):
    ok = abs(claimed - computed) <= tol
    rows.append((ok, name, claimed, computed, unit))
    if not ok:
        fails.append(name)


# ---- 2.1 rolling model -------------------------------------------------
chk("K = (5/7)g",                      7007.0,  K,                          1.0, "mm/s^2/rad")
th_max = math.degrees(math.asin(50.0 / 250.0))
chk("theta_max = asin(50/250)",        11.54,   th_max,                     0.01, "deg")
a_max = K * math.sin(math.radians(th_max))
chk("a_ball_max = K sin(theta_max)",   1401.0,  a_max,                      2.0, "mm/s^2")
chk("a_ball_max in g",                 0.143,   a_max / G,                  0.001, "g")

# ---- 1.1 track geometry ----------------------------------------------
L = (2 * 1.5 + 2 * math.pi * 0.5) * 1000.0
chk("lap length L = 3+pi (m->mm)",     6141.6,  L,                          0.5, "mm")
chk("curve share of lap",              51.2,    100 * (2 * math.pi * 0.5) / (3 + math.pi), 0.1, "%")

# ---- odometry scale ---------------------------------------------------
circ = ENC_CPR / CPMM
chk("wheel circumference",             186.9,   circ,                       0.1, "mm")
chk("1 RPM in mm/s",                   3.115,   circ / 60.0,                0.005, "mm/s")
v65 = 65.0 * circ / 60.0
chk("65 RPM",                          202.4,   v65,                        0.2, "mm/s")

# ---- 2.8 lap-time model  t(v) = L/v + v/a ----------------------------
def lap(v):
    return L / v + v / A_LIM


chk("t(202.4 mm/s)",                   31.0,    lap(v65),                   0.05, "s")
chk("t(234 mm/s)",                     27.0,    lap(234.0),                 0.05, "s")
# solve D/v + v/a = T -> v^2 - T*a*v + a*D = 0; take lower root
def min_cruise(distance_mm, limit_s):
    disc = (limit_s * A_LIM) ** 2 - 4 * A_LIM * distance_mm
    return (limit_s * A_LIM - math.sqrt(disc)) / 2


v_item2 = min_cruise(L, 20.0)
chk("v_min item2 for 20 s",            324.6,   v_item2,                    0.2, "mm/s")
chk("v_min item2 in RPM",             104.2,   v_item2 * 60.0 / circ,      0.1, "RPM")
v_item4 = min_cruise(1500.0, 8.0)
chk("v_min item4 for 8 s",             205.0,   v_item4,                    0.2, "mm/s")
chk("v_min item4 in RPM",              65.8,   v_item4 * 60.0 / circ,      0.1, "RPM")
v_min = min_cruise(L, 30.0)
chk("v_min for 30 s",                  209.6,   v_min,                      0.3, "mm/s")
chk("v_min in RPM",                    67.3,    v_min * 60.0 / circ,        0.1, "RPM")
chk("234 mm/s in RPM",                 75.0,    234.0 * 60.0 / circ,        0.3, "RPM")

# ---- 2.8 required *average* speeds (tab:speed) ------------------------
chk("req avg, item2 (20 s)",           307.1,   L / 20.0,                   0.1, "mm/s")
chk("req avg, item4 (1500 mm / 8 s)",  187.5,   1500.0 / 8.0,               0.1, "mm/s")
chk("req avg, item5/6 (30 s)",         204.7,   L / 30.0,                   0.1, "mm/s")
chk("req avg item5/6 in RPM",          65.8,    (L / 30.0) * 60.0 / circ,   0.1, "RPM")

# ---- square closure test wording -------------------------------------
chk("12 mm / 600 mm side",             2.0,     100.0 * 12.0 / 600.0,      0.01, "%")
chk("12 mm / 2400 mm perimeter",       0.5,     100.0 * 12.0 / (4 * 600.0), 0.01, "%")

# ---- 2.2 measured beam authority (real machine, 2026-07-31) -----------
# Sweep identification: fit a = K_tot * (us - us_center) from ball acceleration
# at several fixed pulse widths. Everything below derives from these four
# measured inputs, so a re-identification only needs these lines changed.
K_TOT = 1.622                    # mm/s^2 per us, magnitude
US_CENTER = 1086.0               # us, mechanical level position (zero crossing)
US_MIN, US_MAX = 960.0, 1320.0   # us, firmware clamp inside the 940..1340 travel
us_per_deg = K / K_TOT * math.pi / 180.0
chk("us per beam degree from K_tot",   75.4,    us_per_deg,                  0.1, "us/deg")
chk("geometry est 80 vs measured",     5.7,     100 * (80.0 - us_per_deg) / 80.0, 0.1, "%")
auth_lo = (US_CENTER - US_MIN) / us_per_deg
auth_hi = (US_MAX - US_CENTER) / us_per_deg
chk("weak-side authority",             1.67,    auth_lo,                     0.01, "deg")
chk("strong-side authority",           3.10,    auth_hi,                     0.01, "deg")
span_deg = (US_MAX - US_MIN) / us_per_deg
chk("full usable span",                4.77,    span_deg,                    0.02, "deg")
# single-point cross-check of eq:ball at us = 1000
dev_deg = (US_CENTER - 1000.0) / us_per_deg
chk("us=1000 offset from level",       1.14,    dev_deg,                     0.01, "deg")
a_model_1000 = K * math.sin(math.radians(dev_deg))
chk("model a at us=1000",              139.4,   a_model_1000,                0.5, "mm/s^2")
chk("measured/model at us=1000",       97.0,    100 * 135.1 / a_model_1000,  0.6, "%")

# ---- 2.3.2 authority budget ------------------------------------------
chk("PD angle at e=50 mm",             3.68,    math.degrees(KP * 50.0 / K), 0.02, "deg")
chk("a_x feedforward at 300 mm/s^2",   1.752,   math.degrees(A_LIM / G),     0.005, "deg")
# the weak side in turn bounds how much chassis acceleration is compensable
chk("a_veh ceiling from weak side",    286.0,   G * math.sin(math.radians(auth_lo)), 2.0, "mm/s^2")
# trajectory segments: |a| = 4A/T^2, so T is chosen FROM the authority
T_OUT, T_DWELL, T_BACK, T_SETTLE = 1.6, 0.2, 2.0, 1.0
a_out = 4 * 50.0 / T_OUT ** 2
chk("traj |a| segment 1",              78.1,    a_out,                       0.2, "mm/s^2")
chk("traj angle segment 1",            0.64,    math.degrees(a_out / K),     0.01, "deg")
a_back = 4 * 100.0 / T_BACK ** 2
chk("traj |a| segment 3",              100.0,   a_back,                      0.2, "mm/s^2")
chk("traj angle segment 3",            0.82,    math.degrees(a_back / K),    0.01, "deg")
chk("traj total time",                 4.8,     T_OUT + T_DWELL + T_BACK + T_SETTLE, 0.001, "s")
chk("PD headroom after ff",            0.85,    auth_lo - math.degrees(a_back / K), 0.02, "deg")
chk("PD headroom vs weak side",        51.0,
    100 * (auth_lo - math.degrees(a_back / K)) / auth_lo,                    1.0, "%")
# superseded paper-era budget: kept so nobody puts +-6 deg back into the report
chk("paper-era peak sum (superseded)", 6.57,    3.68 + 1.752 + 1.14,         0.02, "deg")
chk("paper budget vs real authority",  3.9,     6.57 / auth_lo,              0.05, "x")
chk("a_x compensable ceiling",         1961.0,  G * math.sin(math.radians(th_max)), 3.0, "mm/s^2")

# ---- 2.2 hinge-pivot centrifugal term and curve term -----------------
R_HINGE_BALL = 125.0 + 50.0                  # mm, hinge to ball at x = +50
th_dot_track = 3.0 * math.radians(0.8)       # rad/s = bandwidth x angle swing
chk("theta_dot while tracking",        0.042,   th_dot_track,                0.001, "rad/s")
cf_track = (5.0 / 7.0) * R_HINGE_BALL * th_dot_track ** 2
chk("centrifugal term, tracking",      0.2,     cf_track,                    0.05, "mm/s^2")
chk("centrifugal e_ss, tracking",      0.02,    cf_track / KP,               0.006, "mm")
BEAM_RATE = 300.0 / 8.34                     # deg/s at beam; servo 60 deg / 0.2 s
chk("beam slew rate",                  36.0,    BEAM_RATE,                   0.2, "deg/s")
cf_slew = (5.0 / 7.0) * R_HINGE_BALL * math.radians(BEAM_RATE) ** 2
chk("centrifugal term at full slew",   49.0,    cf_slew,                     1.0, "mm/s^2")
t_span = span_deg / BEAM_RATE
chk("time to slew full span",          0.13,    t_span,                      0.005, "s")
chk("displacement during full slew",   0.4,     0.5 * cf_slew * t_span ** 2, 0.05, "mm")
OMZ = v65 / 500.0
chk("yaw rate at cruise",              0.40,    OMZ,                         0.006, "rad/s")
curve_a = (5.0 / 7.0) * OMZ ** 2 * 75.0
chk("curve longitudinal term",         8.8,     curve_a,                     0.2, "mm/s^2")
chk("curve e_ss",                      1.0,     curve_a / KP,                0.05, "mm")

# ---- 2.2.3 pitch-induced steady-state error --------------------------
def e_pitch(deg):
    return K * math.sin(math.radians(deg)) / KP


chk("e_ss at pitch 0.5 deg",           6.79,    e_pitch(0.5),               0.02, "mm")
chk("e_ss at pitch 2 deg (test)",      27.2,    e_pitch(2.0),               0.1, "mm")
chk("wheel frequency at 202 mm/s",     1.08,    202.0 / circ,               0.01, "Hz")

# ---- 2.3.1 damping ---------------------------------------------------
chk("zeta at kd=6.0",                  1.0,     KD / (2 * math.sqrt(KP)),   0.001, "-")
chk("zeta at kd=4.8",                  0.8,     4.8 / (2 * math.sqrt(KP)),  0.001, "-")
chk("omega_n",                         3.0,     math.sqrt(KP),              0.001, "rad/s")

# ---- 2.3.3 sampling / delay margins ---------------------------------
wc = 6.186
chk("omega_c in Hz",                   0.985,   wc / (2 * math.pi),         0.002, "Hz")
need = 20 * wc / (2 * math.pi)
chk("20x crossover requirement",       19.7,    need,                       0.05, "Hz")
chk("camera margin (30 fps)",          1.52,    30.0 / need,                0.01, "-")
chk("control-loop margin (50 Hz)",     2.54,    50.0 / need,                0.01, "-")
chk("old 20 Hz margin",                1.02,    20.0 / need,                0.01, "-")
chk("delay margin PM/wc",              215.0,   math.radians(76.4) / wc * 1000, 2.0, "ms")
chk("one frame as share of margin",    15.0,    100 * 33.0 / 215.0,         0.5, "%")

# ---- 2.7 error budget (RSS) -----------------------------------------
QUANT, HILITE, CALIB, SERVO = 0.3, 1.0, 0.5, 1.6
PITCH_ON, PITCH_OFF = 1.4, 6.79
AX_RES = (5.0 / 7.0) * A_LIM * 0.10 / KP
PARALLAX_OFF = 16.8


def rss(*v):
    return math.sqrt(sum(x * x for x in v))


chk("a_x residual term",               2.4,  AX_RES,                        0.05, "mm")
chk("RSS, this design",                3.4,  rss(QUANT, HILITE, CALIB, 0.0, PITCH_ON, AX_RES, SERVO), 0.05, "mm")
chk("margin vs 10 mm",                 2.9,  10.0 / rss(QUANT, HILITE, CALIB, 0.0, PITCH_ON, AX_RES, SERVO), 0.05, "x")
chk("RSS, no pitch comp",              7.5,  rss(QUANT, HILITE, CALIB, 0.0, PITCH_OFF, AX_RES, SERVO), 0.05, "mm")
chk("margin, no pitch comp",           1.3,  10.0 / rss(QUANT, HILITE, CALIB, 0.0, PITCH_OFF, AX_RES, SERVO), 0.05, "x")
chk("RSS, camera on body only",        17.1, rss(QUANT, HILITE, CALIB, PARALLAX_OFF, PITCH_ON, AX_RES, SERVO), 0.1, "mm")
chk("RSS, camera fixed + no pitch comp", 18.4, rss(QUANT, HILITE, CALIB, PARALLAX_OFF, PITCH_OFF, AX_RES, SERVO), 0.1, "mm")

# ---- 1.4 parallax model ---------------------------------------------
def parallax(x_mm, theta_deg, H=200.0, hinge=-125.0):
    th = math.radians(theta_deg)
    return x_mm * math.cos(th) / (H - (x_mm - hinge) * math.sin(th)) * H - x_mm


chk("parallax at x=50 mm, 6 deg",      4.7,  parallax(50.0, 6.0),           0.2, "mm")
chk("parallax at x=120 mm, 6 deg",     16.8, parallax(120.0, 6.0),          0.3, "mm")

# ---- 1.3.2 look-ahead hard bound ------------------------------------
chk("look-ahead bound sqrt(2Rw)",      205.0, math.sqrt(2 * 500.0 * 42.0),  1.0, "mm")

# ---- 2.7 camera scale -----------------------------------------------
chk("QVGA scale 280 mm / 320 px",      0.875, 280.0 / 320.0,                0.001, "mm/px")
chk("ball diameter in px",             11.0,  10.0 / (280.0 / 320.0),       0.5, "px")

# ---- 1.3.1 UART feasibility ----------------------------------------
chk("byte time at 115200 baud (10b)",  87.0,  10.0 / 115200.0 * 1e6,        0.5, "us")
chk("4-byte FIFO poll deadline",       348.0, 4 * 10.0 / 115200.0 * 1e6,    2.0, "us")

# ---- 3.2.1 motor / current -----------------------------------------
chk("60% of 12 V bus",                 7.2,   12.0 * 0.60,                  0.01, "V")

# ---- report --------------------------------------------------------
print("=== check_numbers: report figures vs first-principles recomputation ===")
print("%-38s %12s %12s %s" % ("quantity", "in report", "recomputed", "unit"))
for ok, name, claimed, computed, unit in rows:
    print("%s %-36s %12.4g %12.4g  %s"
          % ("  ok " if ok else "  XX ", name, claimed, computed, unit))
print("checks: %d   failed: %d" % (len(rows), len(fails)))
if fails:
    print("RESULT: FAIL ->", ", ".join(fails))
    sys.exit(1)
print("RESULT: PASS")
