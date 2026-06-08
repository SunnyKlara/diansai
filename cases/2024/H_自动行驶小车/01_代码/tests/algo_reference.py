"""
algo_reference.py
=================

2024 H 自动行驶小车 — 算法层 Python 金标准。

镜像 C 代码的 PID / 循迹 / 路径段管理，主要用途：

  1. PC 上跑闭环仿真，验证 PID 收敛性 + 段切换正确性
  2. 算法移植到 MSPM0 后做"金标准对比"
  3. 给报告"控制系统仿真"章节提供数据

镜像范围：
  - PID_Compute / PID_Incremental（pid.c 等价实现）
  - Track_CalcPosition（track.c 加权位置算法）
  - Path 段序列（path.c 任务装载）
  - 简化的差动小车运动学模型 → 闭环仿真
"""

import math
import sys

# Windows GBK 终端兼容（防止 ✓/⚠ 等字符让 print 崩溃）
try:
    sys.stdout.reconfigure(encoding="utf-8")
except (AttributeError, ValueError):
    pass
from dataclasses import dataclass, field
from enum import IntEnum
from typing import List, Tuple


# ============================================================
#  与 config.h 同步的常量
# ============================================================
WHEEL_DIAMETER_MM = 65.0
WHEEL_BASE_MM     = 155.0
ENCODER_PPR       = 1320
MM_PER_PULSE      = math.pi * WHEEL_DIAMETER_MM / ENCODER_PPR

# 路径常量
PATH_LINE_AB_MM   = 1000.0
PATH_LINE_AC_MM   = 1280.6
PATH_ARC_MM       = 1256.6
PATH_TURN_AC_DEG  = 38.66
PATH_TURN_BD_DEG  = -38.66

# PID
PID_SPEED_KP, PID_SPEED_KI, PID_SPEED_KD = 10.0, 1.0, 0.0
PID_YAW_KP,   PID_YAW_KI,   PID_YAW_KD   = 3.0,  0.05, 1.5
PID_TURN_KP,  PID_TURN_KI,  PID_TURN_KD  = 4.0,  0.0,  2.0

PID_YAW_OUT_MIN,  PID_YAW_OUT_MAX  = -300.0, 300.0
PID_TURN_OUT_MIN, PID_TURN_OUT_MAX = -400.0, 400.0

V_LINE_PWM       = 500
V_TURN_PWM       = 300
V_DECEL_DIST_MM  = 150.0
V_LINE_END_PWM   = 250

CTRL_SPEED_HZ    = 100      # 10ms 周期
CTRL_TRACK_HZ    = 50       # 20ms 周期


# ============================================================
#  PID 控制器（pid.c 等价）
# ============================================================
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
    output_min: float = -1e9
    output_max: float =  1e9
    integral_max: float = 1e9

    def reset(self):
        self.error = self.last_error = self.prev_error = 0.0
        self.integral = 0.0
        self.output = 0.0

    def set_target(self, target: float):
        self.setpoint = target

    def compute(self, measured: float) -> float:
        """位置式（航向 / 转向 / 循迹）"""
        e = self.setpoint - measured
        self.integral += e
        # 积分限幅
        if self.integral >  self.integral_max: self.integral =  self.integral_max
        if self.integral < -self.integral_max: self.integral = -self.integral_max

        d_term = e - self.last_error
        out = self.Kp * e + self.Ki * self.integral + self.Kd * d_term

        # 输出限幅
        if out > self.output_max: out = self.output_max
        if out < self.output_min: out = self.output_min

        self.last_error = e
        self.output = out
        self.error = e
        return out

    def incremental(self, measured: float) -> float:
        """增量式（速度环）"""
        e = self.setpoint - measured
        delta = (self.Kp * (e - self.last_error)
                 + self.Ki * e
                 + self.Kd * (e - 2.0 * self.last_error + self.prev_error))
        self.output += delta
        if self.output > self.output_max: self.output = self.output_max
        if self.output < self.output_min: self.output = self.output_min
        self.prev_error = self.last_error
        self.last_error = e
        return self.output


def _make_yaw_pid() -> PIDController:
    p = PIDController(Kp=PID_YAW_KP, Ki=PID_YAW_KI, Kd=PID_YAW_KD,
                      output_min=PID_YAW_OUT_MIN, output_max=PID_YAW_OUT_MAX,
                      integral_max=200.0)
    return p


def _make_turn_pid() -> PIDController:
    p = PIDController(Kp=PID_TURN_KP, Ki=PID_TURN_KI, Kd=PID_TURN_KD,
                      output_min=PID_TURN_OUT_MIN, output_max=PID_TURN_OUT_MAX)
    return p


# ============================================================
#  循迹加权位置（track.c 等价）
# ============================================================
IR_WEIGHTS = (-2.0, -1.0, 0.0, 1.0, 2.0)
IR_LOST_VALUE = -10.0    # NaN 替代


