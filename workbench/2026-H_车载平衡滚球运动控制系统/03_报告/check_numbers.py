# -*- coding: utf-8 -*-
"""Recompute every quantitative claim used by the formal 2026-H report.

The gate intentionally uses only three traceable sources:
  1. first-principles geometry and ball dynamics;
  2. current firmware macros in config.h;
  3. raw static-ball CSV logs quoted by the report.

It also checks that the rendered TeX body still contains the complete
differential-drive, pure-rolling, vehicle-coupled and feedforward equations.
Practice-track logs and extrapolated competition results are deliberately out
of scope. Exit 0 means all values and required model terms reproduce.
"""

from __future__ import annotations

import csv
import math
import re
import statistics
import sys
from pathlib import Path

REPORT_DIR = Path(__file__).resolve().parent
WORKBENCH_DIR = REPORT_DIR.parents[1]
CAR_DIR = WORKBENCH_DIR / "mspm0" / "car"
REPORT = REPORT_DIR / "设计报告.tex"
CONFIG = CAR_DIR / "config.h"
BALL_DIR = CAR_DIR / "_logs" / "ball"

G_MM_S2 = 9.81e3
BALL_K = 5.0 / 7.0 * G_MM_S2
K_TOTAL = 1.622
TRACK_LENGTH_MM = (2.0 * 1.5 + 2.0 * math.pi * 0.5) * 1000.0

fails: list[str] = []
numeric_rows: list[tuple[bool, str, float, float, str]] = []
text_rows: list[tuple[bool, str]] = []


def chk(name: str, claimed: float, computed: float, tol: float, unit: str = "") -> None:
    ok = abs(claimed - computed) <= tol
    numeric_rows.append((ok, name, claimed, computed, unit))
    if not ok:
        fails.append(name)


def read_macro(name: str) -> float:
    text = CONFIG.read_text(encoding="utf-8")
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+\(?\s*([+-]?\d+(?:\.\d+)?)\s*[fFuUlL]*\s*\)?",
        text,
        flags=re.MULTILINE,
    )
    if not match:
        raise RuntimeError(f"cannot parse {name} from {CONFIG}")
    return float(match.group(1))


def read_ball(path: Path, limit: int | None = None) -> list[dict[str, str]]:
    rows_out: list[dict[str, str]] = []
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            if int(row["seen"]) == 1:
                rows_out.append(row)
            if limit is not None and len(rows_out) == limit:
                break
    return rows_out


def ball_metrics(rows_in: list[dict[str, str]]) -> tuple[float, float, float, float, int]:
    values = [float(row["x_mm"]) for row in rows_in]
    return (
        statistics.mean(values),
        statistics.stdev(values),
        max(abs(value) for value in values),
        math.sqrt(sum(value * value for value in values) / len(values)),
        sum(abs(value) > 10.0 for value in values),
    )


def strip_tex_comments(text: str) -> str:
    return "\n".join(re.sub(r"(?<!\\)%.*$", "", line) for line in text.splitlines())


report_compact = re.sub(r"\s+", "", strip_tex_comments(REPORT.read_text(encoding="utf-8")))


def require_tex(name: str, *fragments: str) -> None:
    ok = all(fragment in report_compact for fragment in fragments)
    text_rows.append((ok, name))
    if not ok:
        fails.append(f"report {name}")


def forbid_tex(name: str, *fragments: str) -> None:
    ok = all(fragment not in report_compact for fragment in fragments)
    text_rows.append((ok, name))
    if not ok:
        fails.append(f"report {name}")


