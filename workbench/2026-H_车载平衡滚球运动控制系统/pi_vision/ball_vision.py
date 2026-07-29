#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ball_vision.py —— 2026-H 车载平衡滚球：树莓派侧「球位视觉传感器」

定位（一句话）：**Pi 只当传感器，不进控制环。** 相机 -> 一维球位(mm) -> 串口发
`$V,...` 帧给 MSPM0；控制律(ball.c)、舵机、循迹、按键、计时、显示全部留在 MCU 上。

为什么这样切（不是妥协，是判据）
  1. 题目说明 4 禁止人为干涉/遥控 ⇒ 检测必须**在车上**算，不能把画面传到笔记本再算。
  2. 运动层(速度环/里程 5.109counts/mm/转角/链式导航)全部是 MSPM0 上的真机资产，
     84h 内换平台等于自杀（作战地图 D7）。
  3. 循迹那 16 分(L0 保底层)必须**不依赖 Pi** —— 所以八路红外继续挂 MCU，Pi 死了 L0 还活着。
  4. px->mm 标定放在 Pi 上：重标 = 改一个 json，**不用重烧板**（禁忌 2：别频繁烧录）。

── 帧格式（与 workbench/mspm0/car/uart_frame.h 逐字一致，MCU 侧零改动就能收）──
    $V,<id>,<cx>,<cy>,<area>*<HH>\\n
    id   : 1 = 本帧看到球 / -1 = 没看到（UF_ID_NONE，MCU 会判成 NO_TARGET 而不是链路断）
    cx   : ⚠ **本题里放的是球位 x_mm × 100（即 0.01mm 单位）**，不是像素。
           理由见上第 4 条；MCU 侧只需 `x_mm = cx / 100.0f`。
           （帧的**格式**没改，只是这一题往 cx 里放什么量的约定；请同步写进 config.h 注释）
    cy   : 原始像素列（诊断用：看画面里球在哪，判 ROI 有没有偏）
    area : 峰面积代理（像素权重和），太小=可能是噪声，用于事后判读
    HH   : '$' 与 '*' 之间所有字符的**异或**，两位大写 HEX

── 算法层/驱动层分离（同 line.c/nav.c 的纪律）──
  * 检测核心只依赖 numpy（`profile_from_roi` / `detect_peak_1d` / `px_to_mm` / `build_frame`）
    ⇒ 可在 PC 上 `--selftest` 单测，**不需要相机、不需要 cv2**。
  * cv2 只用于「抓帧 / 读图 / JPEG 编码 / 录像」，import 是惰性的，缺了也能跑 selftest。

── 证据等级 ──
  `[PC 已验]` 检测核心 + 帧编码（见 --selftest，含 3 条独立算出的校验和金标准）
  `[待真机]` 相机曝光设置 / 帧率 / 端到端延迟 / 与 MCU 的串口链路 —— 全部要上车实测。

用法（详见同目录 README.md）
    python ball_vision.py --selftest                     # PC 自测，不要硬件
    python ball_vision.py --config calib.json --preview  # 有相机时看画面调 ROI
    python ball_vision.py --config calib.json            # 正常跑（发串口）
    python ball_vision.py --config calib.json --port none --print   # 只打印不发串口
    python ball_vision.py --config calib.json --stream-port 8081    # 顺带做 web 图传
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import time

import numpy as np

