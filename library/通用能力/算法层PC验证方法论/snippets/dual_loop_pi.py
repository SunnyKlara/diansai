"""
dual_loop_pi.py
===============

双闭环 PI 控制器（电压外环 + 电流内环），含 ramp-up + anti-windup。
来自 2026 模拟双向 DC-DC 实战，可复用于：
  - 任何"恒压输出"的电源题（Buck / Boost / Buck-Boost / 单相逆变）
  - 电机调速（外环位置/速度，内环电流）
  - 任何需要"软启动 + 抗积分饱和"的控制系统

核心设计：
  - 外环周期慢（典型 10~100 倍内环周期）
  - 外环输出 = 内环 setpoint
  - ramp-up：setpoint 限幅变化 → 抑制超调
  - anti-windup：通过 PIDController 自动处理

复用：snippets/pid_controller.py
"""

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pid_controller import PIDController


class DualLoopPI:
    """双闭环 PI：外环慢，内环快。

    Args:
        vref:           最终目标值（电压）
        kp_v, ki_v:     外环 PI 参数
        kp_i, ki_i:     内环 PI 参数
        outer_period_ratio: 外环周期 = 内环周期 × ratio（典型 10）
        i_max:          内环 setpoint 上限（电流参考最大值）
        duty_min, duty_max: 内环输出限幅（PWM 占空比）
        ramp_per_outer_step: 每个外环周期 setpoint 推进多少（None=禁用 ramp-up）
        ts_inner:       内环周期（秒），用于离散化 Ki·∫
    """

    def __init__(self, vref: float,
                 kp_v: float, ki_v: float,
                 kp_i: float, ki_i: float,
                 outer_period_ratio: int = 10,
                 i_max: float = 100.0,
                 duty_min: float = 0.05, duty_max: float = 0.95,
                 ramp_per_outer_step: float = None,
                 ts_inner: float = 1e-5):
        self.vref_final = vref
        self.ramp = ramp_per_outer_step
        self.outer_period = outer_period_ratio
        self.ts_inner = ts_inner

        ts_outer = ts_inner * outer_period_ratio
        initial_setpoint = 0.0 if ramp_per_outer_step is not None else vref

        self.outer = PIDController(
            Kp=kp_v, Ki=ki_v * ts_outer, Kd=0.0,
            out_min=0.0, out_max=i_max,
            integral_max=i_max * 2.0,
        )
        self.outer.set_target(initial_setpoint)

        self.inner = PIDController(
            Kp=kp_i, Ki=ki_i * ts_inner, Kd=0.0,
            out_min=duty_min, out_max=duty_max,
            integral_max=10.0,
        )

        self.iref = 0.0
        self.counter = 0

    def update(self, v_meas: float, i_meas: float) -> float:
        """每个内环周期调用一次，返回 PWM 占空比。"""
        # 外环（每 N 次内环跑一次）
        if self.counter % self.outer_period == 0:
            if self.ramp is not None:
                self.outer.set_target_ramp(self.vref_final, self.ramp)
            self.iref = self.outer.compute(v_meas)
        self.counter += 1

        # 内环
        self.inner.set_target(self.iref)
        return self.inner.compute(i_meas)

    def reset(self):
        self.outer.reset()
        self.inner.reset()
        self.iref = 0.0
        self.counter = 0

    def reset_for_setpoint_change(self, new_vref: float, ff_duty: float = None):
        """跳变设定值（如模式切换）时，把内环积分重置为前馈占空比。"""
        self.vref_final = new_vref
        if ff_duty is not None:
            # 让内环积分给出 ff_duty
            self.inner.integral = ff_duty / max(self.inner.Ki, 1e-9)
        self.outer.reset()
        if self.ramp is not None:
            # 不从 0 重起，从当前实测值起 ramp
            self.outer.setpoint = self.outer.last_error  # 大致延续


def _example():
    """阶跃响应：双闭环 PI 把一阶惯性系统的输出推到 setpoint"""
    pi = DualLoopPI(
        vref=12.0,
        kp_v=0.8, ki_v=5000.0,
        kp_i=0.5, ki_i=8000.0,
        outer_period_ratio=10,
        i_max=15.0,
        duty_min=0.05, duty_max=0.95,
        ramp_per_outer_step=12.0 / 50,    # 5ms 推到 12V
        ts_inner=1e-5,
    )

    # 简化的一阶系统：vbus 跟踪 duty × 24V，时间常数 1ms
    vbus = 0.0
    iL = 0.0
    L = 22e-6
    C = 470e-6
    R = 2.4
    Vin = 24.0
    dt = 1e-5

    print(f"{'t/ms':>6} {'vbus':>8} {'iL':>8} {'duty':>6}")
    overshoot = 0.0
    settle_t = None
    for k in range(5000):
        t = k * dt
        duty = pi.update(vbus, iL)
        # 简化连续模型
        vL = duty * Vin - vbus - iL * 0.03
        iL += vL / L * dt
        if iL < 0: iL = 0
        vbus += (iL - vbus / R) / C * dt
        if vbus < 0: vbus = 0

        overshoot = max(overshoot, vbus - 12.0)
        if settle_t is None and abs(vbus - 12.0) < 0.1 and t > 0.005:
            settle_t = t

        if k % 500 == 0:
            print(f"{t*1000:>6.1f} {vbus:>8.3f} {iL:>8.3f} {duty:>6.3f}")

    print()
    print(f"超调      : {max(0.0, overshoot):.3f} V")
    print(f"稳定时间  : {settle_t} s")
    print(f"终态 vbus : {vbus:.4f} V")
    print(f"误差      : {abs(vbus - 12.0):.4f} V "
          f"({'✓' if abs(vbus - 12.0) < 0.1 else '✗'} 容差 ±0.1V)")


if __name__ == "__main__":
    _example()
