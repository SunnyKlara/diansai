"""
cal_helper.py
=============

2025 A 能量回馈变流器 — PC 端调试 / 调参助手（与 STM32G474 通过 UART 对话）。

工作流：
  python cal_helper.py --port COM3 --stat            # 看实时电压/电流/调制比
  python cal_helper.py --port COM3 --tune-vloop --kp 0.005 --ki 0.005
  python cal_helper.py --port COM3 --set-freq 60     # 切输出频率
  python cal_helper.py --port COM3 --enable-sr       # 启用同步整流
  python cal_helper.py --port COM3 --simulate --vdc 53 --kp 0.005 --ki 0.005

UART 协议：
  STAT          → "vline=XX.X iout=XX.XX vdc=XX.X mod=X.XXX state=XX"
  PID VLOOP KP=X KI=X
  SET FREQ=XX
  SET MOD=X.XX        （手动调制比，调试用）
  SR ON / SR OFF      （同步整流）
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


def do_tune_vloop(ser, kp: float, ki: float):
    print(cmd(ser, f"PID VLOOP KP={kp} KI={ki}"))


def do_set_freq(ser, freq: float):
    print(cmd(ser, f"SET FREQ={freq}"))


def do_set_mod(ser, mod: float):
    """手动设置调制比（开环测试用）"""
    print(cmd(ser, f"SET MOD={mod}"))


def do_enable_sr(ser):
    print(cmd(ser, "SR ON"))


def do_disable_sr(ser):
    print(cmd(ser, "SR OFF"))


def do_simulate(vdc: float, kp: float, ki: float):
    """用 algo_reference 仿真给定母线电压 + PI 参数下的稳态"""
    import algo_reference as alg_mod
    r = alg.simulate_voltage_loop(vdc=vdc, ki_override=ki)
    print(f"PC 仿真（Vdc={vdc}, Ki={ki}）：")
    print(f"  终态 Vline   : {r['final_vrms']:.3f} V")
    print(f"  残差         : {r['final_err_v']:+.4f} V")
    print(f"  超调         : {r['overshoot_v']:.3f} V")
    print(f"  稳定时间     : {r['settle_time_s']} s")
    
    # 给现场建议
    if abs(r['final_err_v']) > 0.25:
        print()
        print("⚠️  残差超容差。检查清单（按顺序）：")
        print(f"   1. 母线电压是否够高？理论上限 = m_max × Vdc / √2 = {0.95 * vdc / 1.414:.2f} V")
        if 0.95 * vdc / 1.414 < 33.0:
            print(f"      → 母线 {vdc}V 撞顶，建议升到 ≥ 50V")
        print(f"   2. 调制比限幅 m_max = 0.95，现在最高 {r['final_vrms'] * 1.414 / vdc:.3f}")
        print(f"   3. Ki 是否够大让积分追赶稳态误差？")


def do_reset(ser):
    print(cmd(ser, "RST"))


def main():
    ap = argparse.ArgumentParser(description="2025 A 能量回馈变流器 PC 调试助手")
    ap.add_argument("--port", help="串口号")
    ap.add_argument("--baud", type=int, default=115200)

    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--stat", action="store_true")
    g.add_argument("--tune-vloop", action="store_true", help="调整电压外环 PI")
    g.add_argument("--set-freq", type=float, metavar="HZ", help="设置输出频率")
    g.add_argument("--set-mod", type=float, metavar="M", help="手动设置调制比（开环）")
    g.add_argument("--enable-sr", action="store_true", help="启用同步整流")
    g.add_argument("--disable-sr", action="store_true", help="禁用同步整流（用 body diode）")
    g.add_argument("--simulate", action="store_true", help="PC 仿真给定母线 + PI（无需串口）")
    g.add_argument("--reset", action="store_true")

    ap.add_argument("--kp", type=float, default=0.005)
    ap.add_argument("--ki", type=float, default=0.0001)
    ap.add_argument("--vdc", type=float, default=48.0)
    args = ap.parse_args()

    if args.simulate:
        do_simulate(args.vdc, args.kp, args.ki); return

    if not args.port:
        print("ERR: 该子命令需要 --port", file=sys.stderr); sys.exit(2)

    ser = open_serial(args.port, args.baud)
    try:
        if args.stat:                 do_stat(ser)
        elif args.tune_vloop:         do_tune_vloop(ser, args.kp, args.ki)
        elif args.set_freq is not None: do_set_freq(ser, args.set_freq)
        elif args.set_mod is not None:  do_set_mod(ser, args.set_mod)
        elif args.enable_sr:          do_enable_sr(ser)
        elif args.disable_sr:         do_disable_sr(ser)
        elif args.reset:              do_reset(ser)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
