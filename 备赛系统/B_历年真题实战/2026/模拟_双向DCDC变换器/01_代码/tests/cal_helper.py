"""
cal_helper.py
=============

PC 端校准 / 调试助手（与装置通过 UART 对话）。

工作流：
  python cal_helper.py --port COM3 --stat        # 看实时数据
  python cal_helper.py --port COM3 --calibrate   # 交互式校准
  python cal_helper.py --port COM3 --dump out.csv # 导出原始数据
  python cal_helper.py --port COM3 --verify out.csv # 跑金标准
  python cal_helper.py --port COM3 --reset       # 出厂复位
"""

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import algo_reference as alg


def open_serial(port: str, baud: int = 115200):
    try:
        import serial  # type: ignore
    except ImportError:
        print("ERR: 缺少 pyserial，请先 `pip install pyserial`", file=sys.stderr)
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


def do_calibrate(ser):
    print("[1] 当前实测：")
    print("    " + cmd(ser, "STAT").strip())
    real_val = input("[2] 输入标准基准值（按格式 'V=220.0 I=2.20'）: ").strip()
    print("[3] 发送校准命令")
    print("    " + cmd(ser, f"CAL {real_val}").strip())


def do_dump(ser, out_csv: str):
    """TODO: 根据题目调整 header 和数据格式"""
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
            started = True; continue
        if line == "DUMP_END":
            break
        if started:
            lines.append(line)
    
    Path(out_csv).write_text("raw\n" + "\n".join(lines) + "\n", encoding="utf-8")
    print(f"OK 写入 {out_csv}（{len(lines)} 行）")


def do_verify(csv_path: str):
    """TODO: 用 algo_reference 跑一遍并输出对比"""
    print(f"读 {csv_path}（待实现）")


def do_reset(ser):
    print(cmd(ser, "RST"))


def main():
    ap = argparse.ArgumentParser(description="2026 模拟 双向DCDC变换器 PC 校准助手")
    ap.add_argument("--port", help="串口号")
    ap.add_argument("--baud", type=int, default=115200)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--stat", action="store_true")
    g.add_argument("--calibrate", action="store_true")
    g.add_argument("--dump", metavar="CSV")
    g.add_argument("--verify", metavar="CSV")
    g.add_argument("--reset", action="store_true")
    args = ap.parse_args()

    if args.verify:
        do_verify(args.verify); return

    if not args.port:
        print("ERR: 该子命令需要 --port", file=sys.stderr); sys.exit(2)
    
    ser = open_serial(args.port, args.baud)
    try:
        if args.stat: do_stat(ser)
        elif args.calibrate: do_calibrate(ser)
        elif args.dump: do_dump(ser, args.dump)
        elif args.reset: do_reset(ser)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
