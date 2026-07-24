# -*- coding: utf-8 -*-
r"""
Render real measured CSV logs into a polished, unified figure suite for the
design report. Output: 03_报告/figs/*.pdf (vector, embedded via includegraphics)

Run:  python plot_report_figs.py
Deps: matplotlib, numpy
"""
import os, glob, csv
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager as fm

# ---------------- unified house style ----------------
for name in ["Microsoft YaHei", "SimHei", "SimSun"]:
    try:
        fm.findfont(name, fallback_to_default=False)
        plt.rcParams["font.sans-serif"] = [name]
        break
    except Exception:
        pass
plt.rcParams.update({
    "axes.unicode_minus": False,
    "figure.dpi": 130,
    "savefig.dpi": 200,
    "savefig.bbox": "tight",
    "font.size": 11,
    "axes.titlesize": 12,
    "axes.titleweight": "bold",
    "axes.labelsize": 10.5,
    "axes.edgecolor": "#444444",
    "axes.linewidth": 0.8,
    "axes.grid": True,
    "grid.color": "#cfcfcf",
    "grid.linestyle": ":",
    "grid.linewidth": 0.7,
    "grid.alpha": 0.7,
    "legend.fontsize": 8.5,
    "legend.framealpha": 0.92,
    "legend.edgecolor": "#bbbbbb",
    "xtick.labelsize": 9,
    "ytick.labelsize": 9,
})

# palette
C_ACT = "#2E7D32"   # actual height (green)
C_TGT = "#C62828"   # target (red)
C_PWM = "#1565C0"   # pwm (blue)
C_VEL = "#6A1B9A"   # velocity (purple)
C_RPM = "#00838F"   # rpm (teal)
C_ACC = "#EF6C00"   # accent (orange)
B_2CM = "#FFE0B2"   # +/-2cm band
B_1CM = "#C8E6C9"   # +/-1cm band

HERE = os.path.dirname(os.path.abspath(__file__))
LOGS = os.path.normpath(os.path.join(HERE, "..", "..", "Main", "tools", "logs"))
FIGS = os.path.join(HERE, "figs")
os.makedirs(FIGS, exist_ok=True)

# representative runs (verified by _analyze.py, same gain set ~20:48-20:50)
F_HOLD = {10: "run_t10_20260611_205044.csv",
          15: "run_t15_20260611_204952.csv",
          20: "run_t20_20260611_204855.csv"}
F_TRACK = "sysid_step_10to20_20260611_205622.csv"
F_FAN = "sysid_fan_4000_20260611_200356.csv"
F_IDSTEP = "sysid_step_15to18_20260611_202248.csv"

TAIL = 0.40  # steady-state window = last 40%


def load(name):
    path = os.path.join(LOGS, name)
    if not os.path.exists(path):
        g = glob.glob(os.path.join(LOGS, name.split("_2026")[0] + "*.csv"))
        path = sorted(g)[-1]
    rows = []
    with open(path, "r", encoding="utf-8-sig", errors="ignore") as f:
        rd = csv.reader(f); header = next(rd)
        for r in rd:
            if len(r) < len(header):
                continue
            try:
                rows.append([float(x) for x in r[:len(header)]])
            except ValueError:
                continue
    a = np.array(rows, float)
    d = {h.strip(): a[:, i] for i, h in enumerate(header)}
    if "t_ms" in d:
        d["t"] = (d["t_ms"] - d["t_ms"][0]) / 1000.0
    return d


def steady(h):
    seg = h[int(len(h) * (1 - TAIL)):]
    return seg.mean(), seg.std(ddof=0), seg.max() - seg.min(), seg


def band(ax, c, lo, hi, label=None):
    ax.axhspan(lo, hi, color=c, alpha=0.55, lw=0, label=label, zorder=0)


def finish(fig, out):
    p = os.path.join(FIGS, out)
    fig.savefig(p)
    plt.close(fig)
    print("[ok]", out)


