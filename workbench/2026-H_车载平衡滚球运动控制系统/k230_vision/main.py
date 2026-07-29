# -*- coding: utf-8 -*-
"""
main.py —— 2026-H 车载平衡滚球：庐山派 K230 侧「球位视觉传感器」（CanMV MicroPython）

定位：**K230 只当传感器，不进控制环。** 相机 → 槽内钢球一维位置(mm) → UART 发
`$V,id,cx,cy,area*HH` 给 MSPM0；控制律(ball.c)、舵机、循迹、按键、计时、显示全在 MCU 上。

为什么这么切（都有依据，别改）
  * 评委答疑 Q4「视觉模块自带处理器…能否直接输出钢球坐标给主控 → 不限」⇒ 本架构官方合规
  * 答疑 Q15「不允许回传信息再参与平衡控制」⇒ 检测必须在车上算，不能传出去算
  * 答疑 Q22「球位相机安装位置不限、可随摆杆一同摆动」⇒ 装摆杆上消视差合规
  * MSPM0 侧 `uart_frame.c` 已有这套帧的解析 + `V` 诊断命令 + tools/vision_test.ps1
    ⇒ **MCU 侧零改动**（只需把球位输入接到一路 UART，见 README）

⚠️ 证据等级（诚实标注，别当已完成）
  `[PC 已验]` 纯逻辑：校验和 / px→mm 标定 / 离群门 —— 见文件末 selftest()，
             在 K230 上跑 `SELFTEST=True` 或在电脑上 `python main.py --selftest` 都能验
  `[待真机验证]` 所有 CanMV 硬件 API（Sensor / Display / FPIOA / UART 引脚号）
             —— 我写这份时手上没有 K230，板级 API 与引脚号**必须现场核**。
             脚本已把每个硬件调用包在 try 里并打 `[api]` 行，跑一次就知道哪句不对。

── 帧格式（与 workbench/mspm0/car/uart_frame.h 逐字一致）──
    $V,<id>,<cx>,<cy>,<area>*<HH>\\n
    id   : 1 = 看到球 / -1 = 没看到（MCU 判 NO_TARGET，不会误判成链路断）
    cx   : ⚠ 本题放的是 **球位 x_mm × 100**（0.01mm），不是像素。MCU 侧 x_mm = cx/100.0f
    cy   : 原始像素列（诊断：看 ROI 有没有偏）
    area : 斑点像素数（太小=可能是噪声）
    HH   : '$' 与 '*' 之间所有字符的异或，两位大写 HEX
"""

# ═══════════════════════ 配置（现场只改这一块） ═══════════════════════

SELFTEST = False          # True = 只跑纯逻辑自测、不开相机（第一次上板建议先 True）

# --- 相机 ---
IMG_W, IMG_H = 1280, 720  # ⚠ 宽度决定分辨率账：1280px 覆盖 250mm 摆杆 = 5.1 px/mm
                          #   ⇒ 整像素 cx 的量化误差只有 0.2mm，而控制目标是 ±3~5mm
                          #   ⇒ **亚像素不是必需的，提分辨率比写亚像素质心便宜**
GRAYSCALE = True          # 灰度：一维找亮/暗斑够用，且比 RGB 快

# --- ROI：只取槽内窄条（沿摆杆方向） ---
# ⚠ 刻度线不许进 ROI（说明 7 规定刻度贴槽外边沿）—— 它会产生假峰
ROI = (0, 330, 1280, 60)  # (x, y, w, h)，先用预览把它对准槽内

# --- 检测 ---
MODE      = "dark"        # "dark" = 球比槽底暗（暗场，对镜面球最鲁棒，推荐）
                          # "bright" = 镜面反光最亮（必须环形/漫射对称补光，不能点光源侧照）
THR_DARK   = (0, 70)      # dark 模式灰度阈值区间
THR_BRIGHT = (200, 255)   # bright 模式灰度阈值区间
PIX_MIN    = 30           # 斑点最小像素数（滤噪）
PIX_MAX    = 20000        # 斑点最大像素数（滤掉整条槽被误判）

