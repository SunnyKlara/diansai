"""
dcdc_simple_model.py
====================

DC-DC 变换器的简化连续平均模型（Buck / Boost / Buck-Boost）。
用于在 PC 上仿真 PI 闭环响应，避开开关瞬态细节。

来自 2026 模拟双向 DC-DC 实战。

理论：
  Buck:        Vout = D × Vin  
  Boost:       Vout = Vin / (1-D)  
  Buck-Boost:  Vout = D × Vin / (1-D)（同步，单电感）

考虑损耗后：实际占空比有 ±2~5% 的偏移（DCR + Rdson + 二极管压降）。
"""

import math
from dataclasses import dataclass
from enum import IntEnum


class DCDCTopo(IntEnum):
    BUCK = 0
    BOOST = 1
    BUCK_BOOST = 2     # 同步双向，作 Buck 用


@dataclass
class DCDCParams:
    L: float = 22e-6           # 电感（H）
    C: float = 470e-6          # 输出电容（F）
    R_dcr: float = 0.020       # 电感 DCR
    R_dson: float = 0.010      # MOS 导通电阻
    Vin: float = 24.0          # 输入电压
    R_load: float = 2.4        # 负载电阻


@dataclass
class DCDCState:
    vbus: float = 0.0   # 输出电压
    iL: float = 0.0     # 电感电流


def step_buck(state: DCDCState, params: DCDCParams, duty: float, dt: float):
    """Buck 模式平均模型：L · diL/dt = D·Vin - Vout - iL·(DCR+Rdson)"""
    vL = duty * params.Vin - state.vbus - state.iL * (params.R_dcr + params.R_dson)
    di_dt = vL / params.L
    i_out = state.vbus / params.R_load
    dv_dt = (state.iL - i_out) / params.C

    state.iL += di_dt * dt
    state.vbus += dv_dt * dt
    if state.vbus < 0: state.vbus = 0
    if state.iL < 0: state.iL = 0   # 不允许反向（同步整流后可改）


def step_boost(state: DCDCState, params: DCDCParams, duty: float, dt: float):
    """Boost 模式平均模型：L · diL/dt = Vin - (1-D)·Vout - iL·(DCR+Rdson)"""
    vL = params.Vin - (1.0 - duty) * state.vbus - state.iL * (params.R_dcr + params.R_dson)
    di_dt = vL / params.L
    i_out = state.vbus / params.R_load
    dv_dt = ((1.0 - duty) * state.iL - i_out) / params.C

    state.iL += di_dt * dt
    state.vbus += dv_dt * dt
    if state.vbus < 0: state.vbus = 0
    if state.iL < 0: state.iL = 0


def calc_steady_state_duty(topo: DCDCTopo, vin: float, vout_target: float) -> float:
    """计算稳态占空比（用于初值或 PI 前馈）"""
    if topo == DCDCTopo.BUCK:
        if vin <= 0: return 0.0
        return min(0.95, max(0.05, vout_target / vin))
    elif topo == DCDCTopo.BOOST:
        if vout_target <= 0: return 0.0
        return min(0.95, max(0.05, 1.0 - vin / vout_target))
    return 0.5


def calc_efficiency(state: DCDCState, params: DCDCParams) -> float:
    """估算瞬时效率（不含开关损耗，仅导通 + DCR）"""
    p_out = state.vbus * (state.vbus / params.R_load)
    p_loss_cond = state.iL ** 2 * (params.R_dcr + params.R_dson)
    p_in = p_out + p_loss_cond
    if p_in <= 0: return 0.0
    return p_out / p_in


def _example():
    """开环 Buck：D=0.5 在 24V 输入下应稳态到 12V"""
    params = DCDCParams(Vin=24.0, R_load=2.4)
    state = DCDCState(vbus=0.0, iL=0.0)
    dt = 1e-5

    # 跑 5ms
    print(f"{'t/ms':>6} {'vbus':>8} {'iL':>8}")
    for k in range(500):
        step_buck(state, params, duty=0.5, dt=dt)
        if k % 50 == 0:
            print(f"{k*dt*1000:>6.2f} {state.vbus:>8.4f} {state.iL:>8.4f}")

    print()
    print(f"稳态 vbus = {state.vbus:.4f} V (理论 D·Vin = 12.000)")
    print(f"稳态 iL   = {state.iL:.4f} A (理论 = vbus/R = 5.000)")
    eff = calc_efficiency(state, params)
    print(f"效率      = {eff*100:.2f} %")

    # 计算前馈占空比
    d_ff = calc_steady_state_duty(DCDCTopo.BUCK, 24.0, 12.0)
    print(f"前馈占空比（理论 0.5）= {d_ff:.4f}")


if __name__ == "__main__":
    _example()
