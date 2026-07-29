# -*- coding: utf-8 -*-
"""
jf_recorder.py —— 2026-H 图传接收端：收 'JF' JPEG 流 → 实时显示 + 录像 + 可回放

**为什么需要它**（不是锦上添花，是补一个硬缺口）
  题目说明 1 与答疑 Q23 要求图传「**实时显示 + 完整记录每次测试的视频且能按要求回放**」。
  队友那条链（K230 → WiFi → C5 → SDIO → P4 → 硬解 → 长条屏）**实时那半已真机 PASS**，
  但 P4 那头只解码上屏、`microSD` 还被 `P4_ENABLE_SDCARD` 编译期关掉 ⇒ **没有任何存储/回放路径**。
  Q16 明答"允许用录屏软件、无需车载端存储" ⇒ 把接收端换成笔记本，实时与录像一次都有了。

**⚠️ 读过 K230 侧源码后的硬事实（决定了用法）**
  `k230_ap_stream.py` 是 `srv.listen(1)` 且**只 accept 一次**，之后全部帧只往那一个连接写
  ⇒ **单客户端**。所以 **P4 屏与本脚本互斥，同一时刻只能有一个连上去**。
  想两个同时看，得改 K230 脚本做多客户端 —— 那是在改一个已真机 PASS 的推流脚本，赛中不建议。
  测试时只需要一个接收端，用本脚本（能录像）；P4+长条屏留作展示/备份。

**帧格式**（与 `k230_ap_stream.py` / `p4_lcd.video_stream.c` 逐字一致）
    'J' 'F' | uint32 little-endian 长度 | JPEG 字节

**证据等级**
  `[PC 已验]` 帧同步 / 长度校验 / 重同步计数 / 落盘 —— `python jf_recorder.py --selftest`
  `[待真机]` 对着真 K230 跑（要 K230 的 AP 在线、笔记本连上 `K230_AP`）

用法
    python jf_recorder.py --selftest                       # 不要硬件，自测解析与落盘
    python jf_recorder.py --host 192.168.169.1             # 连 K230 的 AP 网关，实时看+录
    python jf_recorder.py --host 192.168.169.1 --seconds 40 --out _rec\t1
    python jf_recorder.py --host 192.168.169.1 --no-view   # 无窗口，纯录（远程/后台）
"""

from __future__ import annotations

import argparse
import os
import socket
import struct
import sys
import time

import numpy as np

MAGIC = b"JF"
HDR = 6                       # 'J''F' + uint32 LE
MAX_FRAME = 4 * 1024 * 1024   # 单帧长度上限：错位时防止读出一个天文数字


# ═════════════════ 解析层（只依赖 stdlib/numpy，可 PC 单测） ═════════════════

class JFStream:
    """把 TCP 字节流切成 JPEG 帧。

    ⚠ 这里唯一值得小心的失败模式是**流错位**：一旦少读/多读一个字节，之后每帧的
    长度字段都是垃圾，表现是"连上了但一帧都解不出"或"帧长忽大忽小"。
    所以不假设"读到的头一定是 JF"，而是**滑动重同步并计数**（`n_resync`），
    这个计数出现非零就说明发送端/链路有问题，别去调解码器。
    """

    def __init__(self):
        self.buf = bytearray()
        self.n_frames = 0
        self.n_resync = 0
        self.n_oversize = 0
        self.bytes_total = 0

    def feed(self, chunk: bytes):
        """喂一段字节，yield 出其中完整的 JPEG 帧（bytes）。"""
        self.buf += chunk
        self.bytes_total += len(chunk)
        while True:
            # 1) 对齐到魔数
            if len(self.buf) < HDR:
                return
            if self.buf[0:2] != MAGIC:
                idx = self.buf.find(MAGIC, 1)
                if idx < 0:
                    # 整段都没有魔数：只保留最后 1 字节（可能是被切断的 'J'）
                    self.n_resync += 1
                    del self.buf[: max(0, len(self.buf) - 1)]
                    return
                self.n_resync += 1
                del self.buf[:idx]
                continue
            # 2) 取长度
            (size,) = struct.unpack_from("<I", self.buf, 2)
            if size == 0 or size > MAX_FRAME:
                self.n_oversize += 1
                self.n_resync += 1
                del self.buf[:2]          # 丢掉这个假魔数，继续找
                continue
            if len(self.buf) < HDR + size:
                return                    # 帧还没收全
            frame = bytes(self.buf[HDR: HDR + size])
            del self.buf[: HDR + size]
            self.n_frames += 1
            yield frame


def jpeg_looks_valid(b: bytes) -> bool:
    """JPEG 头尾标记检查（SOI FFD8 / EOI FFD9）—— 便宜的坏帧判据。"""
    return len(b) > 4 and b[0] == 0xFF and b[1] == 0xD8 and b[-2] == 0xFF and b[-1] == 0xD9


# ═════════════════ 落盘 ═════════════════