# --- 两点 px→mm 标定（真值 = 槽边那条 0.1cm 刻度带）---
# 把球依次放到刻度 -100mm / +100mm，看串口/屏上打的 px，填进来
CAL_P1_PX, CAL_P1_MM = 40.0, -100.0
CAL_P2_PX, CAL_P2_MM = 1240.0, 100.0

X_ABS_MAX_MM = 130.0      # 超出摆杆物理范围的读数直接判无效
MAX_JUMP_MM  = 25.0       # 相邻帧最大跳变（离群门），连续 3 帧超限则认账重锁

# --- UART（⚠ 引脚号按庐山派实际可用脚改，见 README）---
UART_ID   = 2             # UART2
UART_TX   = 11            # FPIOA 引脚号
UART_RX   = 12
UART_BAUD = 115200

# --- 板载屏（调试用；答疑 Q5「2英寸限制只针对计时显示」⇒ 这块屏不违规）---
USE_LCD   = True          # 屏上画 ROI 框 + 球位 mm，现场不用连电脑就能调
STAT_SEC  = 2.0           # 每几秒打一行统计


# ═══════════════════════ 纯逻辑层（可 PC 单测，不依赖任何硬件） ═══════════════════════

def uf_checksum(body):
    """'$' 与 '*' 之间所有字符的异或（等同 uart_frame.c 的 uf_checksum）。"""
    x = 0
    for ch in body:
        x ^= ord(ch)
    return x & 0xFF


def build_frame(fid, cx, cy, area):
    body = "V,%d,%d,%d,%d" % (int(fid), int(cx), int(cy), int(area))
    return "$%s*%02X\n" % (body, uf_checksum(body))


def frame_seen(x_mm, x_px, area):
    """看到球：cx = x_mm*100（0.01mm），cy = 原始像素列。"""
    return build_frame(1, int(round(x_mm * 100.0)), int(round(x_px)), area)


def frame_none():
    return build_frame(-1, 0, 0, 0)


def px_to_mm(x_px):
    """两点线性标定。相机装反（像素轴与刻度反向）也自动成立。"""
    return CAL_P1_MM + (float(x_px) - CAL_P1_PX) * (CAL_P2_MM - CAL_P1_MM) / (CAL_P2_PX - CAL_P1_PX)


def mm_per_px():
    return abs((CAL_P2_MM - CAL_P1_MM) / (CAL_P2_PX - CAL_P1_PX))


class JumpGate:
    """离群门：跳变超限就丢，但连续 3 次超限就认账重锁（防永久卡死在旧值上）。"""

    def __init__(self, max_jump, relock_after=3):
        self.max_jump = max_jump
        self.relock_after = relock_after
        self.last = None
        self.streak = 0
        self.n_rejected = 0

    def accept(self, x_mm):
        if self.last is None or self.max_jump <= 0:
            self.last, self.streak = x_mm, 0
            return True
        if abs(x_mm - self.last) <= self.max_jump:
            self.last, self.streak = x_mm, 0
            return True
        self.streak += 1
        self.n_rejected += 1
        if self.streak >= self.relock_after:
            self.last, self.streak = x_mm, 0
            return True
        return False


def pick_blob(blobs):
    """选像素数最多且在合理区间内的斑点；返回 (cx_px, pixels) 或 (None, 0)。"""
    best = None
    for b in blobs:
        n = b.pixels()
        if n < PIX_MIN or n > PIX_MAX:
            continue
        if best is None or n > best[1]:
            best = (b.cx(), n)
    return best if best else (None, 0)


# ═══════════════════════ PC / 板上自测（纯逻辑，不用相机） ═══════════════════════