# ============ F1: three-height hold time series ============
def fig_hold():
    fig, axes = plt.subplots(3, 1, figsize=(7.4, 6.6), constrained_layout=True)
    stats = {}
    for ax, tgt in zip(axes, [10, 15, 20]):
        d = load(F_HOLD[tgt]); t = d["t"]; h = d["H_cm"]
        avg, std, ptp, seg = steady(h)
        stats[tgt] = (avg, std, ptp)
        band(ax, B_2CM, tgt - 2, tgt + 2, "±2 cm")
        band(ax, B_1CM, tgt - 1, tgt + 1, "±1 cm")
        ax.axhline(tgt, color=C_TGT, lw=1.3, ls="--", label="目标 %d cm" % tgt, zorder=3)
        ax.plot(t, h, color=C_ACT, lw=1.0, zorder=4, label="实际高度")
        ax.set_ylim(tgt - 5.5, tgt + 5.5)
        ax.set_xlim(0, t[-1])
        ax.set_ylabel("高度 / cm")
        ax.margins(x=0)
        ax.text(0.985, 0.06,
                r"$\bar h$=%.2f  $\sigma$=%.2f cm  峰峰=%.1f cm" % (avg, std, ptp),
                transform=ax.transAxes, ha="right", va="bottom", fontsize=8.5,
                bbox=dict(fc="white", ec="#999", alpha=0.9, pad=2.5))
        ax.set_title("目标 %d cm" % tgt, loc="left", fontsize=10.5)
        if tgt == 10:
            ax.legend(loc="upper right", ncol=4, fontsize=8)
    axes[-1].set_xlabel("时间 / s")
    fig.suptitle("三高度定高实测时间序列（统一保守增益 $K_p$12 $K_i$1.1 $K_d$7）",
                 fontsize=12.5, fontweight="bold")
    finish(fig, "hold.pdf")
    print("   HOLD stats:", {k: (round(v[0], 2), round(v[1], 2), round(v[2], 1))
                             for k, v in stats.items()})
    return stats


# ============ F2: 15cm multi-signal (H / PWM / velocity) ============
def fig_multi():
    d = load(F_HOLD[15]); t = d["t"]
    fig, ax = plt.subplots(3, 1, figsize=(7.4, 6.4), sharex=True,
                           constrained_layout=True)
    # height
    band(ax[0], B_1CM, 14, 16, "±1 cm")
    ax[0].axhline(15, color=C_TGT, lw=1.2, ls="--", label="目标 15 cm")
    ax[0].plot(t, d["H_cm"], color=C_ACT, lw=1.0, label="实际高度")
    ax[0].set_ylabel("高度 / cm"); ax[0].set_ylim(11, 19)
    ax[0].legend(loc="upper right", ncol=3, fontsize=8)
    ax[0].set_title("(a) 高度响应", loc="left", fontsize=10)
    # pwm
    ax[1].plot(t, d["P"], color=C_PWM, lw=0.9, label="PWM 占空比计数")
    ax[1].set_ylabel("PWM / 计数")
    pm = d["P"][int(len(d["P"])*0.4):]
    ax[1].text(0.985, 0.08, "稳态 PWM %.0f±%.0f" % (pm.mean(), pm.std()),
               transform=ax[1].transAxes, ha="right", fontsize=8.5,
               bbox=dict(fc="white", ec="#999", alpha=0.9, pad=2.5))
    ax[1].set_title("(b) 控制量（执行器努力）", loc="left", fontsize=10)
    ax[1].legend(loc="upper right", fontsize=8)
    # velocity
    if "D_cms" in d:
        ax[2].axhline(0, color="#888", lw=0.8)
        ax[2].plot(t, d["D_cms"], color=C_VEL, lw=0.8, label="球速估计 (EMA)")
        vs = d["D_cms"][int(len(d["D_cms"])*0.4):]
        ax[2].text(0.985, 0.08, r"球速 $\sigma$=%.1f cm/s" % vs.std(),
                   transform=ax[2].transAxes, ha="right", fontsize=8.5,
                   bbox=dict(fc="white", ec="#999", alpha=0.9, pad=2.5))
    ax[2].set_ylabel("球速 / cm·s$^{-1}$"); ax[2].set_xlabel("时间 / s")
    ax[2].set_title("(c) 球速（D 项阻尼信号）", loc="left", fontsize=10)
    ax[2].legend(loc="upper right", fontsize=8)
    for a in ax:
        a.set_xlim(0, t[-1])
    fig.suptitle("15 cm 定高多信号实测（高度 / 控制量 / 球速）",
                 fontsize=12.5, fontweight="bold")
    finish(fig, "multi.pdf")


# ============ F3: steady-state error distribution (3 heights) ============
def fig_hist():
    fig, ax = plt.subplots(figsize=(7.2, 3.8), constrained_layout=True)
    cols = {10: "#43A047", 15: "#1E88E5", 20: "#8E24AA"}
    bins = np.linspace(-3, 3, 31)
    for tgt in [10, 15, 20]:
        d = load(F_HOLD[tgt])
        _, _, _, seg = steady(d["H_cm"])
        err = seg - tgt
        pct1 = 100.0 * np.mean(np.abs(err) <= 1.0)
        ax.hist(err, bins=bins, alpha=0.5, color=cols[tgt], edgecolor="white",
                label="%d cm（±1cm 内 %.0f%%）" % (tgt, pct1))
    ax.axvspan(-1, 1, color=B_1CM, alpha=0.5, zorder=0, label="±1 cm 带")
    ax.axvline(0, color=C_TGT, lw=1.2, ls="--")
    ax.set_xlabel("稳态误差 $h-h_{target}$ / cm")
    ax.set_ylabel("样本数")
    ax.set_title("三高度稳态误差分布", fontsize=12.5)
    ax.legend(loc="upper right", fontsize=8.5)
    finish(fig, "hist.pdf")