# ---- Firmware values quoted by the report -----------------------------
for macro, claimed, tol, unit in (
    ("CFG_SERVO_CENTER_US", 1154.0, 0.01, "us"),
    ("CFG_SERVO_MIN_US", 960.0, 0.01, "us"),
    ("CFG_SERVO_MAX_US", 1320.0, 0.01, "us"),
    ("CFG_SERVO_US_PER_DEG", 75.4, 0.01, "us/deg"),
    ("CFG_BALL_KP", 5.0, 0.001, "1/s^2"),
    ("CFG_BALL_KD", 2.0, 0.001, "1/s"),
    ("CFG_BALL_KI", 0.0, 0.001, "-"),
    ("CFG_BALL_FRIC_DEG", 0.0, 0.001, "deg"),
    ("CFG_BALL_THETA_MAX", 3.0, 0.001, "deg"),
    ("CFG_BALL_ALPHA", 0.8, 0.001, "-"),
    ("CFG_BALL_BETA", 0.5333, 0.0001, "-"),
    ("CFG_BALL_FF_AX", 1.0, 0.001, "bool"),
    ("CFG_BALL_FF_PITCH", 0.0, 0.001, "bool"),
    ("CFG_KP_LINE", 2.6, 0.001, "-"),
    ("CFG_KD_LINE", 0.2, 0.001, "-"),
    ("CFG_LINE_W_MAX", 120.0, 0.001, "RPM"),
):
    chk(f"config {macro}", claimed, read_macro(macro), tol, unit)

# ---- First-principles and task-constraint quantities ------------------
chk("ball K=(5/7)g", 7007.0, BALL_K, 1.0, "mm/s^2/rad")
chk("standard track length", 6141.6, TRACK_LENGTH_MM, 0.1, "mm")
chk("20 s minimum average speed", 307.1, TRACK_LENGTH_MM / 20.0, 0.1, "mm/s")
chk("30 s minimum average speed", 204.7, TRACK_LENGTH_MM / 30.0, 0.1, "mm/s")
chk("60 percent of 12 V", 7.2, 12.0 * 0.60, 0.01, "V")

us_per_deg = BALL_K / K_TOTAL * math.pi / 180.0
chk("us per beam degree", 75.4, us_per_deg, 0.1, "us/deg")
chk("lower-pulse span", 194.0, 1154.0 - 960.0, 0.01, "us")
chk("upper-pulse span", 166.0, 1320.0 - 1154.0, 0.01, "us")
chk("lower-pulse authority", 2.57, (1154.0 - 960.0) / us_per_deg, 0.01, "deg")
chk("upper-pulse authority", 2.20, (1320.0 - 1154.0) / us_per_deg, 0.01, "deg")
chk(
    "fixed-view height change",
    6.7,
    (125.0 + 50.0) * math.sin(math.radians(2.20)),
    0.1,
    "mm",
)

a_out = 4.0 * 50.0 / 1.6**2
a_back = 4.0 * 100.0 / 2.0**2
chk("trajectory outbound acceleration", 78.1, a_out, 0.1, "mm/s^2")
chk("trajectory return acceleration", 100.0, a_back, 0.1, "mm/s^2")
chk("trajectory outbound angle", 0.64, math.degrees(a_out / BALL_K), 0.01, "deg")
chk("trajectory return angle", 0.82, math.degrees(a_back / BALL_K), 0.01, "deg")
chk("trajectory total time", 4.8, 1.6 + 0.2 + 2.0 + 1.0, 0.001, "s")

# ---- Static ball-position evidence ------------------------------------
old_rows = read_ball(BALL_DIR / "kd2_211636.csv", limit=85)
new_rows = read_ball(BALL_DIR / "c1154_211911.csv", limit=85)
if len(old_rows) != 85 or len(new_rows) != 85:
    raise RuntimeError("static A/B logs do not contain the required 85 valid samples")
old_mean, old_std, old_peak, _, old_out = ball_metrics(old_rows)
new_mean, new_std, new_peak, _, new_out = ball_metrics(new_rows)
for name, claimed, computed, tol, unit in (
    ("1172 mean", -8.553, old_mean, 0.001, "mm"),
    ("1172 std", 1.753, old_std, 0.001, "mm"),
    ("1172 peak", 13.3, old_peak, 0.01, "mm"),
    ("1172 outside", 19.0, float(old_out), 0.0, "samples"),
    ("1154 mean", -2.661, new_mean, 0.001, "mm"),
    ("1154 std", 0.486, new_std, 0.001, "mm"),
    ("1154 peak", 3.4, new_peak, 0.01, "mm"),
    ("1154 outside", 0.0, float(new_out), 0.0, "samples"),
):
    chk(name, claimed, computed, tol, unit)