def selftest():
    ok = 0
    bad = 0

    def ck(name, got, want, tol=0.0):
        nonlocal ok, bad
        if isinstance(want, float) or isinstance(got, float):
            good = abs(got - want) <= tol
        else:
            good = (got == want)
        print("  %-44s got=%-24s want=%-24s %s" % (name, repr(got), repr(want), "OK" if good else "**FAIL**"))
        if good:
            ok += 1
        else:
            bad += 1

    print("-- 1. 帧编码 / 校验和（金标准由 PowerShell 独立异或算出）")
    ck("V,1,320,240,1500 校验", "%02X" % uf_checksum("V,1,320,240,1500"), "64")
    ck("V,-1,0,0,0 校验", "%02X" % uf_checksum("V,-1,0,0,0"), "7A")
    ck("V,1,-1234,517,842 校验", "%02X" % uf_checksum("V,1,-1234,517,842"), "43")
    ck("整帧", build_frame(1, 320, 240, 1500), "$V,1,320,240,1500*64\n")
    ck("没看到球的帧", frame_none(), "$V,-1,0,0,0*7A\n")
    ck("-12.34mm 编成 cx=-1234", frame_seen(-12.34, 517.0, 842), "$V,1,-1234,517,842*43\n")
    ck("帧长 <= UF_BUF_LEN(48)", len(build_frame(1, -12000, 1279, 99999)) - 2 <= 48, True)

    print("-- 2. px->mm 两点标定")
    ck("标定点1", px_to_mm(CAL_P1_PX), CAL_P1_MM, 1e-6)
    ck("标定点2", px_to_mm(CAL_P2_PX), CAL_P2_MM, 1e-6)
    ck("中点 -> 0mm", px_to_mm((CAL_P1_PX + CAL_P2_PX) / 2.0), 0.0, 1e-6)
    ck("分辨率 mm/px", round(mm_per_px(), 6),
       round(abs(CAL_P2_MM - CAL_P1_MM) / abs(CAL_P2_PX - CAL_P1_PX), 6), 1e-9)

    print("-- 3. 离群门（丢弃 + 连续3次重锁）")
    g = JumpGate(25.0)
    ck("首帧必接受", g.accept(0.0), True)
    ck("小跳变接受", g.accept(10.0), True)
    ck("大跳变第1次丢", g.accept(90.0), False)
    ck("大跳变第2次丢", g.accept(91.0), False)
    ck("大跳变第3次重锁", g.accept(92.0), True)
    ck("重锁后附近值接受", g.accept(95.0), True)

    print("-- 4. 斑点挑选（滤噪 + 取最大）")

    class _B:
        def __init__(self, cx, n):
            self._cx, self._n = cx, n

        def cx(self):
            return self._cx

        def pixels(self):
            return self._n

    ck("噪声斑全被滤掉 -> None", pick_blob([_B(100, 5), _B(200, 9)])[0], None)
    ck("取像素最多的那个", pick_blob([_B(100, 40), _B(600, 900), _B(900, 60)])[0], 600)
    ck("超大斑被滤掉", pick_blob([_B(100, 50), _B(600, 999999)])[0], 100)
    ck("空列表 -> None", pick_blob([])[0], None)

    print("\n  passed=%d failed=%d" % (ok, bad))
    print("RESULT: %s" % ("PASS" if bad == 0 else "FAIL"))
    return 0 if bad == 0 else 1


# ═══════════════════════ 硬件层（CanMV，全部 `待真机验证`） ═══════════════════════

def try_call(tag, fn):
    """把板级 API 包起来：成功/失败都打一行，这样第一次上板就知道哪句 API 名字不对。"""
    try:
        r = fn()
        print("[api] %-34s OK" % tag)
        return True, r
    except Exception as e:
        print("[api] %-34s FAIL %s: %s" % (tag, type(e).__name__, e))
        return False, None


