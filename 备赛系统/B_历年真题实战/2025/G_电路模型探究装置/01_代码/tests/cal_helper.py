"""
cal_helper.py
=============

2025 G 电路模型探究装置 — PC 端调试助手（与 STM32F407 通过 UART 对话）。

工作流：
  python cal_helper.py --port COM3 --stat                # 看当前学习状态
  python cal_helper.py --port COM3 --learn               # 触发扫频学习
  python cal_helper.py --port COM3 --dump-table out.csv  # 导出频响表
  python cal_helper.py --port COM3 --plot out.csv        # 画 Bode 图（需 matplotlib）
  python cal_helper.py --port COM3 --infer 1000 --vpp 0.5  # 单频 sin 推理验证
  python cal_helper.py --port COM3 --reset

UART 协议：
  STAT      → "filter=LP fc=5000 q=0.71 n_points=30 state=READY"
  LEARN     → 启动扫频学习（约 5~10 秒）
  DUMP      → 导出频响表（CSV：freq,gain,phase）
  INFER <freq> <vpp>  → 在该频率合成 sin，输出经 H(f) 处理后的结果
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
import math
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


def do_learn(ser):
    print("[1] 触发扫频学习（约 5~10 秒）")
    print(cmd(ser, "LEARN", wait_s=10.0))
    print("[2] 学习完成后状态：")
    print(cmd(ser, "STAT"))


def do_dump_table(ser, csv_path: str):
    """导出频响表"""
    ser.reset_input_buffer()
    ser.write(b"DUMP\r\n")
    ser.flush()
    rows = []
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
            rows.append(line)

    Path(csv_path).write_text("freq,gain,phase\n" + "\n".join(rows) + "\n",
                              encoding="utf-8")
    print(f"OK 写入 {csv_path}（{len(rows)} 个频点）")


def do_plot(csv_path: str):
    """画 Bode 图（增益 dB + 相位）"""
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("ERR: 缺少 matplotlib，请 `pip install matplotlib`", file=sys.stderr)
        sys.exit(2)

    rows = Path(csv_path).read_text(encoding="utf-8").strip().splitlines()[1:]
    freqs, gains, phases = [], [], []
    for r in rows:
        f, g, p = r.split(",")
        freqs.append(float(f))
        gains.append(20 * math.log10(max(float(g), 1e-9)))
        phases.append(math.degrees(float(p)))

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))
    ax1.semilogx(freqs, gains)
    ax1.set_ylabel("Gain (dB)")
    ax1.grid(True, which="both")
    ax1.set_title(f"Bode Plot - {csv_path}")

    ax2.semilogx(freqs, phases)
    ax2.set_xlabel("Frequency (Hz)")
    ax2.set_ylabel("Phase (deg)")
    ax2.grid(True, which="both")

    plt.tight_layout()
    out_png = csv_path.replace(".csv", ".png")
    plt.savefig(out_png, dpi=120)
    print(f"→ 写入 {out_png}")


def do_classify(csv_path: str):
    """读 CSV → 用 PC 端算法分类滤波器类型"""
    rows = Path(csv_path).read_text(encoding="utf-8").strip().splitlines()[1:]
    points = []
    for r in rows:
        f, g, p = r.split(",")
        points.append(alg.FreqPoint(frequency=float(f), gain=float(g), phase_rad=float(p)))
    ftype = alg.classify_filter(points)
    print(f"分类结果: {ftype}")
    return ftype


def do_infer_pc(freq: float, vpp: float, csv_path: str):
    """PC 端模拟一次推理：用导出的频响表对单频 sin 做 FFT 处理"""
    rows = Path(csv_path).read_text(encoding="utf-8").strip().splitlines()[1:]
    table = []
    for r in rows:
        f, g, p = r.split(",")
        table.append(alg.FreqPoint(frequency=float(f), gain=float(g), phase_rad=float(p)))

    fs = alg.INFERENCE_SAMPLE_RATE
    n = 1024
    inp = [(vpp / 2) * math.sin(2 * math.pi * freq * k / fs) for k in range(n)]
    out = alg.infer_one_frame(table, inp, fs)
    in_peak = max(abs(v) for v in inp)
    out_peak = max(abs(v) for v in out)
    gain_meas = out_peak / in_peak if in_peak > 0 else 0.0

    g, ph = alg.interp_freq_response(table, freq)
    print(f"PC 推理（freq={freq} Hz, vpp={vpp}）：")
    print(f"  表内 H({freq}) = {g:.4f} ∠ {math.degrees(ph):+.2f}°")
    print(f"  FFT 推理输出峰值: {out_peak:.4f}")
    print(f"  实测增益(out_peak/in_peak): {gain_meas:.4f}")
    err = abs(gain_meas - g)
    print(f"  增益误差: {err:.4f} ({'✓' if err < 0.1 else '⚠'} 容差 0.10)")


def do_reset(ser):
    print(cmd(ser, "RST"))


def main():
    ap = argparse.ArgumentParser(description="2025 G 电路模型探究装置 PC 调试助手")
    ap.add_argument("--port", help="串口号")
    ap.add_argument("--baud", type=int, default=115200)

    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--stat", action="store_true")
    g.add_argument("--learn", action="store_true", help="触发扫频学习")
    g.add_argument("--dump-table", metavar="CSV", help="导出频响表")
    g.add_argument("--plot", metavar="CSV", help="画 Bode 图（无需串口）")
    g.add_argument("--classify", metavar="CSV", help="对导出的 CSV 分类（无需串口）")
    g.add_argument("--infer-pc", type=float, metavar="FREQ", help="PC 端用导出表跑推理")
    g.add_argument("--reset", action="store_true")

    ap.add_argument("--vpp", type=float, default=0.5, help="--infer-pc 用的 Vpp")
    ap.add_argument("--table", default="freq_table.csv", help="--infer-pc 用的频响表 CSV")
    args = ap.parse_args()

    # 不需要串口的子命令
    if args.plot:
        do_plot(args.plot); return
    if args.classify:
        do_classify(args.classify); return
    if args.infer_pc is not None:
        do_infer_pc(args.infer_pc, args.vpp, args.table); return

    if not args.port:
        print("ERR: 该子命令需要 --port", file=sys.stderr); sys.exit(2)

    ser = open_serial(args.port, args.baud)
    try:
        if args.stat:               do_stat(ser)
        elif args.learn:            do_learn(ser)
        elif args.dump_table:       do_dump_table(ser, args.dump_table)
        elif args.reset:            do_reset(ser)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