# ───────────────────────── 默认配置（--config 里的字段会覆盖同名项） ─────────────────────────
DEFAULT_CFG = {
    "camera": {
        "index": 0,            # /dev/video0
        "width": 640,
        "height": 480,
        "fps": 30,
        "fourcc": "MJPG",      # MJPG 通常能换来更高帧率；不支持时自动退回默认
        "autoexposure": False,  # ⚠ 必须关：球是镜面，AE 一动阈值就漂（弯道丢球的头号原因）
        "exposure": 100,        # 抄 MentorPi peripherals/config/usb_cam_param.yaml 的出厂值
        "auto_wb": False,
        "gain": None,           # None = 不设
    },
    # ROI：只取槽内窄条。axis="x" 表示摆杆在画面里沿 x 方向；"y" 则脚本内部自动转置。
    # ⚠ 刻度线不许进 ROI（说明 7 规定刻度贴在槽外边沿）——它会产生假峰。
    "roi": {"x": 0, "y": 200, "w": 640, "h": 40, "axis": "x"},
    "detect": {
        "mode": "bright",      # bright（镜面反光最亮，默认） / dark（球比槽底暗） / bgdiff（需先按 b 存背景）
        "min_amp": 12.0,       # 峰高门限（灰度）；低于它判"没看到"
        "win_px": 9,           # 亚像素质心窗口半宽
        "smooth": 3,           # 一维剖面滑动平均长度（1=不平滑）
        "max_jump_mm": 25.0,   # 相邻帧最大跳变（离群门），连续 3 帧超限则强制重锁
    },
    # 两点标定：把球放在槽边刻度的两个位置，各记像素列
    "calib": {"p1_px": 20.0, "p1_mm": -100.0, "p2_px": 620.0, "p2_mm": 100.0},
    "serial": {"port": "/dev/ttyUSB0", "baud": 115200},
    "limits": {"x_abs_max_mm": 130.0},  # 超出摆杆物理范围的读数直接判无效
}


def deep_update(base: dict, patch: dict) -> dict:
    for k, v in patch.items():
        if isinstance(v, dict) and isinstance(base.get(k), dict):
            deep_update(base[k], v)
        else:
            base[k] = v
    return base


# ═════════════════════════ 算法层（只依赖 numpy，可 PC 单测） ═════════════════════════

def profile_from_roi(gray: np.ndarray, axis: str = "x") -> np.ndarray:
    """把 ROI 灰度图压成沿摆杆方向的一维剖面（横向取均值）。

    横向取均值而不是取最大：均值把横向 N 行噪声降 sqrt(N) 倍，而球在横向占满窄条
    ⇒ 信号几乎不损失。轻微旋转（几度）也被这一步吸收，且旋转带来的尺度误差会被
    两点 mm 标定一起吸收（标定是沿"观测到的那个轴"做的）。
    """
    a = np.asarray(gray, dtype=np.float64)
    if a.ndim != 2:
        raise ValueError("profile_from_roi 需要二维灰度图")
    if axis == "y":
        a = a.T
    return a.mean(axis=0)


def _smooth(p: np.ndarray, n: int) -> np.ndarray:
    if n is None or n <= 1:
        return p
    n = int(n)
    k = np.ones(n, dtype=np.float64) / n
    return np.convolve(p, k, mode="same")


def detect_peak_1d(prof: np.ndarray, mode: str = "bright", ref: np.ndarray | None = None,
                   min_amp: float = 12.0, win_px: int = 9, smooth: int = 3):
    """一维剖面里找球，返回 (x_px | None, amp, area)。

    x_px 是**亚像素**位置（半峰以上加权质心）。±1cm 判据下亚像素不是锦上添花：
    5px/mm 时 1 个像素就是 0.2mm，整像素量化会直接吃掉一成裕量。

    返回 None 表示"这一帧没看到球" —— 绝不用上一帧顶替（过期数据冒充有效是
    uart_frame.h 明确要避免的失败模式）。
    """
    p = _smooth(np.asarray(prof, dtype=np.float64), smooth)
    if ref is not None:
        r = _smooth(np.asarray(ref, dtype=np.float64), smooth)
        n = min(len(p), len(r))
        sig = p[:n] - r[:n]
        if mode == "dark":
            sig = -sig
    else:
        base = float(np.median(p))
        sig = (p - base) if mode != "dark" else (base - p)
    if sig.size == 0:
        return None, 0.0, 0
    i = int(np.argmax(sig))
    amp = float(sig[i])
    if not np.isfinite(amp) or amp < float(min_amp):
        return None, amp, 0
    lo = max(0, i - int(win_px))
    hi = min(sig.size, i + int(win_px) + 1)
    w = sig[lo:hi] - 0.5 * amp          # 半峰以上才参与质心，压掉肩部与背景倾斜
    w = np.clip(w, 0.0, None)
    s = float(w.sum())
    if s <= 0.0:
        return float(i), amp, 0
    x = float((np.arange(lo, hi, dtype=np.float64) * w).sum() / s)
    return x, amp, int(round(s))


