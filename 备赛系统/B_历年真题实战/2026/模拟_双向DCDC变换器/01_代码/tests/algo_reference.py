"""
algo_reference.py
=================

2026 模拟 双向 DC-DC 变换器 — 算法层 Python 金标准。

镜像范围：
  - 双闭环 PI（电压环 + 电流环）
  - Buck / Boost 模式自动切换状态机
  - 模式切换瞬态（PI 积分重置）

主要价值：
  1. 验证 PI 参数能让稳态误差 < 容差（Buck ±0.1V / Boost ±0.2V）
  2. 验证模式切换瞬态 < 50ms
  3. 验证算法在阶跃负载下的响应

复用：snippets/pid_controller.py
"""

import math
import sys
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path

# 引用 snippets 库（验证抽象层是否就位）
SNIPPETS = Path(__file__).resolve().parents[5] / \
    "C_通用能力" / "算法层PC验证方法论" / "snippets"
sys.path.insert(0, str(SNIPPETS))
from pid_controller import PIDController


# ============================================================
#  与 config.h 同步的常量
# ============================================================
VBAT_NOMINAL = 24.0      # 电池端额定电压
VBUS_BUCK = 12.0         # Buck 输出目标
VBUS_BOOST = 24.0        # Boost 输出目标
TOL_BUCK = 0.1           # Buck 容差 ±0.1V
TOL_BOOST = 0.2          # Boost 容差 ±0.2V

PWM_FSW = 100_000.0      # 100 kHz 开关频率
TS_INNER = 1.0 / PWM_FSW # 电流环周期 = PWM 周期 = 10us
TS_OUTER = 100e-6        # 电压环周期 = 100us（10kHz）

# 双闭环 PI 参数（PC 仿真已调参，达稳态 < 0.1V 误差）
KP_V = 0.8
KI_V = 5000.0
KP_I = 0.5
KI_I = 8000.0

# 系统物理参数
L_INDUCTOR = 22e-6       # 22 uH
C_OUTPUT = 470e-6        # 470 uF
R_LOAD_BUCK = 12.0 / 5.0 # 满载 5A → R = 2.4 Ω
R_LOAD_BOOST = 24.0 / 2.5 # 满载 2.5A → R = 9.6 Ω
R_DCR = 0.020            # 电感 DCR
R_DSON = 0.010           # MOS 导通电阻


# ============================================================
#  模式
# ============================================================
class Mode(IntEnum):
    BUCK = 0
    BOOST = 1


# ============================================================
#  双向 DC-DC 模型（带损耗的简化连续时间模型）
# ============================================================
@dataclass
class DCDCState:
    vbus: float = 0.0      # 母线电压（A 端，要稳压的目标）
    iL: float = 0.0        # 电感电流
    vbat: float = VBAT_NOMINAL


def dcdc_step(state: DCDCState, duty: float, mode: Mode,
              r_load: float, dt: float):
    """简化的双向 Buck-Boost 平均模型（连续导通模式 CCM）。
    
    Buck:  Vout = D × Vin
    Boost: Vout = Vin / (1 - D)
    
    考虑损耗后：实际占空比有 ±2~5% 的偏移。
    """
    if mode == Mode.BUCK:
        # 输入 vbat → 输出 vbus
        # 平均开关方程: L · diL/dt = D · vbat - vbus - I · (DCR + Rdson)
        vL = duty * state.vbat - state.vbus - state.iL * (R_DCR + R_DSON)
        di_dt = vL / L_INDUCTOR
        # 输出电容: C · dvout/dt = iL - vbus / R
        i_out = state.vbus / r_load
        dv_dt = (state.iL - i_out) / C_OUTPUT
    else:  # BOOST
        # 输入 vbus（A 端）→ 输出 vbat（B 端）—— 反向
        # 但一般 Boost 模式是 vbus 作为输出（双向中 vbus = 24V 目标）
        # 输入 vbat（电池低电），输出 vbus（高压母线）
        # 实际是 vbat → 升压 → vbus
        # L · diL/dt = vbat - (1-D) · vbus - iL · (DCR + Rdson)
        vL = state.vbat - (1.0 - duty) * state.vbus - state.iL * (R_DCR + R_DSON)
        di_dt = vL / L_INDUCTOR
        i_out = state.vbus / r_load
        dv_dt = ((1.0 - duty) * state.iL - i_out) / C_OUTPUT
    
    state.iL += di_dt * dt
    state.vbus += dv_dt * dt
    # 物理约束
    if state.vbus < 0: state.vbus = 0
    if state.iL < 0: state.iL = 0  # 不允许反向（简化）


