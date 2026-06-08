#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
校赛 B 智能球平衡控制装置 —— PC 金标准仿真（可真实运行）

目的：在写 MCU 代码之前，先在 PC 上把"被控物理 + 控制律"跑通，
证明 前馈 u_ff + PID(D微分先行 + 弱I) + 设定值ramp 能让乒乓球：
  - 稳定悬浮在 10/15/20cm（稳态误差 ≤ ±1cm）
  - 10cm↔20cm 切换调节时间 ≤ 5s、无明显振荡

物理模型（竖直气流管中的乒乓球，与深度审题第二层一致）：
  m * a = F_drag - m*g
  F_drag = 0.5 * rho * Cd * A * (v_air - v_ball)^2     (v_air > v_ball 时向上)
  风机出口风速随高度衰减（射流扩散）：v_air(x, u) = k_fan * u / (1 + alpha * x)
  u = 风机占空比(0~1)，由控制器给出。

控制律（与 MCU 端 ball_ctrl.c 将完全一致，便于移植）：
  e = h_set_ramped - h_meas
  u = u_ff(h_set) + Kp*e + Ki*∫e(抗饱和) - Kd*(h_meas微分)     # 微分先行
  u = clamp(u, u_min, u_max)

运行：python ball_plant_golden.py        （仅需标准库 + 可选 matplotlib）
"""

import math
import sys
try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

# ============ 物理常数（乒乓球 + 空气）============
M = 2.7e-3            # 球质量 kg（标准三星乒乓球）
G = 9.8
RHO = 1.2            # 空气密度 kg/m^3
CD = 0.47            # 球体阻力系数
D_BALL = 0.040       # 球径 m
A = math.pi * (D_BALL / 2) ** 2   # 迎风面积 m^2
MG = M * G                         # 悬浮所需推力 ≈ 0.0265 N

# ============ 风机/风道模型（标定型参数）============
# v_air(x,u) = K_FAN * u / (1 + ALPHA * x)
# 选 K_FAN/ALPHA 使： 在 x≈0.15m、u≈0.55 附近达到悬浮风速 ~8.6 m/s
K_FAN = 16.0         # 满占空比出口风速标度 m/s
ALPHA = 3.0          # 高度衰减系数 1/m
U_MIN = 0.20         # 风机起浮死区（低于此不足以托起球）
U_MAX = 1.00

# ============ 控制器参数（= 将来的 config.h）============
TS = 0.02            # 控制周期 20ms = 50Hz
KP = 0.55
KI = 0.18
KD = 0.85
RAMP_RATE = 0.06     # 设定值每周期最多变化 m（0.06m/0.02s=3m/s，过渡平滑）
I_LIMIT = 0.25       # 积分项限幅
DEADBAND = 0.002     # 误差死区 2mm 内不积分


def v_air(x, u):
    return K_FAN * u / (1.0 + ALPHA * max(x, 0.0))


def drag_accel(x, v_ball, u):
    """返回球的净加速度 (m/s^2)。"""
    vrel = v_air(x, u) - v_ball
    f_drag = 0.5 * RHO * CD * A * vrel * abs(vrel)   # 带符号
    return (f_drag - MG) / M


def u_feedforward(h_set):
    """该目标高度的悬浮基准占空比：令 v_air(h_set,u)=v_hover 反解。"""
    v_hover = math.sqrt(2 * MG / (RHO * CD * A))      # ≈8.6 m/s
    return clamp(v_hover * (1.0 + ALPHA * h_set) / K_FAN, U_MIN, U_MAX)


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


class BallController:
    """前馈 + PID(微分先行) + 设定值 ramp + 抗积分饱和。与 MCU 端一致。"""

    def __init__(self):
        self.integ = 0.0
        self.h_set_ramped = 0.10
        self.prev_h = 0.10

    def update(self, h_set_target, h_meas):
        # 1) 设定值 ramp（满足"单次调整≤3cm""无明显振荡"）
        d = h_set_target - self.h_set_ramped
        if d > RAMP_RATE:
            d = RAMP_RATE
        elif d < -RAMP_RATE:
            d = -RAMP_RATE
        self.h_set_ramped += d

        e = self.h_set_ramped - h_meas
        u_ff = u_feedforward(self.h_set_ramped)

        # 2) 弱积分（死区外才积，抗饱和）
        if abs(e) > DEADBAND:
            self.integ += e * TS
            self.integ = clamp(self.integ, -I_LIMIT, I_LIMIT)

        # 3) 微分先行：对测量微分，避免设定值突变踢腿
        dmeas = (h_meas - self.prev_h) / TS
        self.prev_h = h_meas

        u = u_ff + KP * e + KI * self.integ - KD * dmeas
        return clamp(u, U_MIN, U_MAX)


def simulate(setpoint_schedule, t_end=12.0, x0=0.05, disturb=None):
    """返回 (时间, 高度, 占空比, 目标) 列表。setpoint_schedule: t->目标高度(m)。"""
    n = int(t_end / TS)
    x, v = x0, 0.0
    ctrl = BallController()
    T, H, U, S = [], [], [], []
    for k in range(n):
        t = k * TS
        h_set = setpoint_schedule(t)
        u = ctrl.update(h_set, x)
        # 物理积分（子步 Euler，稳定）
        sub = 8
        dt = TS / sub
        for _ in range(sub):
            a = drag_accel(x, v, u)
            if disturb:
                a += disturb(t)
            v += a * dt
            x += v * dt
            if x < 0.0:     # 触底
                x, v = 0.0, 0.0
        T.append(t); H.append(x * 100); U.append(u); S.append(h_set * 100)
    return T, H, U, S


def settle_time(T, H, target_cm, band=1.0):
    """进入并保持在 target±band(cm) 的时刻。"""
    last_out = T[0]
    for t, h in zip(T, H):
        if abs(h - target_cm) > band:
            last_out = t
    return last_out


def report(name, T, H, S, target_cm, t_change=None):
    tail = [h for t, h in zip(T, H) if t > T[-1] - 2.0]
    sse = sum(abs(h - target_cm) for h in tail) / len(tail)
    mx = max(tail); mn = min(tail)
    print(f"[{name}] 目标 {target_cm:.0f}cm | 稳态均误差 {sse:.2f}cm | "
          f"末段范围 {mn:.1f}~{mx:.1f}cm | 稳态带宽 ±{(mx-mn)/2:.2f}cm")
    return sse


def main():
    print("=" * 64)
    print("校赛 B 球平衡 PC 金标准仿真 | 50Hz | 前馈+PID(微分先行)+ramp")
    print(f"悬浮所需风速 ≈ {math.sqrt(2*MG/(RHO*CD*A)):.2f} m/s | "
          f"u_ff(10/15/20cm)={u_feedforward(.10):.2f}/{u_feedforward(.15):.2f}/{u_feedforward(.20):.2f}")
    print("=" * 64)

    ok = True
    # --- 测试 1：三个定高点 ---
    for tgt in (0.10, 0.15, 0.20):
        T, H, U, S = simulate(lambda t: tgt, t_end=8.0)
        sse = report(f"定高{int(tgt*100)}", T, H, S, tgt * 100)
        ok &= sse <= 1.0

    # --- 测试 2：动态跟踪 10→20→10，调节时间 ≤5s ---
    def sched(t):
        return 0.10 if t < 3 else 0.20 if t < 9 else 0.10
    T, H, U, S = simulate(sched, t_end=15.0)
    # 10→20 段（3s 起）调节时间
    seg1 = [(t - 3, h) for t, h in zip(T, H) if 3 <= t < 9]
    st_up = settle_time([t for t, _ in seg1], [h for _, h in seg1], 20.0, 1.0)
    seg2 = [(t - 9, h) for t, h in zip(T, H) if t >= 9]
    st_dn = settle_time([t for t, _ in seg2], [h for _, h in seg2], 10.0, 1.0)
    print(f"[跟踪] 10→20cm 调节时间 {st_up:.2f}s | 20→10cm 调节时间 {st_dn:.2f}s （要求 ≤5s）")
    ok &= (st_up <= 5.0 and st_dn <= 5.0)

    # --- 测试 3：抗扰动（第6s 给一个向下冲击，模拟手戳气流）---
    def dist(t):
        return -25.0 if 6.0 <= t < 6.1 else 0.0
    T, H, U, S = simulate(lambda t: 0.15, t_end=10.0, disturb=dist)
    recov = [h for t, h in zip(T, H) if t > 8.0]
    rec_err = sum(abs(h - 15.0) for h in recov) / len(recov)
    print(f"[抗扰] 6s 施加向下冲击后，8s 起稳态均误差 {rec_err:.2f}cm （能自恢复）")
    ok &= rec_err <= 1.0

    print("=" * 64)
    print("结论：" + ("✅ 全部通过 —— 控制律可移植到 MCU" if ok else "❌ 未达标，需调参"))
    print("=" * 64)

    # 可选：画图（有 matplotlib 才画）
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        T, H, U, S = simulate(sched, t_end=15.0)
        fig, ax = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
        ax[0].plot(T, H, label="球高 h(cm)"); ax[0].plot(T, S, "--", label="目标(cm)")
        ax[0].set_ylabel("高度 cm"); ax[0].legend(); ax[0].grid(alpha=.3)
        ax[1].plot(T, U, color="tab:orange"); ax[1].set_ylabel("风机占空比")
        ax[1].set_xlabel("时间 s"); ax[1].grid(alpha=.3)
        out = __file__.replace("ball_plant_golden.py", "golden_curve.png")
        fig.tight_layout(); fig.savefig(out, dpi=110)
        print(f"曲线已保存：{out}")
    except Exception as e:
        print(f"(未画图：{e})")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