# ============ F4: phase portrait (h-htgt vs velocity) ============
def fig_phase():
    fig, ax = plt.subplots(figsize=(5.6, 5.2), constrained_layout=True)
    d = load(F_HOLD[15])
    if "D_cms" not in d:
        plt.close(fig); return
    e = d["H_cm"] - 15.0
    v = d["D_cms"]
    sc = ax.scatter(e, v, c=d["t"], cmap="viridis", s=10, alpha=0.8, lw=0)
    ax.axhline(0, color="#888", lw=0.7)
    ax.axvline(0, color="#888", lw=0.7)
    ax.axvspan(-1, 1, color=B_1CM, alpha=0.4, zorder=0)
    ax.plot(0, 0, marker="*", color=C_TGT, ms=16, zorder=5, label="目标平衡点")
    ax.set_xlabel("高度误差 $h-15$ / cm")
    ax.set_ylabel("球速 $\\dot h$ / cm·s$^{-1}$")
    ax.set_title("15 cm 定高相轨迹（收敛到平衡点）", fontsize=11.5)
    cb = fig.colorbar(sc, ax=ax, pad=0.02)
    cb.set_label("时间 / s", fontsize=9)
    ax.legend(loc="upper right", fontsize=8.5)
    finish(fig, "phase.pdf")


# ============ F5: dynamic tracking step 10->20 (H + PWM) ============
def fig_track():
    d = load(F_TRACK); t = d["t"]; h = d["H"]
    fig, ax = plt.subplots(2, 1, figsize=(7.4, 5.0), sharex=True,
                           height_ratios=[2, 1], constrained_layout=True)
    band(ax[0], B_2CM, 18, 22, "目标 ±2 cm")
    band(ax[0], B_1CM, 19, 21, "目标 ±1 cm")
    ax[0].step([t[0], t[-1]], [20, 20], where="post", color=C_TGT, lw=1.3,
               ls="--", label="目标 20 cm")
    ax[0].plot(t, h, color=C_ACT, lw=1.2, label="实际高度")
    # first reach of the +/-2cm vicinity (h>=18) = fast response metric
    i2 = int(np.argmax(h >= 18.0))
    tr = t[i2]
    ax[0].axvline(tr, color=C_ACC, lw=1.1, ls=":")
    ax[0].annotate("初到目标邻域(±2cm)\n$t_r$≈%.1f s" % tr,
                   xy=(tr, 18), xytext=(tr + 0.5, 12.5), fontsize=8.5,
                   color=C_ACC, arrowprops=dict(arrowstyle="->", color=C_ACC, lw=1))
    # honest note: residual slow oscillation, +/-1cm not cleanly held within 5s
    ax[0].text(0.985, 0.06, "残余慢振荡：严格保持 ±1cm 时间 > 5 s（进阶优化中）",
               transform=ax[0].transAxes, ha="right", va="bottom", fontsize=8,
               color="#b25e00",
               bbox=dict(fc="white", ec="#e0a060", alpha=0.9, pad=2.5))
    ax[0].set_ylabel("高度 / cm"); ax[0].set_ylim(8, 23)
    ax[0].legend(loc="lower right", fontsize=8.5, ncol=2)
    ax[0].set_title("(a) 高度阶跃响应", loc="left", fontsize=10)
    if "P" in d:
        ax[1].plot(t, d["P"], color=C_PWM, lw=1.0, label="PWM 占空比计数")
        ax[1].legend(loc="upper right", fontsize=8)
    ax[1].set_ylabel("PWM / 计数"); ax[1].set_xlabel("时间 / s")
    ax[1].set_title("(b) 控制量", loc="left", fontsize=10)
    for a in ax:
        a.set_xlim(0, t[-1])
    fig.suptitle("动态跟踪阶跃响应（10 → 20 cm）", fontsize=12.5, fontweight="bold")
    finish(fig, "track.pdf")
    print("   TRACK t_reach(>=18)=%.2f  first|e|<=1=%.2f" %
          (tr, t[int(np.argmax(np.abs(h - 20) <= 1.0))]))


