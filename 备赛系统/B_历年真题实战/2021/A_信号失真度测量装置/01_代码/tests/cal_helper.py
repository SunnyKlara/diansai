"""
cal_helper.py
=============

2021 A 信号失真度测量装置 — PC 端调试助手（与 MSP432P401R 通过 UART 对话）。

工作流：
  python cal_helper.py --port COM3 --stat              # 看一次 THD 测量结果
  python cal_helper.py --port COM3 --measure           # 触发"一键测量"
  python cal_helper.py --port COM3 --dump out.csv      # 导出 1024 点 ADC 原始数据
  python cal_helper.py --port COM3 --verify out.csv    # PC 跑金标准对比
  python cal_helper.py --port COM3 --reset

UART 协议：
  STAT      → "freq=XXX.X thd=X.XX H1=X.XXX H2=X.XXX H3=X.XXX H4=X.XXX H5=X.XXX vrms=X.XXX"
  MEASURE   → 触发一次完整测量（约 200ms）
  DUMP      → 导出 1024 点 ADC 原始数据（CSV）
  RST
"""

import argparse
import sys
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


def do_measure(ser):
    print("[1] 触发一键测量")
    print(cmd(ser, "MEASURE", wait_s=1.0))
    print("[2] 测量结果：")
    print(cmd(ser, "STAT"))


def do_dump(ser, out_csv: str):
    """导出 1024 点 ADC 原始数据"""
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

    if len(lines) < alg.FFT_SIZE:
        print(f"ERR: 期望 {alg.FFT_SIZE} 行，实际 {len(lines)} 行", file=sys.stderr)
        sys.exit(3)

    Path(out_csv).write_text("adc_raw\n" + "\n".join(lines[:alg.FFT_SIZE]) + "\n",
                             encoding="utf-8")
    print(f"OK 写入 {out_csv}（{alg.FFT_SIZE} 行）")


def do_verify(csv_path: str, fs: float):
    """用 algo_reference 跑金标准 → 输出对比"""
    rows = Path(csv_path).read_text(encoding="utf-8").strip().splitlines()[1:]
    if len(rows) < alg.FFT_SIZE:
        print(f"ERR: CSV 只有 {len(rows)} 行，不足 {alg.FFT_SIZE}", file=sys.stderr)
        sys.exit(3)

    adc_data = [int(r) for r in rows[:alg.FFT_SIZE]]

    # 先粗扫频
    sig_signed = [x - alg.ADC_MID_VALUE for x in adc_data]
    f_coarse = alg.freq_detect_measure(sig_signed, fs)
    sync_fs, m = alg.freq_detect_sync_rate(f_coarse, fs)
    print(f"PC 金标准：粗扫频 {f_coarse:.2f} Hz, 同步采样 fs={sync_fs:.2f}（M={m} 周期）")

    v_scale = alg.ADC_VREF / alg.ADC_MAX_VALUE
    r = alg.measure_thd(adc_data, fs, v_scale)

    print(f"\nPC reference (gold standard):")
    print(f"  Frequency  : {r.frequency:.3f} Hz")
    print(f"  Vrms (基波): {r.fundamental_vrms:.4f} V")
    print(f"  THD        : {r.thd_percent:.4f} %")
    print(f"  归一化谐波 :")
    for h in range(1, 6):
        print(f"    H{h}  {r.harmonic_norm[h]:.5f}")
    print()
    print("→ 与装置 STAT 输出对比，所有值差 < 1e-4 视为算法移植正确")


def do_reset(ser):
    print(cmd(ser, "RST"))


def main():
    ap = argparse.ArgumentParser(description="2021 A 信号失真度测量装置 PC 调试助手")
    ap.add_argument("--port", help="串口号")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--fs", type=float, default=alg.INITIAL_SAMPLE_RATE,
                    help="--verify 时使用的采样率（Hz）")

    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--stat", action="store_true")
    g.add_argument("--measure", action="store_true", help="触发一键测量")
    g.add_argument("--dump", metavar="CSV", help="导出原始 ADC 数据")
    g.add_argument("--verify", metavar="CSV", help="PC 跑金标准对比（无需串口）")
    g.add_argument("--reset", action="store_true")

    args = ap.parse_args()

    if args.verify:
        do_verify(args.verify, args.fs); return

    if not args.port:
        print("ERR: 该子命令需要 --port", file=sys.stderr); sys.exit(2)

    ser = open_serial(args.port, args.baud)
    try:
        if args.stat:        do_stat(ser)
        elif args.measure:   do_measure(ser)
        elif args.dump:      do_dump(ser, args.dump)
        elif args.reset:     do_reset(ser)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
