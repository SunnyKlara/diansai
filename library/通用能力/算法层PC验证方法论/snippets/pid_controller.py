"""
pid_controller.py
=================

PID 控制器 Python 参考实现（与 2024 H 的 C 代码 1:1 等价）。

用法：
    from pid_controller import PIDController

    pid = PIDController(Kp=3.0, Ki=0.05, Kd=1.5,
                        out_min=-300, out_max=300, integral_max=200)
    pid.set_target(0.0)              # 设定值
    output = pid.compute(measured)    # 位置式（航向 / 循迹 / 转向）
    output = pid.incremental(measured) # 增量式（速度环）
    pid.reset()                       # 故障 / 停机时
"""

from dataclasses import dataclass


@dataclass
class PIDController:
    Kp: float = 0.0
    Ki: float = 0.0
    Kd: float = 0.0
    setpoint: float = 0.0
    error: float = 0.0
    last_error: float = 0.0
    prev_error: float = 0.0
    integral: float = 0.0
    output: float = 0.0
    out_min: float = -1e9
    out_max: float = 1e9
    integral_max: float = 1e9

    def reset(self):
        """清状态，不改 Kp/Ki/Kd。"""
        self.error = self.last_error = self.prev_error = 0.0
        self.integral = 0.0
        self.output = 0.0

    def set_target(self, target: float):
        self.setpoint = target

    def set_target_ramp(self, target: float, ramp_per_step: float):
        """ramp-up：每次推 setpoint 一小步，限制变化率。
        在主循环中按周期调用，target 是最终目标。
        """
        diff = target - self.setpoint
        if abs(diff) <= ramp_per_step:
            self.setpoint = target
        else:
            self.setpoint += ramp_per_step if diff > 0 else -ramp_per_step

    def set_params(self, kp: float, ki: float, kd: float):
        self.Kp, self.Ki, self.Kd = kp, ki, kd

    def compute(self, measured: float) -> float:
        """位置式 PID：output = Kp·e + Ki·∫e + Kd·de/dt
        
        含 anti-windup：当输出饱和时，停止累积同向误差到积分项，防止积分饱和。
        """
        e = self.setpoint - measured
        
        # Anti-windup：先临时积分，看输出是否饱和
        new_integral = self.integral + e
        # 输出限幅
        if new_integral > self.integral_max: new_integral = self.integral_max
        if new_integral < -self.integral_max: new_integral = -self.integral_max

        d_term = e - self.last_error
        out = self.Kp * e + self.Ki * new_integral + self.Kd * d_term

        # 输出限幅 + back-calculation：如果饱和且 e 同号，不累积
        if out > self.out_max:
            out = self.out_max
            # 只在误差与饱和方向相反时才累积（让积分回流）
            if e < 0:
                self.integral = new_integral
        elif out < self.out_min:
            out = self.out_min
            if e > 0:
                self.integral = new_integral
        else:
            self.integral = new_integral

        self.last_error = e
        self.output = out
        self.error = e
        return out

    def incremental(self, measured: float) -> float:
        """增量式 PID：Δoutput = Kp·(e-e1) + Ki·e + Kd·(e-2·e1+e2)"""
        e = self.setpoint - measured
        delta = (self.Kp * (e - self.last_error)
                 + self.Ki * e
                 + self.Kd * (e - 2.0 * self.last_error + self.prev_error))
        self.output += delta
        if self.output > self.out_max: self.output = self.out_max
        if self.output < self.out_min: self.output = self.out_min
        self.prev_error = self.last_error
        self.last_error = e
        return self.output


def _example():
    """位置式 PID：跟踪阶跃响应 0 → 100。"""
    pid = PIDController(Kp=2.0, Ki=0.1, Kd=0.5,
                        out_min=-1000, out_max=1000)
    pid.set_target(100.0)

    measured = 0.0
    print(f"{'step':>4} {'measured':>10} {'output':>10}")
    for k in range(20):
        out = pid.compute(measured)
        # 一阶惯性模型
        measured += 0.1 * (out - measured)
        print(f"{k:>4} {measured:>10.4f} {out:>10.4f}")


if __name__ == "__main__":
    _example()