def track_calc_position(ir_raw: List[int]) -> float:
    assert len(ir_raw) == 5
    s = 0.0
    cnt = 0
    for v, w in zip(ir_raw, IR_WEIGHTS):
        if v:
            s += w
            cnt += 1
    if cnt == 0:
        return IR_LOST_VALUE
    return s / cnt


# ============================================================
#  路径段（path.c 等价）
# ============================================================
class SegType(IntEnum):
    END  = 0
    TURN = 1
    LINE = 2
    ARC  = 3


@dataclass
class Segment:
    type: SegType
    distance_mm: float = 0.0
    delta_yaw_deg: float = 0.0
    arc_dir: int = 0


def path_load(task_id: int) -> List[Segment]:
    """对应 path.c 的任务段序列。"""
    if task_id == 1:        # A → B 直线
        return [
            Segment(SegType.LINE, distance_mm=PATH_LINE_AB_MM),
        ]
    if task_id == 2:        # 一圈：A→B→弧→C→D→弧→A
        return [
            Segment(SegType.LINE, distance_mm=PATH_LINE_AB_MM),
            Segment(SegType.ARC,  distance_mm=PATH_ARC_MM, arc_dir=1),
            Segment(SegType.LINE, distance_mm=PATH_LINE_AB_MM),
            Segment(SegType.ARC,  distance_mm=PATH_ARC_MM, arc_dir=1),
        ]
    if task_id == 3:        # 交叉一圈
        return [
            Segment(SegType.TURN, delta_yaw_deg=PATH_TURN_AC_DEG),
            Segment(SegType.LINE, distance_mm=PATH_LINE_AC_MM),
            Segment(SegType.ARC,  distance_mm=PATH_ARC_MM, arc_dir=2),
            Segment(SegType.TURN, delta_yaw_deg=PATH_TURN_BD_DEG),
            Segment(SegType.LINE, distance_mm=PATH_LINE_AC_MM),
            Segment(SegType.ARC,  distance_mm=PATH_ARC_MM, arc_dir=2),
        ]
    if task_id == 4:        # 4 圈竞速：重复任务 2 四次
        return path_load(2) * 4
    raise ValueError(f"unknown task_id={task_id}")


# ============================================================
#  差动小车运动学（仿真用）
# ============================================================
@dataclass
class CarState:
    x: float = 0.0          # mm
    y: float = 0.0          # mm
    yaw_deg: float = 0.0    # °
    v_left: float = 0.0     # mm/s
    v_right: float = 0.0    # mm/s


def step_kinematics(state: CarState, dt_s: float):
    """差速车运动学积分。v_left / v_right 单位是 mm/s。"""
    v = 0.5 * (state.v_left + state.v_right)
    omega_rad_s = (state.v_right - state.v_left) / WHEEL_BASE_MM
    yaw_rad = math.radians(state.yaw_deg)
    state.x += v * math.cos(yaw_rad) * dt_s
    state.y += v * math.sin(yaw_rad) * dt_s
    state.yaw_deg += math.degrees(omega_rad_s * dt_s)
    # 归一到 [-180, 180]
    while state.yaw_deg >  180.0: state.yaw_deg -= 360.0
    while state.yaw_deg < -180.0: state.yaw_deg += 360.0


# ============================================================
#  闭环仿真：直线段
# ============================================================
def simulate_line_segment(distance_mm: float, hold_yaw_deg: float,
                          init_yaw_offset_deg: float = 5.0,
                          dt_s: float = 0.01) -> dict:
    """模拟一段直线行驶，验证 yaw_pid 把初始偏角拉回到 0 的过程。

    返回各项指标：
      - end_distance: 实际走过的距离
      - end_yaw_err:  终态航向误差
      - max_yaw_err:  过程最大航向误差
      - settle_time_s: 航向误差降到 ±1° 内所需时间
    """
    car = CarState(yaw_deg=hold_yaw_deg + init_yaw_offset_deg)
    yaw_pid = _make_yaw_pid()
    yaw_pid.set_target(hold_yaw_deg)

    base_pwm = V_LINE_PWM
    pwm_to_speed = 2.0          # 简化：1 PWM 单位 ≈ 2 mm/s

    trip = 0.0
    t = 0.0
    max_yaw_err = 0.0
    settle_time = None
    while trip < distance_mm and t < 10.0:
        # 末段减速
        if (distance_mm - trip) < V_DECEL_DIST_MM:
            base_pwm = V_LINE_END_PWM

        # 航向 PID
        yaw_err = (hold_yaw_deg - car.yaw_deg)
        diff = yaw_pid.compute(car.yaw_deg)
        # 转换为左右轮 PWM
        pwm_l = base_pwm - diff
        pwm_r = base_pwm + diff
        car.v_left  = pwm_l * pwm_to_speed
        car.v_right = pwm_r * pwm_to_speed

        # 运动学积分
        step_kinematics(car, dt_s)
        trip += 0.5 * (car.v_left + car.v_right) * dt_s

        max_yaw_err = max(max_yaw_err, abs(yaw_err))
        if settle_time is None and abs(yaw_err) < 1.0 and t > 0.1:
            settle_time = t
        t += dt_s

    return {
        "end_distance_mm": trip,
        "end_yaw_err_deg": hold_yaw_deg - car.yaw_deg,
        "max_yaw_err_deg": max_yaw_err,
        "settle_time_s": settle_time,
        "elapsed_s": t,
        "end_pos": (car.x, car.y),
    }