def px_to_mm(x_px: float, calib: dict) -> float:
    """两点线性标定 px -> mm（真值 = 槽边那条 0.1cm 刻度带）。"""
    p1, m1 = float(calib["p1_px"]), float(calib["p1_mm"])
    p2, m2 = float(calib["p2_px"]), float(calib["p2_mm"])
    if p2 == p1:
        raise ValueError("标定两点像素相同，无法定标")
    return m1 + (float(x_px) - p1) * (m2 - m1) / (p2 - p1)


def mm_per_px(calib: dict) -> float:
    p1, m1 = float(calib["p1_px"]), float(calib["p1_mm"])
    p2, m2 = float(calib["p2_px"]), float(calib["p2_mm"])
    return abs((m2 - m1) / (p2 - p1))


def uf_checksum(body: str) -> int:
    """'$' 与 '*' 之间所有字符的异或（与 uart_frame.c 的 uf_checksum 同义）。"""
    x = 0
    for ch in body.encode("ascii"):
        x ^= ch
    return x & 0xFF


def build_frame(id_: int, cx: int, cy: int, area: int) -> str:
    body = "V,%d,%d,%d,%d" % (int(id_), int(cx), int(cy), int(area))
    return "$%s*%02X\n" % (body, uf_checksum(body))


def frame_seen(x_mm: float, x_px: float, area: int) -> str:
    """看到球：cx = x_mm*100（0.01mm），cy = 原始像素列（诊断）。"""
    return build_frame(1, int(round(x_mm * 100.0)), int(round(x_px)), area)


def frame_none() -> str:
    return build_frame(-1, 0, 0, 0)


class JumpGate:
    """离群门：相邻帧跳变超限则丢弃，但连续超限 n 次就认账重锁（防永久卡死）。"""

    def __init__(self, max_jump_mm: float, relock_after: int = 3):
        self.max_jump = float(max_jump_mm)
        self.relock_after = int(relock_after)
        self.last = None
        self.streak = 0
        self.n_rejected = 0

    def accept(self, x_mm: float) -> bool:
        if self.last is None or self.max_jump <= 0:
            self.last, self.streak = x_mm, 0
            return True
        if abs(x_mm - self.last) <= self.max_jump:
            self.last, self.streak = x_mm, 0
            return True
        self.streak += 1
        self.n_rejected += 1
        if self.streak >= self.relock_after:      # 认账重锁
            self.last, self.streak = x_mm, 0
            return True
        return False

    def reset(self):
        self.last, self.streak = None, 0


# ═════════════════════════ 驱动层（相机 / 串口 / 图传，需要 cv2 或 pyserial） ═════════════════════════

def open_camera(cam_cfg: dict):
    import cv2  # 惰性 import：selftest 不需要
    idx = cam_cfg.get("index", 0)
    cap = cv2.VideoCapture(idx, cv2.CAP_V4L2 if hasattr(cv2, "CAP_V4L2") and os.name != "nt" else cv2.CAP_ANY)
    if not cap.isOpened():
        cap = cv2.VideoCapture(idx)
    if not cap.isOpened():
        raise RuntimeError("打不开相机 index=%r（先跑 `v4l2-ctl --list-devices` 看设备号）" % idx)
    fourcc = cam_cfg.get("fourcc")
    if fourcc:
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*fourcc))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, cam_cfg.get("width", 640))
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, cam_cfg.get("height", 480))
    cap.set(cv2.CAP_PROP_FPS, cam_cfg.get("fps", 30))
    # ⚠ 关自动曝光/白平衡：镜面球 + AE = 阈值漂移 + 弯道丢球（A.6 那条 OpenMV 踩坑同理）
    try:
        cap.set(cv2.CAP_PROP_AUTO_EXPOSURE, 3 if cam_cfg.get("autoexposure") else 1)
    except Exception:
        pass
    if not cam_cfg.get("autoexposure") and cam_cfg.get("exposure") is not None:
        cap.set(cv2.CAP_PROP_EXPOSURE, cam_cfg["exposure"])
    try:
        cap.set(cv2.CAP_PROP_AUTO_WB, 1 if cam_cfg.get("auto_wb") else 0)
    except Exception:
        pass
    if cam_cfg.get("gain") is not None:
        cap.set(cv2.CAP_PROP_GAIN, cam_cfg["gain"])
    try:
        cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)   # 只留最新一帧：缓冲深度直接变成延迟
    except Exception:
        pass
    return cap