class Recorder:
    """双路落盘：
    ① **原始 JPEG 序列（零重编码）** —— 回放时刻度最清晰，判分看的就是刻度，别再压一次
    ② .avi（MJPG）—— 便于连续播放/拖动，给评委看

    ⚠ 叠加的时间戳是**接收时刻**，不是曝光时刻（发送端未注入时间戳）⇒ **不能当端到端延迟证据**。
    """

    def __init__(self, out_prefix: str, save_jpg: bool, fps_hint: float = 17.0):
        self.prefix = out_prefix
        self.save_jpg = save_jpg
        self.fps_hint = fps_hint
        self.writer = None
        self.n_jpg = 0
        self.n_vid = 0
        d = os.path.dirname(os.path.abspath(out_prefix))
        if d:
            os.makedirs(d, exist_ok=True)
        if save_jpg:
            self.jpgdir = out_prefix + "_jpg"
            os.makedirs(self.jpgdir, exist_ok=True)

    def put_raw(self, idx: int, t_rel: float, raw: bytes):
        if not self.save_jpg:
            return
        with open(os.path.join(self.jpgdir, "%06d_%08.3fs.jpg" % (idx, t_rel)), "wb") as f:
            f.write(raw)
        self.n_jpg += 1

    def put_img(self, img):
        import cv2
        if self.writer is None:
            h, w = img.shape[:2]
            path = self.prefix + ".avi"
            self.writer = cv2.VideoWriter(path, cv2.VideoWriter_fourcc(*"MJPG"),
                                          self.fps_hint, (w, h))
            print("[rec] 视频 -> %s (%dx%d @%.1ffps)" % (path, w, h, self.fps_hint))
        self.writer.write(img)
        self.n_vid += 1

    def close(self):
        if self.writer is not None:
            self.writer.release()


def annotate(img, idx: int, t_rel: float, fps: float, mbps: float, size: int):
    import cv2
    txt1 = "#%d  t=%.2fs  %.1ffps  %.2fMbps" % (idx, t_rel, fps, mbps)
    txt2 = time.strftime("%Y-%m-%d %H:%M:%S") + "  frame %dB" % size
    for (y, t) in ((22, txt1), (44, txt2)):
        cv2.putText(img, t, (8, y), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 0), 3)
        cv2.putText(img, t, (8, y), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 0), 1)
    return img


# ═════════════════ 主循环 ═════════════════

def run(args) -> int:
    import cv2
    st = JFStream()
    rec = Recorder(args.out, not args.no_jpg) if args.out else None
    print("[net] 连接 %s:%d ..." % (args.host, args.port))
    s = socket.create_connection((args.host, args.port), timeout=args.connect_timeout)
    s.settimeout(2.0)
    print("[net] 已连接")

    t0 = time.time()
    n_bad = 0
    n_win = 0
    b_win = 0
    t_win = t0
    fps = mbps = 0.0
    rc = 1
    try:
        while True:
            if args.seconds and time.time() - t0 >= args.seconds:
                break
            try:
                chunk = s.recv(65536)
            except socket.timeout:
                print("[warn] 2s 没收到字节（发送端结束了？）")
                continue
            if not chunk:
                print("[net] 对端关闭连接")
                break
            for raw in st.feed(chunk):
                t_rel = time.time() - t0
                if not jpeg_looks_valid(raw):
                    n_bad += 1
                    continue
                if rec:
                    rec.put_raw(st.n_frames, t_rel, raw)
                n_win += 1
                b_win += len(raw)
                need_img = (not args.no_view) or rec is not None
                if need_img:
                    img = cv2.imdecode(np.frombuffer(raw, np.uint8), cv2.IMREAD_COLOR)
                    if img is None:
                        n_bad += 1
                        continue
                    vis = annotate(img.copy(), st.n_frames, t_rel, fps, mbps, len(raw))
                    if rec:
                        rec.put_img(vis)
                    if not args.no_view:
                        cv2.imshow("H-tuchuan (q=quit)", vis)
                        if (cv2.waitKey(1) & 0xFF) in (27, ord("q")):
                            raise KeyboardInterrupt
            dt = time.time() - t_win
            if dt >= 2.0:
                fps = n_win / dt
                mbps = b_win * 8.0 / dt / 1e6
                print("[stat] %.1f fps  %.2f Mbps  frames=%d bad=%d resync=%d"
                      % (fps, mbps, st.n_frames, n_bad, st.n_resync))
                n_win = b_win = 0
                t_win = time.time()
        rc = 0
    except KeyboardInterrupt:
        print("\n[net] 用户中止")
        rc = 0
    finally:
        el = time.time() - t0
        try:
            s.close()
        except Exception:
            pass
        if rec:
            rec.close()
        if not args.no_view:
            try:
                cv2.destroyAllWindows()
            except Exception:
                pass
        print("[sum] %.1fs  帧 %d  坏帧 %d  重同步 %d  超长 %d  字节 %d  平均 %.1f fps"
              % (el, st.n_frames, n_bad, st.n_resync, st.n_oversize,
                 st.bytes_total, st.n_frames / el if el else 0))
        if rec:
            print("[sum] 落盘：JPEG %d 张 / 视频 %d 帧" % (rec.n_jpg, rec.n_vid))
        good = st.n_frames > 0 and n_bad == 0 and st.n_resync == 0
        print("RESULT: %s" % ("PASS" if good else ("FAIL" if st.n_frames == 0 else "INCONCLUSIVE")))
    return rc