# ============================================================
#  双闭环 PI 控制器
# ============================================================
class DualLoopPI:
    def __init__(self, vref: float, ramp_per_step: float = None):
        self.vref_final = vref         # 最终目标
        self.ramp_per_step = ramp_per_step  # 每个外环周期推进多少 V
        # 外环 setpoint 从 0 起步
        initial = 0.0 if ramp_per_step is not None else vref
        self.outer = PIDController(Kp=KP_V, Ki=KI_V * TS_OUTER, Kd=0.0,
                                   out_min=0.0, out_max=15.0,
                                   integral_max=20.0)
        self.inner = PIDController(Kp=KP_I, Ki=KI_I * TS_INNER, Kd=0.0,
                                   out_min=0.05, out_max=0.95,
                                   integral_max=10.0)
        self.outer.set_target(initial)
        self.iref = 0.0
        self.outer_counter = 0

    def update(self, vbus: float, iL: float) -> float:
        # 外环（10kHz）+ ramp setpoint
        if self.outer_counter % 10 == 0:
            if self.ramp_per_step is not None:
                self.outer.set_target_ramp(self.vref_final, self.ramp_per_step)
            self.iref = self.outer.compute(vbus)
        self.outer_counter += 1
        self.inner.set_target(self.iref)
        duty = self.inner.compute(iL)
        return duty

    def reset_for_mode_switch(self, new_mode: Mode, vbat: float, vbus_target: float):
        """模式切换时把 PI 积分重置为新模式的稳态前馈值。"""
        # 稳态占空比的解析解
        if new_mode == Mode.BUCK:
            d_steady = vbus_target / vbat
        else:
            d_steady = 1.0 - vbat / vbus_target
        # 重置积分到对应稳态
        self.inner.integral = d_steady / max(KI_I * TS_INNER, 1e-9)
        self.outer.reset()


# ============================================================
#  仿真
# ============================================================
def simulate_buck(initial_vbus: float = 0.0, duration_s: float = 0.05) -> dict:
    """Buck 模式：12V 启动 → 测稳态精度 + 收敛时间"""
    state = DCDCState(vbus=initial_vbus, iL=0.0, vbat=24.0)
    # ramp 5ms 内推到目标 → 每个外环周期 (100us) 推 0.24V
    pi = DualLoopPI(vref=VBUS_BUCK, ramp_per_step=12.0 / 50)
    
    history = []
    t = 0.0
    settle_t = None
    while t < duration_s:
        duty = pi.update(state.vbus, state.iL)
        # 用电流环周期作为仿真步长（最快的）
        dcdc_step(state, duty, Mode.BUCK, R_LOAD_BUCK, TS_INNER)
        history.append((t, state.vbus, state.iL, duty))
        if settle_t is None and abs(state.vbus - VBUS_BUCK) < TOL_BUCK and t > 0.005:
            settle_t = t
        t += TS_INNER
    
    final_err = state.vbus - VBUS_BUCK
    overshoot = max((v - VBUS_BUCK for _, v, _, _ in history), default=0.0)
    return {
        "final_vbus": state.vbus,
        "final_err": final_err,
        "settle_t": settle_t,
        "overshoot": max(0.0, overshoot),
        "ripple_pp": _ripple(history),
    }


def simulate_boost(initial_vbus: float = 12.0, duration_s: float = 0.05) -> dict:
    """Boost 模式：从 12V 拉到 24V"""
    state = DCDCState(vbus=initial_vbus, iL=0.0, vbat=12.0)
    # ramp 5ms 内 12V→24V → 0.24V/外环周期
    pi = DualLoopPI(vref=VBUS_BOOST, ramp_per_step=12.0 / 50)
    # ramp 起点为 12（已经稳定在 12V）
    pi.outer.setpoint = 12.0
    
    history = []
    t = 0.0
    settle_t = None
    while t < duration_s:
        duty = pi.update(state.vbus, state.iL)
        dcdc_step(state, duty, Mode.BOOST, R_LOAD_BOOST, TS_INNER)
        history.append((t, state.vbus, state.iL, duty))
        if settle_t is None and abs(state.vbus - VBUS_BOOST) < TOL_BOOST and t > 0.005:
            settle_t = t
        t += TS_INNER
    
    final_err = state.vbus - VBUS_BOOST
    overshoot = max((v - VBUS_BOOST for _, v, _, _ in history), default=0.0)
    return {
        "final_vbus": state.vbus,
        "final_err": final_err,
        "settle_t": settle_t,
        "overshoot": max(0.0, overshoot),
        "ripple_pp": _ripple(history),
    }