final_rows = read_ball(BALL_DIR / "c1154_211911.csv")
_, final_std, _, final_rms, final_out = ball_metrics(final_rows)
final_values = [float(row["x_mm"]) for row in final_rows]
final_servo = [int(row["servo_us"]) for row in final_rows]
chk("final sample count", 259.0, float(len(final_rows)), 0.0, "samples")
chk("final minimum", -3.4, min(final_values), 0.01, "mm")
chk("final maximum", -2.3, max(final_values), 0.01, "mm")
chk("final RMS", 2.8, final_rms, 0.05, "mm")
chk("final std", 0.5, final_std, 0.05, "mm")
chk("final outside", 0.0, float(final_out), 0.0, "samples")
chk("final servo minimum", 1122.0, float(min(final_servo)), 0.0, "us")
chk("final servo maximum", 1174.0, float(max(final_servo)), 0.0, "us")

# ---- Required report equations and quoted evidence --------------------
require_tex(
    "differential-drive kinematics",
    r"v=\frac{v_L+v_R}{2}",
    r"\omega_z=\frac{v_R-v_L}{B}",
    r"\dotX=v\cos\psi",
    r"\dotY=v\sin\psi",
    r"\dot\psi=\omega_z",
    r"v_L^*=v_c-\frac{B}{2}\omega_c",
    r"v_R^*=v_c+\frac{B}{2}\omega_c",
)
require_tex(
    "eight-sensor centroid control",
    r"e_l=\frac{\sum_{i=1}^{8}p_iq_i}{\sum_{i=1}^{8}q_i}",
    r"K_{pl}e_l+K_{dl}\frac{\Deltae_l}{\Deltat}",
)
require_tex(
    "pure-rolling derivation",
    r"m\ddotx&=mg\sin\theta-f",
    r"I\dot\omega&=fR_b",
    r"I=\frac{2}{5}mR_b^2",
    r"R_b\dot\omega=\ddotx",
    r"\ddotx=\frac{5}{7}g\sin\theta=K\sin\theta",
)
require_tex(
    "vehicle-coupled ball model",
    r"g\sin(\theta_b+\varphi)",
    r"-a_{\mathrm{veh}}\cos(\theta_b+\varphi)",
    r"+r\dot\theta_b^{2}",
    r"-\omega_z^{2}\ell_p",
)
require_tex(
    "complete feedforward inversion",
    r"\theta_{b,\mathrm{ff}}=\frac{a_d}{K}+\frac{a_{\mathrm{veh}}}{g}-\varphi",
    r"+\frac{\omega_z^2\ell_p-r\dot\theta_b^2}{g}",
)
require_tex(
    "static A/B table",
    r"1172\us&85&$-8.553$&1.753&13.3&19",
    r"1154\us&85&$-2.661$&0.486&3.4&0",
)
require_tex(
    "extended static result",
    r"259个有效样本",
    r"$-3.4\sim-2.3\unit{mm}$",
    r"$1122\sim1174\us$",
)
forbid_tex(
    "practice-track claims absent",
    "练习赛道",
    r"65\unit{RPM}",
    r"85\unit{RPM}",
    r"105\unit{RPM}",
    r"259\unit{mm/s}",
    r"23.7\unit{s}",
)

# ---- Report ------------------------------------------------------------
print("=== check_numbers: formal report claims vs source evidence ===")
print("%-43s %12s %12s %s" % ("quantity", "in report", "recomputed", "unit"))
for ok, name, claimed, computed, unit in numeric_rows:
    print(
        "%s %-41s %12.4g %12.4g  %s"
        % ("  ok " if ok else "  XX ", name, claimed, computed, unit)
    )
print("\nrequired TeX content:")
for ok, name in text_rows:
    print(f"{'  ok ' if ok else '  XX '} {name}")
print(f"checks: {len(numeric_rows) + len(text_rows)}   failed: {len(fails)}")
if fails:
    print("RESULT: FAIL ->", ", ".join(fails))
    sys.exit(1)
print("RESULT: PASS")