# ============ F6: fan step response + tau identification ============
def fig_fan():
    d = load(F_FAN); t = d["t"]; R = d["R"]; P = d["P"]
    pmax = P.max(); i0 = int(np.where(P >= pmax)[0][0])
    st = t[i0:] - t[i0]; sR = R[i0:]
    r0 = sR[0]; rss = sR[int(len(sR) * 0.6):].mean()
    target = r0 + 0.632 * (rss - r0)
    tau = None
    for i in range(1, len(sR)):
        if sR[i] >= target:
            t1, t2, r1, r2 = st[i-1], st[i], sR[i-1], sR[i]
            tau = t1 + (target - r1) / (r2 - r1) * (t2 - t1) if r2 != r1 else t2
            break
    fig, ax = plt.subplots(figsize=(7.2, 3.7), constrained_layout=True)
    # first-order reference curve
    if tau:
        tt = np.linspace(0, st[-1], 200)
        ax.plot(tt, r0 + (rss - r0) * (1 - np.exp(-tt / tau)), color=C_ACC,
                lw=1.0, ls="--", alpha=0.8, label="一阶拟合 $\\tau$=%.2f s" % tau)
    ax.plot(st, sR, color=C_RPM, lw=1.2, marker="o", ms=4, label="实测转速 R(t)")
    ax.axhline(rss, color="#888", lw=0.8, ls=":", label="稳态 %.0f rpm" % rss)
    ax.axhline(target, color="#bbb", lw=0.8, ls=":")
    if tau:
        ax.axvline(tau, color=C_ACC, lw=0.9, ls=":")
        ax.annotate("63%% 点\n$\\tau$≈%.2f s" % tau, xy=(tau, target),
                    xytext=(tau + 0.8, r0 + 0.35 * (rss - r0)), fontsize=9,
                    color=C_ACC,
                    arrowprops=dict(arrowstyle="->", color=C_ACC, lw=1))
    ax.set_xlabel("时间 / s"); ax.set_ylabel("转速 / rpm")
    ax.set_title("风机阶跃响应与时间常数辨识（PWM 3000→4000，暖机段）",
                 fontsize=11.5)
    ax.legend(loc="lower right", fontsize=8.5)
    finish(fig, "fan.pdf")
    print("   FAN tau=%.2f r0=%.0f rss=%.0f" % (tau or -1, r0, rss))


# ============ F7: identification step 15->18 with 2nd-order model overlay ===
def fig_idstep():
    d = load(F_IDSTEP); t = d["t"]; h = d["H"]
    h0 = float(np.mean(h[:3]))      # pre-step baseline (ball sits below tgt)
    hf = 18.0                        # commanded final target
    # first overshoot peak within the first cycle (t < 2.5 s)
    mask = t < 2.5
    ipk = int(np.argmax(np.where(mask, h, -np.inf)))
    Mp = (h[ipk] - hf) / (hf - h0)
    tp = t[ipk]
    zeta = 0.30; wn = 2.60; wd = wn * np.sqrt(1 - zeta**2)   # identified model
    tt = np.linspace(0, 3.0, 300)
    model = h0 + (hf - h0) * (1 - np.exp(-zeta * wn * tt) *
                             (np.cos(wd * tt) + zeta / np.sqrt(1 - zeta**2) * np.sin(wd * tt)))
    fig, ax = plt.subplots(figsize=(7.2, 3.8), constrained_layout=True)
    ax.axhline(hf, color=C_TGT, lw=1.1, ls="--", label="目标 18 cm")
    ax.axhline(h0, color="#999", lw=0.8, ls=":", label="初始 %.1f cm" % h0)
    ax.plot(t, h, color=C_ACT, lw=1.0, marker=".", ms=4, label="实测高度（欠阻尼）")
    ax.plot(tt, model, color=C_ACC, lw=1.6, alpha=0.85,
            label="辨识二阶模型 $\\zeta$=0.3 $\\omega_n$=2.6")
    ax.plot(tp, h[ipk], "v", color=C_PWM, ms=10, zorder=5)
    ax.annotate("首超调 $M_p$≈%.0f%%\n$t_p$≈%.2f s" % (Mp * 100, tp),
                xy=(tp, h[ipk]), xytext=(tp + 0.6, h[ipk] + 0.5), fontsize=9,
                color=C_PWM, arrowprops=dict(arrowstyle="->", color=C_PWM, lw=1))
    ax.set_xlabel("时间 / s"); ax.set_ylabel("高度 / cm")
    ax.set_xlim(0, t[-1])
    ax.set_title("辨识阶跃（目标 15→18 cm）：实测与二阶模型，反算 $g$≈0.333",
                 fontsize=11.5)
    ax.legend(loc="lower right", fontsize=8.2, ncol=1)
    finish(fig, "idstep.pdf")
    print("   IDSTEP h0=%.1f Mp=%.0f%% tp=%.2f" % (h0, Mp * 100, tp))


if __name__ == "__main__":
    print("LOGS exists:", os.path.isdir(LOGS))
    fig_hold()
    fig_multi()
    fig_hist()
    fig_phase()
    fig_track()
    fig_fan()
    fig_idstep()
    print("done ->", FIGS)