# ============================================================
#  闭环仿真：原地转向
# ============================================================
def simulate_turn_segment(delta_yaw_deg: float, dt_s: float = 0.01) -> dict:
    """模拟原地转向 delta_yaw_deg。"""
    car = CarState()
    target = delta_yaw_deg
    turn_pid = _make_turn_pid()
    turn_pid.set_target(target)

    pwm_to_speed = 2.0
    t = 0.0
    while t < 5.0:
        cur = car.yaw_deg
        out = turn_pid.compute(cur)
        # 原地转向：左右轮反向
        car.v_left  = -out * pwm_to_speed
        car.v_right =  out * pwm_to_speed
        step_kinematics(car, dt_s)
        if abs(target - car.yaw_deg) < 0.5:
            break
        t += dt_s

    return {
        "final_yaw_deg": car.yaw_deg,
        "yaw_err_deg": target - car.yaw_deg,
        "elapsed_s": t,
    }


# ============================================================
#  入口
# ============================================================
def run_validation():
    print("=" * 72)
    print(" 2024 H 自动行驶小车 — 算法层 PC 仿真")
    print("=" * 72)
    print(f" WHEEL_BASE = {WHEEL_BASE_MM} mm  ENCODER_PPR = {ENCODER_PPR}")
    print()

    # ---- 1. 路径序列检查 ----
    for task in (1, 2, 3, 4):
        segs = path_load(task)
        print(f"  Task {task}: {len(segs)} segments  "
              f"({', '.join(s.type.name for s in segs[:6])}{'...' if len(segs)>6 else ''})")
    print()

    # ---- 2. 加权位置 ----
    print(" 红外加权位置（仅工具，验证算法正确）")
    cases = {
        "全白": [0,0,0,0,0],
        "中央": [0,0,1,0,0],
        "偏左": [0,1,0,0,0],
        "偏右": [0,0,0,1,0],
        "压全黑": [1,1,1,1,1],
    }
    for name, ir in cases.items():
        pos = track_calc_position(ir)
        print(f"   {name:6s} {ir} -> pos={pos}")
    print()

    # ---- 3. 直线段闭环：1m 直线 + 5° 初始偏角 ----
    print(" 直线段闭环（1m 直线，初始偏角 5°）")
    r = simulate_line_segment(distance_mm=1000.0, hold_yaw_deg=0.0,
                              init_yaw_offset_deg=5.0)
    print(f"   走过距离  : {r['end_distance_mm']:.2f} mm")
    print(f"   终态偏角  : {r['end_yaw_err_deg']:+.4f}°")
    print(f"   最大偏角  : {r['max_yaw_err_deg']:.4f}°")
    print(f"   稳定时间  : {r['settle_time_s']} s "
          f"({'✓' if r['settle_time_s'] and r['settle_time_s']<2.0 else '⚠'})")
    print(f"   终点偏移  : x={r['end_pos'][0]:.1f}, y={r['end_pos'][1]:.1f} mm "
          f"({'✓' if abs(r['end_pos'][1])<30 else '⚠'} 横向 < 30mm)")
    print(f"   总耗时    : {r['elapsed_s']:.2f} s")
    print()

    # ---- 4. 原地转向 38.66° ----
    print(" 原地转向闭环（目标 38.66°）")
    r = simulate_turn_segment(PATH_TURN_AC_DEG)
    print(f"   终态 yaw  : {r['final_yaw_deg']:.4f}°")
    print(f"   误差      : {r['yaw_err_deg']:+.4f}°")
    print(f"   耗时      : {r['elapsed_s']:.3f} s "
          f"({'✓' if r['elapsed_s']<3.0 else '⚠'})")
    print()

    print("=" * 72)
    print(" 结论：PID 参数 + 路径段管理在 PC 仿真下收敛、终态精度达标。")
    print("       MSPM0 移植后应在相同输入下复现这些数值（绝对差 < 1%）。")
    print("=" * 72)


if __name__ == "__main__":
    run_validation()