def v4l2_force_manual(dev: str, exposure: int, gain: int | None = None) -> None:
    """OpenCV 设曝光在部分 UVC 驱动上不生效 ⇒ 直接调 v4l2-ctl 兜底（Linux 专用，失败不致命）。"""
    if os.name == "nt":
        return
    cmds = [
        "v4l2-ctl -d %s --set-ctrl=auto_exposure=1" % dev,
        "v4l2-ctl -d %s --set-ctrl=exposure_time_absolute=%d" % (dev, int(exposure)),
        "v4l2-ctl -d %s --set-ctrl=white_balance_automatic=0" % dev,
    ]
    if gain is not None:
        cmds.append("v4l2-ctl -d %s --set-ctrl=gain=%d" % (dev, int(gain)))
    for c in cmds:
        os.system(c + " >/dev/null 2>&1")


class SerialOut:
    """串口输出。port='none' 时退化成"什么都不发"，方便无硬件排练。"""

    def __init__(self, port: str, baud: int):
        self.ser = None
        if not port or str(port).lower() in ("none", "off", "-"):
            return
        import serial  # 惰性
        self.ser = serial.Serial(port, int(baud), timeout=0, write_timeout=0.05)

    def send(self, s: str) -> None:
        if self.ser is not None:
            try:
                self.ser.write(s.encode("ascii"))
            except Exception as e:      # 串口拔了不该让视觉进程死
                print("[warn] serial write failed: %r" % (e,), file=sys.stderr)

    def close(self):
        if self.ser is not None:
            try:
                self.ser.close()
            except Exception:
                pass


class MjpegServer:
    """极简 MJPEG 图传（第 1 项）：浏览器打开 http://<pi-ip>:<port>/ 即看画面。

    ⚠ 只出不进：不接受任何控制指令（说明 4 禁止遥控）。
    ⚠ 判分很可能就看这个画面 + 槽边刻度 ⇒ 流里必须**同时有球和刻度**，别把刻度裁掉。
    """

    def __init__(self, port: int, quality: int = 80):
        import threading
        from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
        self.quality = int(quality)
        self._jpg = None
        self._lock = threading.Lock()
        outer = self

        class H(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def log_message(self, *a):
                pass

            def do_GET(self):
                if self.path.rstrip("/") in ("", "/index.html"):
                    body = b"<html><body style='margin:0;background:#111'>" \
                           b"<img src='/stream' style='width:100%'></body></html>"
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                if self.path.rstrip("/") != "/stream":
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
                self.end_headers()
                try:
                    while True:
                        with outer._lock:
                            jpg = outer._jpg
                        if jpg is None:
                            time.sleep(0.05)
                            continue
                        self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                                         + str(len(jpg)).encode() + b"\r\n\r\n" + jpg + b"\r\n")
                        time.sleep(0.03)
                except Exception:
                    return

        self.srv = ThreadingHTTPServer(("0.0.0.0", int(port)), H)
        t = threading.Thread(target=self.srv.serve_forever, daemon=True)
        t.start()

    def publish(self, bgr) -> None:
        import cv2
        ok, buf = cv2.imencode(".jpg", bgr, [int(cv2.IMWRITE_JPEG_QUALITY), self.quality])
        if ok:
            with self._lock:
                self._jpg = buf.tobytes()