def simulate_mode_switch(duration_s: float = 0.1) -> dict:
    """0~50ms Buck，50ms 切换到 Boost。验证瞬态 < 50ms。"""
    state = DCDCState(vbus=0.0, iL=0.0, vbat=24.0)
    pi = DualLoopPI(vref=VBUS_BUCK)
    mode = Mode.BUCK
    
    history = []
    t = 0.0
    switch_t = None
    recover_t = None
    target = VBUS_BUCK
    
    while t < duration_s:
        # 50ms 时切换到 Boost
        if t >= 0.05 and mode == Mode.BUCK:
            mode = Mode.BOOST
            state.vbat = 12.0     # 切换电池侧
            target = VBUS_BOOST
            pi = DualLoopPI(vref=VBUS_BOOST)
            pi.reset_for_mode_switch(Mode.BOOST, 12.0, VBUS_BOOST)
            switch_t = t
        
        duty = pi.update(state.vbus, state.iL)
        r_load = R_LOAD_BUCK if mode == Mode.BUCK else R_LOAD_BOOST
        dcdc_step(state, duty, mode, r_load, TS_INNER)
        history.append((t, state.vbus, state.iL, duty, int(mode)))
        
        if (switch_t is not None and recover_t is None
            and abs(state.vbus - target) < TOL_BOOST
            and t > switch_t + 0.001):
            recover_t = t - switch_t
        t += TS_INNER
    
    return {
        "switch_t": switch_t,
        "recover_t": recover_t,
        "final_vbus": state.vbus,
        "final_err": state.vbus - target,
    }


def _ripple(history) -> float:
    """取最后 10% 的 vbus 范围作为纹波"""
    n = len(history)
    tail = history[int(n * 0.9):]
    if not tail: return 0.0
    vmax = max(v for _, v, _, _, *_ in tail)
    vmin = min(v for _, v, _, _, *_ in tail)
    return vmax - vmin


# ============================================================
#  主验证
# ============================================================
def run_validation():
    print("=" * 72)
    print(" 2026 模拟 双向 DC-DC 变换器 — 算法层 PC 验证")
    print("=" * 72)
    print(f" Vbat = {VBAT_NOMINAL}V, Vbus_buck = {VBUS_BUCK}V, Vbus_boost = {VBUS_BOOST}V")
    print(f" PWM = {PWM_FSW/1000:.0f} kHz, 双闭环 PI")
    print()

    # ---- 1. Buck 模式 ----
    print("【Buck 模式】24V → 12V，满载 5A")
    r = simulate_buck()
    print(f"  终态 Vbus  : {r['final_vbus']:.4f} V  (期望 {VBUS_BUCK})")
    print(f"  误差       : {r['final_err']:+.4f} V  "
          f"({'✓' if abs(r['final_err']) < TOL_BUCK else '✗'} 容差 ±{TOL_BUCK}V)")
    print(f"  超调       : {r['overshoot']:.3f} V")
    print(f"  收敛时间   : {r['settle_t']} s "
          f"({'✓' if r['settle_t'] and r['settle_t'] < 0.020 else '⚠'} 期望 < 20ms)")
    print(f"  稳态纹波   : {r['ripple_pp']:.4f} V (峰峰)")
    print()

    # ---- 2. Boost 模式 ----
    print("【Boost 模式】12V → 24V，满载 2.5A")
    r = simulate_boost()
    print(f"  终态 Vbus  : {r['final_vbus']:.4f} V  (期望 {VBUS_BOOST})")
    print(f"  误差       : {r['final_err']:+.4f} V  "
          f"({'✓' if abs(r['final_err']) < TOL_BOOST else '✗'} 容差 ±{TOL_BOOST}V)")
    print(f"  超调       : {r['overshoot']:.3f} V")
    print(f"  收敛时间   : {r['settle_t']} s "
          f"({'✓' if r['settle_t'] and r['settle_t'] < 0.020 else '⚠'} 期望 < 20ms)")
    print(f"  稳态纹波   : {r['ripple_pp']:.4f} V (峰峰)")
    print()

    # ---- 3. 模式切换 ----
    print("【模式切换】Buck → Boost")
    r = simulate_mode_switch()
    print(f"  切换时刻    : {r['switch_t']} s")
    print(f"  恢复时间    : {r['recover_t']} s "
          f"({'✓' if r['recover_t'] and r['recover_t'] < 0.050 else '⚠'} 期望 < 50ms)")
    print(f"  终态 Vbus   : {r['final_vbus']:.4f} V")
    print()

    print("=" * 72)
    print(" 结论：")
    print(f"   - Buck 稳态误差: {abs(simulate_buck()['final_err']):.4f}V (容差 ±0.1V)")
    print(f"   - Boost 稳态误差: {abs(simulate_boost()['final_err']):.4f}V (容差 ±0.2V)")
    print(f"   - 模式切换恢复: {simulate_mode_switch()['recover_t']}s (要求 < 50ms)")
    print(" → PI 参数仿真已就位，等 STM32G474/MSPM0 实测调参")
    print("=" * 72)


if __name__ == "__main__":
    run_validation()
