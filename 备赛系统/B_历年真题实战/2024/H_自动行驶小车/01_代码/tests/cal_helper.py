"""
cal_helper.py
=============

2024 H 自动行驶小车 — PC 端调试 / 调参助手（与 MSPM0 通过 UART 对话）。

工作流：
  python cal_helper.py --port COM3 --stat        # 看实时车况
  python cal_helper.py --port COM3 --tune-pid yaw --kp 3.5 --ki 0.1 --kd 1.5
  python cal_helper.py --port COM3 --task 2      # 启动任务 2（一圈）
  python cal_helper.py --port COM3 --calib-imu   # 静态 200 次平均零偏
  python cal_helper.py --port COM3 --reset

UART 协议（装置端实现要点）：
  STAT      → "yaw=XX.X v_l=XXX v_r=XXX trip=XXXX state=XX seg=XX"
  PID <env> KP=X KI=X KD=X    （env: yaw / track / turn / speed）
  TASK <id>
  CALIB IMU
  RST
"""

import argparse
import sys

# Windows GBK 终端兼容（防止 ✓/⚠ 等字符让 print 崩溃）
try:
    sys.stdout.reconfigure(encoding="utf-8")
except (AttributeError, ValueError):
    pass
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import algo_reference as alg


def open_serial(port: str, baud: int = 115200):
    try:
        import serial
    except ImportError:
        print("ERR: 缺少 pyserial，请 `pip install pyserial`", file=sys.stderr)
        sys.exit(2)
    return serial.Serial(port, baudrate=baud, timeout=2)


def cmd(ser, line: str, wait_s: float = 0.5) -> str:
    ser.reset_input_buffer()
    ser.write((line + "\r\n").encode())
    ser.flush()
    time.sleep(wait_s)
    return ser.read_all().decode(errors="replace")


def do_stat(ser):
    print(cmd(ser, "STAT"))


def do_log(ser, duration_s: float):
    """连续监听 LOG，把 yaw / 速度时序 dump 到 CSV"""
    print(cmd(ser, "LOG ON"))
    rows = []
    t0 = time.time()
    while time.time() - t0 < duration_s:
        line = ser.readline().decode(errors="replace").strip()
        if line:
            print(line)
            rows.append(line)
    print(cmd(ser, "LOG OFF"))
    csv_path = Path("car_log.csv")
    csv_path.write_text("\n".join(rows), encoding="utf-8")
    print(f"\n→ 写入 {csv_path}")


def do_tune_pid(ser, env: str, kp: float, ki: float, kd: float):
    """调整 PID 参数（env: yaw / track / turn / speed）"""
    resp = cmd(ser, f"PID {env.upper()} KP={kp} KI={ki} KD={kd}")
    print(resp)


def do_task(ser, task_id: int):
    """启动指定任务"""
    print(cmd(ser, f"TASK {task_id}"))


def do_calib_imu(ser):
    """触发 IMU 零偏标定"""
    print(cmd(ser, "CALIB IMU"))
    print("（保持小车静止 5 秒）")
    time.sleep(5)
    print(cmd(ser, "STAT"))


def do_simulate(env: str, kp: float, ki: float, kd: float):
    """PC 端用 algo_reference 仿真给定 PID 参数的响应"""
    if env == "yaw":
        # 用临时 PID 仿真直线段
        from dataclasses import replace
        pid = alg._make_yaw_pid()
        pid.Kp, pid.Ki, pid.Kd = kp, ki, kd
        # 直接在 simulate_line_segment 内部用更通用的方式
        import algo_reference as alg_mod
        alg_mod.PID_YAW_KP, alg_mod.PID_YAW_KI, alg_mod.PID_YAW_KD = kp, ki, kd
        r = alg.simulate_line_segment(distance_mm=1000.0, hold_yaw_deg=0.0,
                                      init_yaw_offset_deg=5.0)
        print(f"PC 仿真（kp={kp}, ki={ki}, kd={kd}）：")
        print(f"  最大偏角     : {r['max_yaw_err_deg']:.4f}°")
        print(f"  稳定时间     : {r['settle_time_s']} s")
        print(f"  终点横向偏移 : {r['end_pos'][1]:.2f} mm")
        print(f"  总耗时       : {r['elapsed_s']:.2f} s")
    elif env == "turn":
        import algo_reference as alg_mod
        alg_mod.PID_TURN_KP, alg_mod.PID_TURN_KI, alg_mod.PID_TURN_KD = kp, ki, kd
        r = alg.simulate_turn_segment(38.66)
        print(f"PC 仿真（kp={kp}, ki={ki}, kd={kd}）：")
        print(f"  终态 yaw     : {r['final_yaw_deg']:.4f}°")
        print(f"  误差         : {r['yaw_err_deg']:+.4f}°")
        print(f"  耗时         : {r['elapsed_s']:.3f} s")
    else:
        print(f"暂不支持 env={env}（支持: yaw, turn）")


def do_reset(ser):
    print(cmd(ser, "RST"))


def main():
    ap = argparse.ArgumentParser(description="2024 H 自动行驶小车 PC 调试助手")
    ap.add_argument("--port", help="串口号")
    ap.add_argument("--baud", type=int, default=115200)

    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--stat", action="store_true", help="读取实时车况")
    g.add_argument("--log", type=float, metavar="SEC", help="监听 N 秒 LOG 写 CSV")
    g.add_argument("--tune-pid", metavar="ENV", help="调 PID 参数（yaw/track/turn/speed）")
    g.add_argument("--task", type=int, metavar="ID", help="启动任务（1~4）")
    g.add_argument("--calib-imu", action="store_true", help="IMU 零偏标定")
    g.add_argument("--simulate", metavar="ENV", help="PC 仿真给定 PID 参数（yaw/turn，无需串口）")
    g.add_argument("--reset", action="store_true")

    ap.add_argument("--kp", type=float, default=0.0)
    ap.add_argument("--ki", type=float, default=0.0)
    ap.add_argument("--kd", type=float, default=0.0)

    args = ap.parse_args()

    # 仿真模式不需要串口
    if args.simulate:
        do_simulate(args.simulate, args.kp, args.ki, args.kd)
        return

    if not args.port:
        print("ERR: 该子命令需要 --port", file=sys.stderr); sys.exit(2)

    ser = open_serial(args.port, args.baud)
    try:
        if args.stat:               do_stat(ser)
        elif args.log is not None:  do_log(ser, args.log)
        elif args.tune_pid:         do_tune_pid(ser, args.tune_pid, args.kp, args.ki, args.kd)
        elif args.task is not None: do_task(ser, args.task)
        elif args.calib_imu:        do_calib_imu(ser)
        elif args.reset:            do_reset(ser)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
