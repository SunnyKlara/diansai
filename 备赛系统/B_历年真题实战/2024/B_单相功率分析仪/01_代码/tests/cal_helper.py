"""
cal_helper.py
=============

PC 端校准助手 / 调试工具（与 MSP430 装置通过 UART 对话）。

工作流（赛前必做一次）：
  1. 接好硬件，上电
  2. 把已知电压（如可调电源 100V/50Hz）+ 已知负载（如 100Ω 电阻）接到装置
  3. 运行：  python cal_helper.py --port COM3 --calibrate
       脚本读 STAT → 输入真实 V/I → 发送 CAL → 读 STAT 验证
  4. 校准系数自动存到装置 FRAM，断电不丢

调试流（任何时候）：
  python cal_helper.py --port COM3 --stat        # 看一次实时数据
  python cal_helper.py --port COM3 --dump out.csv # 导出 1000 点原始 ADC
  python cal_helper.py --port COM3 --verify out.csv  # 用 algo_reference 跑金标准对比
  python cal_helper.py --port COM3 --reset       # 出厂复位

依赖：pyserial（pip install pyserial）
"""

import argparse
import sys
import time
from pathlib import Path

# 校准 / 验证用到的算法参考实现（同目录下）
sys.path.insert(0, str(Path(__file__).parent))
import algo_reference as alg


# ============================================================
#  低层串口
# ============================================================
def open_serial(port: str, baud: int = 115200):
    try:
        import serial  # type: ignore
    except ImportError:
        print("ERR: 缺少 pyserial，请先 `pip install pyserial`", file=sys.stderr)
        sys.exit(2)
    return serial.Serial(port, baudrate=baud, timeout=2)


def cmd(ser, line: str, wait_s: float = 0.5) -> str:
    """发送一行命令，等待装置回包。"""
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


def do_calibrate(ser):
    print("[1] 读取当前测量值")
    raw = cmd(ser, "STAT")
    print("    " + raw.strip())

    v_real = float(input("[2] 输入标准 PA 测得的 Vrms（V）: ").strip())
    i_real = float(input("[3] 输入标准 PA 测得的 Irms（A）: ").strip())

    print("[4] 发送校准命令")
    resp = cmd(ser, f"CAL V={v_real} I={i_real}")
    print("    " + resp.strip())

    print("[5] 复读验证")
    print("    " + cmd(ser, "STAT").strip())


def do_dump(ser, out_csv: str):
    """读取 1000 点原始 ADC，写 CSV"""
    ser.reset_input_buffer()
    ser.write(b"DUMP\r\n")
    ser.flush()

    lines = []
    started = False
    deadline = time.time() + 30.0
    while time.time() < deadline:
        line = ser.readline().decode(errors="replace").strip()
        if not line:
            continue
        if line == "DUMP_START":
            started = True
            continue
        if line == "DUMP_END":
            break
        if started:
            lines.append(line)

    if len(lines) < alg.DFT_N:
        print(f"ERR: 期望 {alg.DFT_N} 行，实际 {len(lines)} 行", file=sys.stderr)
        sys.exit(3)

    Path(out_csv).write_text("v_adc,i_adc\n" + "\n".join(lines[:alg.DFT_N]) + "\n",
                             encoding="utf-8")
    print(f"OK 写入 {out_csv}（{alg.DFT_N} 行）")


def do_verify(csv_path: str, v_gain: float, i_gain: float):
    """用 algo_reference 在 PC 上跑一遍，与装置实测对比"""
    ADC_OFFSET_RAW = 2048    # 与 config.h 同步：12bit ADC 中点 = 1.65V 偏置点

    rows = Path(csv_path).read_text(encoding="utf-8").strip().splitlines()[1:]
    v, i = [], []
    for r in rows:
        a, b = r.split(",")
        v.append((int(a) - ADC_OFFSET_RAW) * v_gain)
        i.append((int(b) - ADC_OFFSET_RAW) * i_gain)

    td   = alg.power_calc_time_domain(v, i)
    harm = alg.dft_harmonic_compute(i)

    print(f"PC reference (gold standard):")
    print(f"  Vrms = {td.v_rms:.4f} V")
    print(f"  Irms = {td.i_rms:.4f} A")
    print(f"  P    = {td.p_active:.4f} W")
    print(f"  PF   = {td.pf:.4f}")
    print(f"  THD  = {harm.thd_percent:.4f} %")
    print()
    print("把这些数值与装置 STAT 输出对比，差值 < 0.01% 视为算法移植正确。")


def do_reset(ser):
    print(cmd(ser, "RST"))


# ============================================================
#  入口
# ============================================================
def main():
    ap = argparse.ArgumentParser(description="2024 B 单相功率分析仪 PC 校准 / 调试助手")
    ap.add_argument("--port", help="串口号（如 COM3 / /dev/ttyUSB0）")
    ap.add_argument("--baud", type=int, default=115200)

    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--stat",      action="store_true", help="读取一次实时数据")
    g.add_argument("--calibrate", action="store_true", help="交互式校准（需要 PA 基准）")
    g.add_argument("--dump",      metavar="CSV",        help="导出 1000 点原始 ADC")
    g.add_argument("--verify",    metavar="CSV",        help="用 algo_reference 跑金标准")
    g.add_argument("--reset",     action="store_true", help="清校准回出厂值")

    ap.add_argument("--v-gain", type=float, default=None,
                    help="--verify 时使用的 v_gain（默认从装置读取）")
    ap.add_argument("--i-gain", type=float, default=None,
                    help="--verify 时使用的 i_gain")

    args = ap.parse_args()

    # verify 不需要串口
    if args.verify:
        if args.v_gain is None or args.i_gain is None:
            print("ERR: --verify 需要 --v-gain 和 --i-gain（先用 --stat 从装置读取）",
                  file=sys.stderr)
            sys.exit(2)
        do_verify(args.verify, args.v_gain, args.i_gain)
        return

    if not args.port:
        print("ERR: 该子命令需要 --port", file=sys.stderr)
        sys.exit(2)

    ser = open_serial(args.port, args.baud)
    try:
        if args.stat:        do_stat(ser)
        elif args.calibrate: do_calibrate(ser)
        elif args.dump:      do_dump(ser, args.dump)
        elif args.reset:     do_reset(ser)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