def run():
    from media.sensor import Sensor
    from media.media import MediaManager
    from machine import UART, FPIOA
    import time

    print("[cfg] %dx%d roi=%s mode=%s  %.3f mm/px (%.1f px/mm)"
          % (IMG_W, IMG_H, ROI, MODE, mm_per_px(), 1.0 / mm_per_px()))
    if mm_per_px() > 0.34:
        print("[warn] 分辨率不足 3 px/mm：±1cm 判据下建议提高分辨率或收窄视野")

    # ---- UART ----
    fpioa = FPIOA()
    try_call("FPIOA UART_TXD", lambda: fpioa.set_function(UART_TX, getattr(FPIOA, "UART%d_TXD" % UART_ID)))
    try_call("FPIOA UART_RXD", lambda: fpioa.set_function(UART_RX, getattr(FPIOA, "UART%d_RXD" % UART_ID)))
    oku, uart = try_call("UART open", lambda: UART(getattr(UART, "UART%d" % UART_ID), UART_BAUD))
    if not oku:
        print("[fatal] UART 起不来 —— 先按 README 核 FPIOA 引脚号，别往下调视觉")
        return 2

    # ---- Sensor ----
    sensor = Sensor(width=IMG_W, height=IMG_H)
    try_call("sensor.reset", lambda: sensor.reset())
    try_call("set_framesize", lambda: sensor.set_framesize(width=IMG_W, height=IMG_H))
    try_call("set_pixformat", lambda: sensor.set_pixformat(Sensor.GRAYSCALE if GRAYSCALE else Sensor.RGB565))
    # ⚠ 固定曝光：球是镜面，AE/AGC 一动阈值就漂（车过弯朝向变时最明显）
    #    这几个 API 在 K230 上的名字与 OpenMV 可能不同 ⇒ 逐个试，失败不致命但要知道
    try_call("set_auto_exposure(off)", lambda: sensor.set_auto_exposure(False))
    try_call("set_auto_gain(off)", lambda: sensor.set_auto_gain(False))
    try_call("set_auto_whitebal(off)", lambda: sensor.set_auto_whitebal(False))

    disp = None
    if USE_LCD:
        try:
            from media.display import Display
            okd, _ = try_call("Display.init", lambda: Display.init(Display.ST7701, to_ide=True))
            disp = Display if okd else None
        except Exception as e:
            print("[api] Display import FAIL %s: %s" % (type(e).__name__, e))

    try_call("MediaManager.init", lambda: MediaManager.init())
    try_call("sensor.run", lambda: sensor.run())

    thr = [THR_DARK] if MODE == "dark" else [THR_BRIGHT]
    gate = JumpGate(MAX_JUMP_MM)
    n_frame = n_det = 0
    t_stat = time.ticks_ms()
    last_mm = 0.0

    try:
        while True:
            img = sensor.snapshot()
            n_frame += 1
            blobs = img.find_blobs(thr, roi=ROI, pixels_threshold=PIX_MIN,
                                   area_threshold=PIX_MIN, merge=True)
            x_px, area = pick_blob(blobs) if blobs else (None, 0)

            x_mm = None
            if x_px is not None:
                cand = px_to_mm(x_px)
                if abs(cand) <= X_ABS_MAX_MM and gate.accept(cand):
                    x_mm = cand

            if x_mm is None:
                uart.write(frame_none())
            else:
                n_det += 1
                last_mm = x_mm
                uart.write(frame_seen(x_mm, x_px, area))

            if disp is not None:
                try:
                    img.draw_rectangle(ROI[0], ROI[1], ROI[2], ROI[3], thickness=2)
                    if x_px is not None:
                        img.draw_line(int(x_px), ROI[1], int(x_px), ROI[1] + ROI[3], thickness=3)
                    img.draw_string_advanced(8, 8, 32,
                                             "NO BALL" if x_mm is None else "x=%+7.2f mm" % x_mm)
                    disp.show_image(img)
                except Exception:
                    disp = None      # 画不出来就别每帧抛异常拖慢主循环

            dt = time.ticks_diff(time.ticks_ms(), t_stat)
            if dt >= int(STAT_SEC * 1000):
                print("[stat] %.1f fps  det %d/%d (%.0f%%)  x=%+.2fmm  outlier %d"
                      % (n_frame * 1000.0 / dt, n_det, n_frame,
                         100.0 * n_det / max(1, n_frame), last_mm, gate.n_rejected))
                n_frame = n_det = 0
                t_stat = time.ticks_ms()
    except KeyboardInterrupt:
        pass
    finally:
        try_call("sensor.stop", lambda: sensor.stop())
        if disp is not None:
            try_call("Display.deinit", lambda: Display.deinit())
        try_call("MediaManager.deinit", lambda: MediaManager.deinit())
    return 0


# ═══════════════════════ 入口 ═══════════════════════

if __name__ == "__main__":
    _args = []
    try:
        import sys as _sys
        _args = list(getattr(_sys, "argv", []))[1:]
    except Exception:
        pass
    if SELFTEST or ("--selftest" in _args):
        selftest()
    else:
        run()
