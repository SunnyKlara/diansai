"""
cal_helper.py
=============

2023 A 单相逆变器并联 — PC 端调试 / 调参助手（与 STM32G4 通过 UART 对话）。

工作流：
  python cal_helper.py --port COM3 --stat         # 看一次实时数据
  python cal_helper.py --port COM3 --tune-pi --kp 0.020 --ki 0.005
  python cal_helper.py --port COM3 --set-vref 24.0
  python cal_helper.py --port COM3 --start
  python cal_helper.py --port COM3 --stop
  python cal_helper.py --port COM3 --calib       # 输入标准 PA 测量值，校准 V/I 增益
  python cal_helper.py --port COM3 --simulate    # PC 仿真当前 PI 参数（不需串口）

UART 协议（装置端实现要点）：
  STAT          → "vout=24.00 iout=2.01 m=0.703 vdc=48.0 fout=50.00 state=RUN err=0"
  PI KP=X KI=X
  VREF X
  START / STOP
  CAL V=X I=X
  RST
"""

import argparse
import sys
import time
from pathlib import Path

# Windows GBK 终端兼容
try:
    sys.stdout.reconfigure(encoding="utf-8")
except (AttributeError, ValueError):
    pass

sys.path.insert(0, str(Path(__file__).parent))
import algo_reference as alg


# ============================================================
#  低层串口
# ============================================================
def open_serial(port: str, baud: int = 115200):
    try:
        import serial  # type: ignore
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


# ============================================================
#  子命令
# ============================================================
def do_stat(ser):
    print(cmd(ser, "STAT"))


def do_tune_pi(ser, kp: float, ki: float):
    print(cmd(ser, f"PI KP={kp} KI={ki}"))


def do_set_vref(ser, vref: float):
    print(cmd(ser, f"VREF {vref}"))


def do_start(ser):
    print(cmd(ser, "START"))


def do_stop(ser):
    print(cmd(ser, "STOP"))


def do_calib(ser):
    """交互式校准 V/I 增益。"""
    print("[1] 启动逆变器并接入标准 PA + 已知负载")
    print("    建议：Vref = 24V，纯阻 12Ω")
    input("    准备好后按回车继续...")

    print("[2] 读装置当前测量值")
    raw = cmd(ser, "STAT")
    print("    " + raw.strip())

    v_real = float(input("[3] 输入标准 PA 测得的 Vrms（V）: ").strip())
    i_real = float(input("[4] 输入标准 PA 测得的 Irms（A）: ").strip())

    print("[5] 发送校准命令")
    resp = cmd(ser, f"CAL V={v_real} I={i_real}")
    print("    " + resp.strip())

    print("[6] 复读验证")
    print("    " + cmd(ser, "STAT").strip())


def do_log(ser, duration_s: float, csv_path: str = "inv_log.csv"):
    """监听 LOG 输出，dump 到 CSV"""
    print(cmd(ser, "LOG ON"))
    rows = []
    t0 = time.time()
    while time.time() - t0 < duration_s:
        line = ser.readline().decode(errors="replace").strip()
        if line:
            print(line)
            rows.append(line)
    print(cmd(ser, "LOG OFF"))
    Path(csv_path).write_text("\n".join(rows), encoding="utf-8")
    print(f"\n→ 写入 {csv_path}")


def do_simulate(kp: float, ki: float, vref: float):
    """PC 端用 algo_reference 仿真给定 PI 参数的响应。"""
    # 临时改 algo_reference 的全局
    alg.VLOOP_KP = kp
    alg.VLOOP_KI = ki
    r = alg.simulate_softstart(load_ohm=12.0, duration_s=1.5)
    print(f"PC 仿真（Kp={kp}, Ki={ki}, Vref={vref}）:")
    print(f"  最终 Vrms     : {r.vout_rms[-1]:.4f}")
    print(f"  最终调制比    : {r.mod_index[-1]:.4f}")
    print(f"  收敛时间      : {r.settled_at_s*1000:.1f} ms" if r.settled_at_s > 0
          else "  收敛时间      : 未收敛")
    err_pct = abs(r.vout_rms[-1] - alg.VOUT_REF_V) / alg.VOUT_REF_V * 100
    print(f"  稳态误差      : {err_pct:.4f} %")
    print(f"  → {'✓ 满足 0.2%' if err_pct < 0.21 else '⚠ 不满足，调大 Ki 或 Kp'}")


def do_reset(ser):
    print(cmd(ser, "RST"))


# ============================================================
#  入口
# ============================================================
def main():
    ap = argparse.ArgumentParser(description="2023 A 单相逆变器 PC 调试助手")
    ap.add_argument("--port", help="串口号")
    ap.add_argument("--baud", type=int, default=115200)

    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--stat",     action="store_true", help="读取实时数据")
    g.add_argument("--tune-pi",  action="store_true", help="调 PI 参数")
    g.add_argument("--set-vref", type=float, metavar="V", help="设定输出电压目标")
    g.add_argument("--start",    action="store_true", help="启动逆变器")
    g.add_argument("--stop",     action="store_true", help="停止逆变器")
    g.add_argument("--calib",    action="store_true", help="交互式校准 V/I 增益")
    g.add_argument("--log",      type=float, metavar="SEC", help="监听 N 秒 LOG 写 CSV")
    g.add_argument("--simulate", action="store_true", help="PC 仿真当前 PI 参数（无需串口）")
    g.add_argument("--reset",    action="store_true")

    ap.add_argument("--kp",   type=float, default=0.020)
    ap.add_argument("--ki",   type=float, default=0.005)
    ap.add_argument("--vref", type=float, default=24.0)

    args = ap.parse_args()

    # 仿真模式不需要串口
    if args.simulate:
        do_simulate(args.kp, args.ki, args.vref)
        return

    if not args.port:
        print("ERR: 该子命令需要 --port", file=sys.stderr); sys.exit(2)

    ser = open_serial(args.port, args.baud)
    try:
        if args.stat:                     do_stat(ser)
        elif args.tune_pi:                do_tune_pi(ser, args.kp, args.ki)
        elif args.set_vref is not None:   do_set_vref(ser, args.set_vref)
        elif args.start:                  do_start(ser)
        elif args.stop:                   do_stop(ser)
        elif args.calib:                  do_calib(ser)
        elif args.log is not None:        do_log(ser, args.log)
        elif args.reset:                  do_reset(ser)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
