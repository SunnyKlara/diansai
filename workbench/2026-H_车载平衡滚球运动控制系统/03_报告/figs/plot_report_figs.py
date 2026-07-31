#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_report_figs.py —— 2026-H 设计报告出图（矢量 PDF）

用法（仓库根）：
    .venv\\Scripts\\python.exe "workbench\\2026-H_车载平衡滚球运动控制系统\\03_报告\\figs\\plot_report_figs.py"

数据来源分两类，图题里会标清楚，**不许混**：
  [仿真]  workbench/mspm0/car/pc_test/_sim/*.csv
          由 pc_test/dump_ball_sim.c 链接**真正的 ball.c** 导出（不是在 Python 里重算模型）。
          ⚠ 被控对象与 ball.h 的推导同源 ⇒ 证明控制律行为，不证明模型描述真实钢球。
  [实测]  workbench/mspm0/car/_logs/*.csv
          真机遥测逐行记录（run_log.ps1 产物）。
  [解析]  纯公式画出来的（Bode、前视距离、误差预算），无数据来源问题。

设计取向：黑白可打印（不靠颜色区分）、字号够大、每张图都能单独看懂（标题里带结论）。
"""
import os
import sys
import csv
import math

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager as fm

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
SIM = os.path.join(REPO, "workbench", "mspm0", "car", "pc_test", "_sim")
LOGS = os.path.join(REPO, "workbench", "mspm0", "car", "_logs")

# ---------------------------------------------------------------- 中文字体
# 报告是中文的，图里的标签也必须是中文；matplotlib 默认字体没有中文 -> 手工挑一个系统字体。
def pick_cjk_font():
    for name in ("Microsoft YaHei", "SimHei", "Microsoft JhengHei", "SimSun", "DengXian"):
        try:
            p = fm.findfont(fm.FontProperties(family=name), fallback_to_default=False)
            if p and os.path.exists(p):
                return name
        except Exception:
            pass
    return None

_f = pick_cjk_font()
if _f:
    plt.rcParams["font.sans-serif"] = [_f]
    print(f"  CJK font : {_f}")
else:
    print("  [!] 没找到中文字体，图里的中文会显示成方框")
plt.rcParams["axes.unicode_minus"] = False       # 负号用 ASCII，避免缺字
plt.rcParams.update({
    "figure.dpi": 120,
    "savefig.bbox": "tight",
    "font.size": 10.5,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "grid.linestyle": ":",
    "legend.framealpha": 0.9,
})

MADE = []


def save(fig, name):
    out = os.path.join(HERE, name)
    fig.savefig(out)
    plt.close(fig)
    MADE.append((name, os.path.getsize(out)))
    print(f"  -> {name}  ({os.path.getsize(out):,} bytes)")


def load_csv(path):
    """读 CSV 成 {列名: np.array}。缺列不报错，返回 None 让调用方跳过该图。"""
    if not os.path.exists(path):
        print(f"  [skip] 缺数据: {os.path.relpath(path, REPO)}")
        return None
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return None
    cols = {}
    for k in rows[0].keys():
        if k is None:
            continue
        vals = []
        for r in rows:
            try:
                vals.append(float(r[k]))
            except (TypeError, ValueError):
                vals.append(np.nan)
        cols[k.strip()] = np.array(vals)
    cols["_mode"] = np.array([str(r.get("mode", "")).strip('"') for r in rows])
    return cols


# ================================================================ 1 解析：Bode
def fig_bode():
    """开环双积分 + PD 补偿的 Bode，标出穿越频率/相位裕度/延迟裕度。

    这一张的作用：把'我们调了个 PD'变成'我们做了控制设计'。
    对象 G(s)=K/s^2 相角恒 -180 度；纯比例闭环只有虚轴极点，不能渐近衰减；
    PD 的零点提供相位超前与阻尼。
    """
    K = 7007.14            # mm/s^2 per rad
    kp, kd = 9.0, 6.0      # 1/s^2, 1/s   (zeta=1.0)
    w = np.logspace(-2, 2, 2000)
    s = 1j * w

    # 开环（未补偿）：以 rad 为输入、mm 为输出
    G = K / (s ** 2)
    # 补偿后回路增益：C(s)=(kd*s+kp)/K 与 G 串联 -> L(s)=(kd*s+kp)/s^2
    L = (kd * s + kp) / (s ** 2)

    def db(x):
        return 20 * np.log10(np.abs(x))

    def ph(x):
        return np.unwrap(np.angle(x)) * 180 / np.pi

    # 穿越频率与相位裕度
    mag = np.abs(L)
    i = int(np.argmin(np.abs(mag - 1.0)))
    wc = w[i]
    pm = 180 + ph(L)[i]
    tau_max = math.radians(pm) / wc           # 延迟裕度

    # 第三张子图：灵敏度函数 |S|=|1/(1+L)| —— 它才是"为什么必须前馈"的定量证据。
    # 车轮转频处若 |S| 接近 1，说明反馈几乎不衰减该扰动，只能从源头前馈减掉。
    S = 1.0 / (1.0 + L)
    w_wheel = 2 * np.pi * 1.1        # 轮转频 1.1 Hz（v=205mm/s、轮周 186.9mm）
    j = int(np.argmin(np.abs(w - w_wheel)))
    S_wheel = float(np.abs(S[j]))
    L_wheel = float(np.abs(L[j]))

    fig, ax = plt.subplots(3, 1, figsize=(6.6, 7.4), sharex=True,
                           gridspec_kw={"height_ratios": [1, 1, 1]})
    ax[0].semilogx(w, db(G), "--", lw=1.5, color="0.60", label=r"未补偿 $G(s)=K/s^2$")
    ax[0].semilogx(w, db(L), "-", lw=1.9, color="k", label=r"补偿后 $L(s)=(K_d s+K_p)/s^2$")
    ax[0].axhline(0, color="0.3", lw=0.8)
    ax[0].axvline(wc, color="0.3", lw=1.0, ls="-.")
    ax[0].set_ylabel("幅值 / dB")
    ax[0].set_ylim(-60, 105)
    # 图例放左下（那块是空的）；标注放右下，两者不打架 —— 第一版统一 upper right 时
    # 图例正好盖住穿越频率标注。
    ax[0].legend(loc="lower left", fontsize=8.8)
    ax[0].annotate("穿越频率 $\\omega_c$ = %.2f rad/s (%.2f Hz)" % (wc, wc / 2 / np.pi),
                   xy=(wc, 0), xytext=(wc * 1.35, -46), fontsize=9,
                   arrowprops=dict(arrowstyle="->", lw=0.9))
    ax[0].set_title("(a) 幅频：双积分斜率 −40 dB/dec，PD 的零点把高频抬起",
                    fontsize=9.5, loc="left", pad=5)

    ax[1].semilogx(w, ph(G), "--", lw=2.2, color="0.60")
    ax[1].semilogx(w, ph(L), "-", lw=1.9, color="k")
    ax[1].axvline(wc, color="0.3", lw=1.0, ls="-.")
    ax[1].set_ylabel("相位 / 度")
    ax[1].set_ylim(-205, -55)
    # 未补偿相位恒 −180，正好压在坐标轴线上、几乎看不见 -> 用文字点明它
    ax[1].text(0.012, -174, "未补偿：相位恒 −180°（临界不稳定，纯比例无论多大都不稳）",
               fontsize=8.6, color="0.30")
    ax[1].annotate("相位裕度 = %.0f°\n对应延迟裕度 %.0f ms" % (pm, tau_max * 1000),
                   xy=(wc, ph(L)[i]), xytext=(wc * 1.9, -190), fontsize=9,
                   arrowprops=dict(arrowstyle="->", lw=0.9))
    ax[1].set_title("(b) 相频：PD 提供相位超前，把相角抬离 −180°",
                    fontsize=9.5, loc="left", pad=5)

    ax[2].semilogx(w, np.abs(S), "-", lw=1.8, color="k",
                   label=r"灵敏度 $|S|=|1/(1+L)|$")
    ax[2].axhline(1.0, color="0.3", lw=0.8, ls="--")
    ax[2].axvline(w_wheel, color="0.3", lw=1.0, ls="-.")
    ax[2].plot([w_wheel], [S_wheel], "o", ms=6, color="k")
    # ⚠ 不要用 U+21D2 '⇒'：Microsoft YaHei 缺该字形，PDF 里渲染成方框（实测踩过）。
    #   中文报告里改用文字"故"或 U+2192 '→'，两者字体都有。
    ax[2].annotate("车轮转频 1.1 Hz 处 $|S|$ = %.2f\n故反馈只能抑制 %.0f%% 的该扰动"
                   % (S_wheel, (1 - S_wheel) * 100),
                   xy=(w_wheel, S_wheel), xytext=(0.045, 0.55), fontsize=9,
                   arrowprops=dict(arrowstyle="->", lw=0.9))
    ax[2].set_ylabel("$|S|$  越小抑制越强")
    ax[2].set_xlabel("角频率 / (rad/s)")
    ax[2].set_ylim(0, 1.35)
    ax[2].legend(loc="lower right", fontsize=9)
    ax[2].set_title("(c) 灵敏度：俯仰扰动落在反馈几乎无能为力的频段，故只能前馈",
                    fontsize=9.5, loc="left", pad=5)

    fig.suptitle("［解析］双积分对象的 PD 补偿：$\\omega_c$=%.2f rad/s (%.2f Hz)、"
                 "相位裕度 %.0f°、延迟裕度 %.0f ms" % (wc, wc / 2 / np.pi, pm, tau_max * 1000),
                 fontsize=10.5, y=0.985)
    fig.subplots_adjust(hspace=0.42)
    save(fig, "fig_bode.pdf")
    return wc, pm, tau_max, S_wheel, L_wheel


# ================================================================ 2 仿真：要求3 轨迹 + 摆角分解
def fig_traj():
    d = load_csv(os.path.join(SIM, "sim_traj.csv"))
    if d is None:
        return
    t = d["t_s"]
    fig, ax = plt.subplots(3, 1, figsize=(6.8, 7.0), sharex=True)

    ax[0].plot(t, d["x_ref_mm"], "--", lw=1.6, color="0.5", label="规划轨迹 $x_{ref}$")
    ax[0].plot(t, d["x_true_mm"], "-", lw=1.8, color="k", label="钢球实际位置")
    ax[0].axhline(50, color="0.75", lw=0.8)
    ax[0].axhline(-50, color="0.75", lw=0.8)
    ax[0].set_ylabel("球位 / mm")
    ax[0].legend(loc="upper right", fontsize=9)
    ax[0].set_title("(a) 位置跟踪：O → +50 mm → 折返 → −50 mm", fontsize=10, loc="left")

    err = d["x_true_mm"] - d["x_ref_mm"]
    ax[1].plot(t, err, "-", lw=1.6, color="k")
    ax[1].axhline(10, color="0.4", ls="--", lw=1.0)
    ax[1].axhline(-10, color="0.4", ls="--", lw=1.0)
    ax[1].text(t[-1] * 0.99, 10, " 题目门限 ±10 mm", ha="right", va="bottom", fontsize=8.5)
    ax[1].set_ylabel("跟踪误差 / mm")
    ax[1].set_ylim(-12, 12)
    ax[1].set_title("(b) 跟踪误差（峰值 %.2f mm，门限的 %.1f%%）"
                    % (np.nanmax(np.abs(err)), 100 * np.nanmax(np.abs(err)) / 10),
                    fontsize=10, loc="left")

    ax[2].plot(t, d["theta_deg"], "-", lw=1.9, color="k", label="合成指令 $\\theta$")
    ax[2].plot(t, d["th_traj"], "-", lw=1.2, color="0.45", label="轨迹加速度前馈")
    ax[2].plot(t, d["th_pd"], ":", lw=1.4, color="0.15", label="PD 项")
    ax[2].axhline(6, color="0.7", lw=0.8)
    ax[2].axhline(-6, color="0.7", lw=0.8)
    ax[2].text(t[-1] * 0.99, 6, " 设计限幅 ±6°（机械极限 ±11.54°）",
               ha="right", va="bottom", fontsize=8.5)
    ax[2].set_ylabel("摆杆倾角 / 度")
    ax[2].set_xlabel("时间 / s")
    ax[2].set_ylim(-7.5, 7.5)
    ax[2].legend(loc="upper right", fontsize=9, ncol=1)
    ax[2].set_title("(c) 指令分解：前馈承担主体，PD 只修残差", fontsize=10, loc="left")

    fig.suptitle("［仿真］第 3 项往返轨迹（数据由固件 ball.c 导出）", fontsize=11)
    save(fig, "fig_traj.pdf")


# ================================================================ 3 仿真：前馈 A/B 对照
def fig_ff_ab():
    """全篇最强的一张：把 23.8 mm 与 6.8 mm 从表格数字变成看得见的两条曲线。"""
    sets = [
        ("sim_ff_ax_off.csv", "sim_ff_ax_on.csv",
         "(a) 车体纵向加速度 $a_x=0.3\\ \\mathrm{m/s^2}$", "加速度前馈"),
        ("sim_ff_pit_off.csv", "sim_ff_pit_on.csv",
         "(b) 车体俯仰 $\\varphi=0.5°$", "俯仰补偿"),
    ]
    # 排版取向（第一版排崩过，记下来）：
    #   1. **不共享 y 轴** —— 两个子图的量级差 3 倍，共享会把 6.8mm 那条挤成一条平线；
    #   2. **不用长箭头标注** —— 箭头横跨画面比不标还难读；文字直接贴在曲线旁；
    #   3. 允许带用 axhspan 画成灰底，比两条虚线直观，且不会压住文字。
    # 排版取向（前两版都排崩过，记下来省下次的返工）：
    #   1. **不共享 y 轴** —— 两个子图量级差 3 倍，共享会把 6.8mm 那条挤成平线；
    #   2. **不用长箭头标注** —— 箭头横跨画面比不标还难读，文字直接贴曲线旁；
    #   3. 允许带用 axhspan 灰底，比两条虚线直观且不压文字；
    #   4. **图例位置必须按曲线走向逐图指定** —— 统一 lower right 时正好盖住两处稳态标注。
    layout = [
        # legend 位置, off 文字落点, on 文字落点, y 范围
        dict(leg="upper right", p_off=(2.55, -21.4), p_on=(2.55, 1.6), ylim=(-28, 13)),
        dict(leg="lower right", p_off=(2.45, 7.9), p_on=(0.85, -1.8), ylim=(-4, 12)),
    ]
    fig, ax = plt.subplots(1, 2, figsize=(9.4, 3.6))
    for k, (foff, fon, title, label) in enumerate(sets):
        a = load_csv(os.path.join(SIM, foff))
        b = load_csv(os.path.join(SIM, fon))
        if a is None or b is None:
            continue
        L = layout[k]
        lo, hi = L["ylim"]
        ax[k].axhspan(-10, 10, color="0.90", zorder=0)
        ax[k].plot(a["t_s"], a["x_true_mm"], "--", lw=1.9, color="0.40",
                   label=f"{label}关闭", zorder=3)
        ax[k].plot(b["t_s"], b["x_true_mm"], "-", lw=2.1, color="k",
                   label=f"{label}开启", zorder=4)
        ss_off = float(a["x_true_mm"][-1])
        ss_on = float(b["x_true_mm"][-1])
        ax[k].text(L["p_off"][0], L["p_off"][1], f"稳态 {ss_off:+.1f} mm",
                   fontsize=9.5, color="0.15", ha="center")
        ax[k].text(L["p_on"][0], L["p_on"][1], f"稳态 {ss_on:+.2f} mm",
                   fontsize=9.5, color="0.15", ha="center")
        ax[k].text(0.08, hi - 0.10 * (hi - lo), "灰带 = 题目允许范围 ±10 mm",
                   fontsize=8.5, color="0.35")
        # 关键判读：即使没超差，也要看它吃掉多少预算 —— 判分"考察全程"，余量才是命
        ax[k].text(0.08, hi - 0.21 * (hi - lo),
                   "关闭时占用预算 %.0f%%" % (100 * abs(ss_off) / 10.0),
                   fontsize=8.5, color="0.15")
        ax[k].set_xlim(0, 4)
        ax[k].set_ylim(lo, hi)
        ax[k].set_xlabel("时间 / s")
        ax[k].set_ylabel("钢球位置偏差 / mm")
        ax[k].set_title(title, fontsize=10, loc="left")
        ax[k].legend(loc=L["leg"], fontsize=9)
    fig.suptitle("［仿真］两条前馈各自的贡献（A/B 单变量对照，数据由固件 ball.c 导出）", fontsize=11)
    save(fig, "fig_ff_ab.pdf")


# ================================================================ 4 仿真：阶跃响应
def fig_step():
    """30 mm 阶跃：zeta=1.0(本设计) 与 zeta=0.8(常用值) 单变量对照。

    只改 kd 一个量（6.0 vs 4.8，kp=9 不动），两条曲线来自 dump_ball_sim 的
    sim_step30.csv / sim_step30_z08.csv。画成对照是因为"为什么选临界阻尼"这个
    取舍必须让读者自己看得出来：zeta=0.8 更快但会冲过零点，而本题钢球脱落即失败。
    """
    d = load_csv(os.path.join(SIM, "sim_step30.csv"))
    if d is None:
        return
    d8 = load_csv(os.path.join(SIM, "sim_step30_z08.csv"))
    t, x = d["t_s"], d["x_true_mm"]
    over = -np.nanmin(x)
    fig, ax = plt.subplots(figsize=(6.4, 3.4))
    ax.plot(t, x, "-", lw=2.0, color="k", label=r"$\zeta$=1.0（本设计，$k_d$=6.0）")
    ax.plot(t, d["x_est_mm"], ":", lw=1.2, color="0.5", label="观测器估计（$\\zeta$=1.0）")
    ttl_extra = ""
    if d8 is not None:
        x8 = d8["x_true_mm"]
        over8 = -np.nanmin(x8)
        ax.plot(d8["t_s"], x8, "--", lw=1.6, color="0.30",
                label=r"$\zeta$=0.8（对照，$k_d$=4.8）")
        # 标出对照组冲过零点的那一段
        i8 = int(np.nanargmin(x8))
        ax.annotate("反向过冲 %.2f mm" % max(over8, 0.0),
                    xy=(d8["t_s"][i8], x8[i8]),
                    xytext=(d8["t_s"][i8] + 0.35, -4.2), fontsize=8.5,
                    arrowprops=dict(arrowstyle="->", lw=0.9))
        ttl_extra = "，对照组 %.2f mm" % max(over8, 0.0)
    ax.axhline(0, color="0.35", lw=0.8)
    for y in (3, -3):
        ax.axhline(y, color="0.75", lw=0.8, ls="--")
    ax.text(t[-1] * 0.99, 3, " ±3 mm（自设目标带）", ha="right", va="bottom", fontsize=8.5)
    ax.set_xlabel("时间 / s")
    ax.set_ylabel("球位 / mm")
    ax.legend(fontsize=8.5, loc="upper right")
    ax.set_title("［仿真］30 mm 初始偏差回中：本设计全程反向过冲 %.3f mm%s"
                 % (max(over, 0.0), ttl_extra), fontsize=11)
    save(fig, "fig_step.pdf")


# ================================================================ 5 解析：前视距离
def fig_lookahead():
    R, w_half, line_half = 0.5, 0.042, 0.009
    d = np.linspace(0, 0.30, 400)
    delta = np.sqrt(R ** 2 + d ** 2) - R
    d_max = math.sqrt(2 * R * w_half)

    fig, ax = plt.subplots(figsize=(6.4, 3.4))
    ax.plot(d * 1000, delta * 1000, "-", lw=2.0, color="k",
            label=r"弯道固有横向偏移 $\Delta=\sqrt{R^2+d^2}-R$")
    ax.axhline(w_half * 1000, color="0.3", ls="--", lw=1.2)
    ax.axhline(line_half * 1000, color="0.6", ls=":", lw=1.2)
    ax.axvline(d_max * 1000, color="0.3", ls="-.", lw=1.2)
    ax.text(4, w_half * 1000 + 1.5, "阵列半宽 42 mm（超过即丢线）", fontsize=8.5)
    ax.text(4, line_half * 1000 + 1.2, "线宽半值 9 mm", fontsize=8.5, color="0.35")
    ax.annotate(f"硬边界 $d<\\sqrt{{2Rw}}$ = {d_max*1000:.0f} mm",
                xy=(d_max * 1000, w_half * 1000),
                xytext=(d_max * 1000 - 108, 60), fontsize=9,
                arrowprops=dict(arrowstyle="->", lw=0.9))
    for dd in (0.08, 0.10, 0.175):
        y = (math.sqrt(R ** 2 + dd ** 2) - R) * 1000
        ax.plot([dd * 1000], [y], "o", ms=5, color="k")
        ax.text(dd * 1000 + 3, y - 3.4, f"{dd*1000:.0f} mm → {y:.1f} mm", fontsize=8.5)
    ax.set_xlabel("探头阵列前视距离 $d$ / mm")
    ax.set_ylabel("弯道横向偏移 $\\Delta$ / mm")
    ax.set_xlim(0, 300)
    ax.set_ylim(0, 90)
    ax.legend(loc="upper left", fontsize=9)
    ax.set_title("［解析］急弯（$R$=0.5 m）对前视距离的硬约束：与控制器性能无关", fontsize=11)
    save(fig, "fig_lookahead.pdf")


# ================================================================ 6 解析：误差预算
def fig_budget():
    items = ["相机量化\n+质心", "高光引起\n质心偏移", "标定与\n畸变残差",
             "透视视差", "车体俯仰", "名义指令加速度\n残差", "舵机回差"]
    current = [0.3, 1.0, 0.5, 0.0, 1.4, 2.4, 1.6]
    # 对照组只改相机安装与俯仰补偿；加速度前馈残差假设保持不变。
    comparison = [0.3, 1.0, 0.5, 16.8, 6.8, 2.4, 1.6]
    x = np.arange(len(items))
    rss_current = math.sqrt(sum(v * v for v in current))
    rss_comparison = math.sqrt(sum(v * v for v in comparison))

    fig, ax = plt.subplots(figsize=(7.6, 3.8))
    ax.bar(x - 0.2, comparison, 0.4, color="0.75", edgecolor="k", lw=0.8,
           label="车体固定相机 + 无俯仰补偿")
    ax.bar(x + 0.2, current, 0.4, color="0.25", edgecolor="k", lw=0.8,
           label="随杆相机 + 俯仰补偿")
    ax.axhline(10, color="k", ls="--", lw=1.2)
    ax.text(len(items) - 0.5, 10.4, "题目门限 ±10 mm", ha="right", fontsize=9)
    ax.set_xticks(x)
    ax.set_xticklabels(items, fontsize=8.5)
    ax.set_ylabel("假设误差量级 / mm")
    ax.set_ylim(0, 19)
    ax.legend(fontsize=9, loc="upper left")
    ax.set_title("［设计估计］误差灵敏度 RSS 对比：%.1f mm 与 %.1f mm（非整机实测）"
                 % (rss_comparison, rss_current), fontsize=11)
    save(fig, "fig_budget.pdf")


# ================================================================ 7 实测：速度环阶跃
def fig_speedloop():
    d = load_csv(os.path.join(LOGS, "run_motor_v100.csv"))
    if d is None or "v1" not in d:
        return
    t = (d["t_ms"] - d["t_ms"][0]) / 1000.0
    m = (t >= 0) & (t <= t[-1])
    fig, ax = plt.subplots(2, 1, figsize=(6.8, 4.6), sharex=True,
                           gridspec_kw={"height_ratios": [2, 1]})
    ax[0].step(t[m], d["tgt"][m], where="post", lw=1.4, color="0.5", label="目标转速")
    ax[0].plot(t[m], d["v1"][m], "-", lw=1.5, color="k", label="左轮实测")
    ax[0].plot(t[m], d["v2"][m], "--", lw=1.3, color="0.35", label="右轮实测")
    ax[0].set_ylabel("转速 / RPM")
    ax[0].legend(fontsize=9, loc="lower right", ncol=3)
    ax[0].set_title("(a) 两轮独立速度环对阶跃指令的响应", fontsize=10, loc="left")

    ax[1].plot(t[m], d["pwm1"][m], "-", lw=1.3, color="k", label="左轮 PWM")
    ax[1].plot(t[m], d["pwm2"][m], "--", lw=1.2, color="0.4", label="右轮 PWM")
    ax[1].axhline(60, color="0.3", ls="--", lw=1.0)
    ax[1].text(t[m][-1], 61, "限幅 60%", ha="right", fontsize=8.5)
    ax[1].set_ylabel("占空比 / %")
    ax[1].set_xlabel("时间 / s")
    ax[1].legend(fontsize=9, loc="lower right", ncol=2)
    ax[1].set_title("(b) 该工况下控制量未触及 60% 限幅", fontsize=10, loc="left")

    fig.suptitle("［实测］速度环闭环响应（车载遥测逐行记录）", fontsize=11)
    save(fig, "fig_speedloop.pdf")


# ================================================================ 8 实测：静止 yaw 漂移
def fig_yawdrift():
    d = load_csv(os.path.join(LOGS, "run_soak_5min.csv"))
    if d is None or "yaw_deg" not in d:
        return
    t = (d["t_ms"] - d["t_ms"][0]) / 60000.0        # 分钟
    y = d["yaw_deg"] - d["yaw_deg"][0]
    ok = np.isfinite(t) & np.isfinite(y)
    t, y = t[ok], y[ok]
    if len(t) < 10:
        return
    k = np.polyfit(t, y, 1)[0]                      # 度/分钟

    fig, ax = plt.subplots(figsize=(6.6, 3.2))
    ax.plot(t, y, "-", lw=1.2, color="k", label="偏航角累计变化")
    ax.plot(t, np.polyval(np.polyfit(t, y, 1), t), "--", lw=1.4, color="0.45",
            label=f"线性拟合 {k:+.2f} °/min")
    ax.set_xlabel("时间 / min")
    ax.set_ylabel("偏航角变化 / 度")
    ax.legend(fontsize=9)
    ax.set_title("［实测］静止 %.0f 分钟的偏航积分漂移（已做零偏标定）" % t[-1], fontsize=11)
    save(fig, "fig_yawdrift.pdf")


# ================================================================
def main():
    print(f"repo : {REPO}")
    print(f"sim  : {os.path.relpath(SIM, REPO)}")
    print(f"logs : {os.path.relpath(LOGS, REPO)}")
    print("---- 出图 ----")
    wc, pm, tau, s_wheel, l_wheel = fig_bode()
    fig_traj()
    fig_ff_ab()
    fig_step()
    fig_lookahead()
    fig_budget()
    fig_speedloop()
    fig_yawdrift()

    print("---- 汇总 ----")
    for n, sz in MADE:
        print(f"  {n:22s} {sz:>9,d} bytes")
    print(f"  共 {len(MADE)} 张")
    print(f"  Bode 关键值（可直接写进报告）：wc={wc:.3f} rad/s ({wc/2/np.pi:.3f} Hz), "
          f"PM={pm:.1f} deg, 延迟裕度={tau*1000:.0f} ms")
    # 诊断输出一律 ASCII：控制台默认 GBK，打 '⇒' 之类会抛 UnicodeEncodeError（实测踩过）
    print(f"  wheel 1.1Hz: |L|={l_wheel:.3f}, |S|={s_wheel:.3f} "
          f"-> nominal feedback rejection {(1-s_wheel)*100:.0f}% (verify with measured pitch spectrum)")
    print(f"  sample rate: 20x crossover needs >= {20*wc/2/np.pi:.1f} Hz "
          f"-> 30fps margin {30/(20*wc/2/np.pi):.2f}x (tight; 60fps safer)")
    return 0 if MADE else 1


if __name__ == "__main__":
    sys.exit(main())