# ═════════════════ PC 自测（不需要 K230） ═════════════════

def selftest() -> int:
    import threading
    import cv2
    ok = bad = 0

    def ck(name, got, want):
        nonlocal ok, bad
        good = got == want
        print("  %-46s got=%-14r want=%-14r %s" % (name, got, want, "OK" if good else "**FAIL**"))
        if good:
            ok += 1
        else:
            bad += 1

    # 造 3 张合成 JPEG
    frames = []
    for i in range(3):
        img = np.zeros((64, 96, 3), np.uint8)
        img[:, :, i % 3] = 40 * (i + 1)
        enc = cv2.imencode(".jpg", img)[1].tobytes()
        frames.append(enc)
    print("-- 1. 帧同步（一次喂全部）")
    st = JFStream()
    blob = b"".join(MAGIC + struct.pack("<I", len(f)) + f for f in frames)
    got = list(st.feed(blob))
    ck("解出帧数", len(got), 3)
    ck("字节完全一致", got == frames, True)
    ck("无重同步", st.n_resync, 0)

    print("-- 2. 逐字节喂（TCP 任意切片下也不能丢帧）")
    st2 = JFStream()
    out = []
    for b in blob:
        out += list(st2.feed(bytes([b])))
    ck("逐字节解出帧数", len(out), 3)
    ck("逐字节内容一致", out == frames, True)

    print("-- 3. 流前面有垃圾 -> 应重同步而不是崩")
    st3 = JFStream()
    got3 = list(st3.feed(b"\x00\x01garbage\xff" + blob))
    ck("仍解出 3 帧", len(got3), 3)
    ck("重同步计数 >0", st3.n_resync > 0, True)

    print("-- 4. 假长度（错位）-> 计 oversize 并继续找")
    st4 = JFStream()
    got4 = list(st4.feed(MAGIC + struct.pack("<I", 0xFFFFFFF) + blob))
    ck("跳过假头后仍解出 3 帧", len(got4), 3)
    ck("oversize 计数 >0", st4.n_oversize > 0, True)

    print("-- 5. JPEG 头尾判据")
    ck("合成帧被判有效", jpeg_looks_valid(frames[0]), True)
    ck("截断帧被判无效", jpeg_looks_valid(frames[0][:-2]), False)
    ck("空字节被判无效", jpeg_looks_valid(b""), False)

    print("-- 6. 端到端：本地 TCP server 推 5 帧 -> 客户端收+落盘")
    port = 0
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    port = srv.getsockname()[1]
    srv.listen(1)

    def serve():
        c, _ = srv.accept()
        for i in range(5):
            f = frames[i % 3]
            c.sendall(MAGIC + struct.pack("<I", len(f)) + f)
            time.sleep(0.02)
        c.close()
        srv.close()

    th = threading.Thread(target=serve, daemon=True)
    th.start()
    outdir = os.path.join(".tmp_pdf", "mp", "jf_selftest")
    a = argparse.Namespace(host="127.0.0.1", port=port, seconds=0, out=outdir,
                           no_jpg=False, no_view=True, connect_timeout=5)
    rc = run(a)
    th.join(timeout=5)
    ck("run() 退出码", rc, 0)
    ck("JPEG 落盘 5 张", len(os.listdir(outdir + "_jpg")), 5)
    ck("avi 已生成", os.path.exists(outdir + ".avi"), True)

    print("\n  passed=%d failed=%d" % (ok, bad))
    print("SELFTEST RESULT: %s" % ("PASS" if bad == 0 else "FAIL"))
    return 0 if bad == 0 else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="2026-H 图传接收端（'JF' JPEG 流 → 显示 + 录像）")
    ap.add_argument("--host", default="192.168.169.1", help="K230 的地址（AP 网关）")
    ap.add_argument("--port", type=int, default=5001)
    ap.add_argument("--seconds", type=float, default=0, help="录多少秒，0=直到对端关流或按 q")
    ap.add_argument("--out", default="", help="落盘前缀，例 _rec\\t1 => t1.avi + t1_jpg\\")
    ap.add_argument("--no-jpg", action="store_true", help="不存原始 JPEG 序列（默认存）")
    ap.add_argument("--no-view", action="store_true", help="不开预览窗")
    ap.add_argument("--connect-timeout", type=float, default=10.0)
    ap.add_argument("--selftest", action="store_true", help="PC 自测，不需要 K230")
    args = ap.parse_args(argv)
    if args.selftest:
        return selftest()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