def annotate(bgr, roi: dict, x_px_abs, x_mm, fps: float, det_rate: float):
    """在画面上标 ROI 与读数（图传/预览用）。x_px_abs=None 表示本帧没看到球。"""
    import cv2
    x, y, w, h = int(roi["x"]), int(roi["y"]), int(roi["w"]), int(roi["h"])
    cv2.rectangle(bgr, (x, y), (x + w, y + h), (0, 255, 255), 1)
    txt = "NO BALL" if x_mm is None else "x = %+7.2f mm" % x_mm
    cv2.putText(bgr, txt, (8, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
    cv2.putText(bgr, "%.1f fps  det %.0f%%" % (fps, det_rate * 100.0), (8, 52),
                cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 1)
    if x_px_abs is not None:
        if roi.get("axis", "x") == "y":
            cv2.line(bgr, (x, int(x_px_abs)), (x + w, int(x_px_abs)), (0, 0, 255), 1)
        else:
            cv2.line(bgr, (int(x_px_abs), y), (int(x_px_abs), y + h), (0, 0, 255), 1)
    return bgr


# ═════════════════════════ 主循环 ═════════════════════════

def crop_roi(gray_full: np.ndarray, roi: dict) -> np.ndarray:
    x, y, w, h = int(roi["x"]), int(roi["y"]), int(roi["w"]), int(roi["h"])
    return gray_full[y:y + h, x:x + w]


def run(cfg: dict, args) -> int:
    import cv2
    cap = open_camera(cfg["camera"])
    if os.name != "nt" and not cfg["camera"].get("autoexposure"):
        v4l2_force_manual("/dev/video%d" % int(cfg["camera"].get("index", 0)),
                          cfg["camera"].get("exposure", 100), cfg["camera"].get("gain"))
    ser = SerialOut(args.port if args.port is not None else cfg["serial"]["port"],
                    cfg["serial"]["baud"])
    stream = MjpegServer(args.stream_port) if args.stream_port else None
    writer = None
    gate = JumpGate(cfg["detect"].get("max_jump_mm", 25.0))
    ref_prof = None
    roi, det, cal = cfg["roi"], cfg["detect"], cfg["calib"]

    res = mm_per_px(cal)
    print("[cfg] roi=%s axis=%s mode=%s  分辨率 %.3f mm/px (%.1f px/mm)"
          % ([roi["x"], roi["y"], roi["w"], roi["h"]], roi.get("axis", "x"),
             det.get("mode"), res, 1.0 / res if res else float("nan")), file=sys.stderr)
    if res > 0.34:
        print("[warn] 分辨率不足 3 px/mm：±1cm 判据下建议提高分辨率或收窄视野", file=sys.stderr)

    n_frame = n_det = 0
    t_stat = time.time()
    xs = []
    try:
        while True:
            ok, bgr = cap.read()
            if not ok:
                print("[warn] 抓帧失败", file=sys.stderr)
                time.sleep(0.01)
                continue
            t_cap = time.time()
            n_frame += 1
            gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
            prof = profile_from_roi(crop_roi(gray, roi), roi.get("axis", "x"))
            x_px, amp, area = detect_peak_1d(
                prof, det.get("mode", "bright"),
                ref_prof if det.get("mode") == "bgdiff" else None,
                det.get("min_amp", 12.0), det.get("win_px", 9), det.get("smooth", 3))

            x_mm = None
            if x_px is not None:
                cand = px_to_mm(x_px, cal)
                if abs(cand) <= cfg["limits"]["x_abs_max_mm"] and gate.accept(cand):
                    x_mm = cand
            if x_mm is None:
                gate.streak = min(gate.streak + 0, gate.relock_after)
                ser.send(frame_none())
            else:
                n_det += 1
                xs.append(x_mm)
                ser.send(frame_seen(x_mm, x_px, area))
                if args.print:
                    print("%.3f  x=%+8.2fmm  px=%7.2f amp=%5.1f area=%5d"
                          % (t_cap, x_mm, x_px, amp, area))

            if stream or args.preview or args.record:
                fps = n_frame / max(1e-6, time.time() - t_stat)
                x_abs = None if x_px is None else (x_px + (roi["y"] if roi.get("axis") == "y" else roi["x"]))
                vis = annotate(bgr, roi, x_abs, x_mm, fps, n_det / max(1, n_frame))
                if stream:
                    stream.publish(vis)
                if args.record:
                    if writer is None:
                        h, w = vis.shape[:2]
                        writer = cv2.VideoWriter(args.record, cv2.VideoWriter_fourcc(*"MJPG"),
                                                 cfg["camera"].get("fps", 30), (w, h))
                    writer.write(vis)
                if args.preview:
                    cv2.imshow("ball_vision", vis)
                    k = cv2.waitKey(1) & 0xFF
                    if k in (27, ord("q")):
                        break
                    if k == ord("b"):     # 存背景剖面（bgdiff 模式用；此时槽内不要放球）
                        ref_prof = prof.copy()
                        print("[cfg] 背景剖面已存", file=sys.stderr)
                    if k == ord("p"):     # 打印当前像素列，用于填两点标定
                        print("[calib] 当前 x_px = %r" % (x_px,))

            if time.time() - t_stat >= 2.0:
                fps = n_frame / (time.time() - t_stat)
                std = float(np.std(xs)) if len(xs) > 4 else float("nan")
                print("[stat] %.1f fps  检出 %d/%d (%.0f%%)  std %.3fmm  离群丢弃 %d"
                      % (fps, n_det, n_frame, 100.0 * n_det / max(1, n_frame), std, gate.n_rejected),
                      file=sys.stderr)
                n_frame = n_det = 0
                xs = []
                t_stat = time.time()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        if writer is not None:
            writer.release()
        try:
            cap.release()
        except Exception:
            pass
        if args.preview:
            try:
                cv2.destroyAllWindows()
            except Exception:
                pass
    return 0


def run_file(cfg: dict, args) -> int:
    """离线跑一张图 / 一段视频 —— 相机还没装好时先在 PC 上把检测器验掉（M1a 加速器）。"""
    import cv2
    roi, det, cal = cfg["roi"], cfg["detect"], cfg["calib"]
    srcs = []
    if args.image:
        img = cv2.imread(args.image, cv2.IMREAD_GRAYSCALE)
        if img is None:
            print("读不到图片: %s" % args.image, file=sys.stderr)
            return 2
        srcs = [img]
    else:
        cap = cv2.VideoCapture(args.video)
        while True:
            ok, f = cap.read()
            if not ok:
                break
            srcs.append(cv2.cvtColor(f, cv2.COLOR_BGR2GRAY))
        cap.release()
    for i, g in enumerate(srcs):
        prof = profile_from_roi(crop_roi(g, roi), roi.get("axis", "x"))
        x_px, amp, area = detect_peak_1d(prof, det.get("mode", "bright"), None,
                                         det.get("min_amp", 12.0), det.get("win_px", 9),
                                         det.get("smooth", 3))
        if x_px is None:
            print("#%d  NO BALL (amp=%.1f)" % (i, amp))
        else:
            print("#%d  x=%+8.2fmm  px=%7.2f amp=%5.1f area=%5d  frame=%s"
                  % (i, px_to_mm(x_px, cal), x_px, amp, area,
                     frame_seen(px_to_mm(x_px, cal), x_px, area).strip()))
    return 0


# ═════════════════════════ PC 自测（不需要相机/cv2） ═════════════════════════

def _synth(width=640, height=40, x_true=320.0, sigma=6.0, amp=90.0,
           bg=60.0, tilt=20.0, noise=2.0, seed=1) -> np.ndarray:
    """造一张合成 ROI：亮斑(球) + 背景倾斜(补光不均) + 高斯噪声。"""
    rng = np.random.default_rng(seed)
    xs = np.arange(width, dtype=np.float64)
    prof = bg + tilt * xs / width + amp * np.exp(-0.5 * ((xs - x_true) / sigma) ** 2)
    img = np.tile(prof, (height, 1)) + rng.normal(0.0, noise, size=(height, width))
    return np.clip(img, 0, 255)


def selftest() -> int:
    ok = fail = 0

    def ck(name, got, want, tol=0.0):
        nonlocal ok, fail
        good = (abs(got - want) <= tol) if isinstance(want, (int, float)) and not isinstance(want, bool) \
            else (got == want)
        print("  %-52s got=%-22r want=%-22r %s" % (name, got, want, "OK" if good else "**FAIL**"))
        if good:
            ok += 1
        else:
            fail += 1

    def ck_eq(name, got, want):
        nonlocal ok, fail
        good = got == want
        print("  %-52s got=%-22r want=%-22r %s" % (name, got, want, "OK" if good else "**FAIL**"))
        if good:
            ok += 1
        else:
            fail += 1

    print("── 1. 帧编码 / 校验和（金标准由 PowerShell 独立异或算出，非本文件自证）")
    ck_eq("$V,1,320,240,1500 校验", "%02X" % uf_checksum("V,1,320,240,1500"), "64")
    ck_eq("$V,-1,0,0,0 校验", "%02X" % uf_checksum("V,-1,0,0,0"), "7A")
    ck_eq("$V,1,-1234,517,842 校验", "%02X" % uf_checksum("V,1,-1234,517,842"), "43")
    ck_eq("build_frame 整帧", build_frame(1, 320, 240, 1500), "$V,1,320,240,1500*64\n")
    ck_eq("没看到球的帧(id=-1)", frame_none(), "$V,-1,0,0,0*7A\n")
    ck_eq("负球位 -12.34mm 编成 cx=-1234", frame_seen(-12.34, 517.0, 842),
          "$V,1,-1234,517,842*43\n")
    ck_eq("帧长 <= UF_BUF_LEN(48)", len(build_frame(1, -12000, 639, 99999)) - 2 <= 48, True)

    print("── 2. 亚像素定位精度（合成图，含背景倾斜与噪声）")
    for xt in (100.0, 320.4, 555.7):
        img = _synth(x_true=xt)
        x_px, amp, area = detect_peak_1d(profile_from_roi(img))
        ck("亮斑 x_true=%.1f 定位误差 <0.3px" % xt, round(abs(x_px - xt), 3), 0.0, 0.3)
        ck_eq("  峰面积 >0", area > 0, True)

    print("── 3. 失败模式必须被识别（不许拿上一帧顶替）")
    blank = _synth(amp=0.0, noise=2.0)
    x_px, amp, _ = detect_peak_1d(profile_from_roi(blank))
    ck_eq("空视野 -> None（判 NO_TARGET 而不是猜）", x_px, None)
    faint = _synth(amp=6.0)
    ck_eq("弱于门限的斑 -> None", detect_peak_1d(profile_from_roi(faint), min_amp=12.0)[0], None)

    print("── 4. 竖直摆杆（axis='y'）与转置一致")
    img = _synth(x_true=222.0)
    ck("axis=y 与 axis=x 结果一致", detect_peak_1d(profile_from_roi(img.T, "y"))[0],
       detect_peak_1d(profile_from_roi(img, "x"))[0], 1e-9)

    print("── 5. dark 模式（球比槽底暗）")
    dark = 200.0 - _synth(x_true=400.0, bg=0.0, tilt=0.0, amp=90.0, noise=1.0)
    ck("暗球定位误差 <0.3px", round(abs(detect_peak_1d(profile_from_roi(dark), "dark")[0] - 400.0), 3),
       0.0, 0.3)

    print("── 6. bgdiff 模式（先存无球背景，再差分）")
    bgimg = _synth(x_true=0.0, amp=0.0, noise=1.0, seed=7)
    ref = profile_from_roi(bgimg)
    cur = profile_from_roi(_synth(x_true=480.3, noise=1.0, seed=7))
    ck("bgdiff 定位误差 <0.3px", round(abs(detect_peak_1d(cur, "bgdiff", ref)[0] - 480.3), 3), 0.0, 0.3)

    print("── 7. px->mm 两点标定")
    cal = {"p1_px": 20.0, "p1_mm": -100.0, "p2_px": 620.0, "p2_mm": 100.0}
    ck("标定点 1", px_to_mm(20.0, cal), -100.0, 1e-9)
    ck("标定点 2", px_to_mm(620.0, cal), 100.0, 1e-9)
    ck("中点 -> 0mm", px_to_mm(320.0, cal), 0.0, 1e-9)
    ck("分辨率 mm/px", round(mm_per_px(cal), 6), round(200.0 / 600.0, 6), 1e-9)
    cal_inv = {"p1_px": 620.0, "p1_mm": -100.0, "p2_px": 20.0, "p2_mm": 100.0}
    ck("相机装反(像素轴与刻度反向)也对", px_to_mm(20.0, cal_inv), 100.0, 1e-9)

    print("── 8. 离群门（跳变丢弃 + 连续 3 次认账重锁）")
    g = JumpGate(25.0)
    ck_eq("首帧必接受", g.accept(0.0), True)
    ck_eq("小跳变接受", g.accept(10.0), True)
    ck_eq("大跳变第1次丢弃", g.accept(90.0), False)
    ck_eq("大跳变第2次丢弃", g.accept(91.0), False)
    ck_eq("大跳变第3次认账重锁", g.accept(92.0), True)
    ck_eq("重锁后附近值被接受", g.accept(95.0), True)

    print("── 9. 端到端：合成 ROI -> 帧字符串")
    img = _synth(x_true=320.0)
    x_px, amp, area = detect_peak_1d(profile_from_roi(img))
    s = frame_seen(px_to_mm(x_px, cal), x_px, area)
    ck_eq("帧以 $V, 开头且以 \\n 结尾", s.startswith("$V,") and s.endswith("\n"), True)
    body, _, chk = s[1:-1].partition("*")
    ck_eq("自校验一致", "%02X" % uf_checksum(body), chk)
    ck("中点球位 ≈ 0mm(±0.2)", round(float(body.split(",")[2]) / 100.0, 3), 0.0, 0.2)

    print("\n  passed=%d  failed=%d" % (ok, fail))
    print("RESULT: %s" % ("PASS" if fail == 0 else "FAIL"))
    return 0 if fail == 0 else 1


# ═════════════════════════ 入口 ═════════════════════════

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="2026-H 球位视觉传感器（Pi 侧）")
    ap.add_argument("--config", help="json 配置（默认值见文件头 DEFAULT_CFG）")
    ap.add_argument("--port", help="串口设备；'none' = 不发串口")
    ap.add_argument("--print", action="store_true", help="把每帧读数打到 stdout")
    ap.add_argument("--preview", action="store_true", help="开预览窗（调 ROI 用；b=存背景 p=打印像素列）")
    ap.add_argument("--stream-port", type=int, default=0, help="开 MJPEG 图传端口，如 8081")
    ap.add_argument("--record", help="本地录像文件（图传录制的保险）")
    ap.add_argument("--image", help="离线：跑一张图")
    ap.add_argument("--video", help="离线：跑一段视频")
    ap.add_argument("--selftest", action="store_true", help="PC 自测，不需要相机/cv2")
    ap.add_argument("--dump-config", action="store_true", help="打印默认配置模板")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    cfg = json.loads(json.dumps(DEFAULT_CFG))
    if args.config:
        with open(args.config, "r", encoding="utf-8") as f:
            deep_update(cfg, json.load(f))
    if args.dump_config:
        print(json.dumps(cfg, indent=2, ensure_ascii=False))
        return 0
    if args.image or args.video:
        return run_file(cfg, args)
    return run(cfg, args)


if __name__ == "__main__":
    sys.exit(main())
