# K230 = AP + TCP server + 摄像头图传 + **录像存卡 + 原速回放**（方案①）
#
# 为什么录像放 K230 侧：P4 侧 microSD 已判死在硬件层（跨 SDMMC/GPSPI/bitbang 三种总线、
# 两张卡、有/无 C5 两种固件，从未应答过一条命令；见 README §10.13 ⑦⑧）。K230 的卡本来
# 就在正常工作（它从这张卡启动），MicroPython 直接 open() 就能写。
#
# ⚠️ 全文 `待真机验证` —— 只做过 PC 语法解析，没在 K230 上跑过。
#    上板第一件事看 RECPLAY 开头的行，尤其 `RECPLAY sd target=... free=...MB`。
#
# ---------------------------------------------------------------- 协议
# K230 -> P4（复用同一条 TCP，P4 侧 video_stream.c 已配套）：
#   'J''F' | u32 LE len | JPEG        图像帧（实时或回放，P4 不需要区分）
#   'J''F' | u32 LE 0                 暂停心跳（无 payload，防 P4 的 SO_RCVTIMEO=10s 断连）
#   'J''M' | u32 LE len | UTF-8 文本  元数据/应答（列表、状态、错误）⬜ P4 侧待加支持
# P4 -> K230：ASCII 行命令，'\n' 结尾
#   REC / RECSTOP / LIVE / LIST / PLAY <idx> / PAUSE / RESUME / SEEK <frame> / STAT
#
# ---------------------------------------------------------------- 录像文件格式
#   /sdcard/rec/REC00001.MJP
#   每帧： u32 LE 相对时间戳(ms) | u32 LE JPEG 长度 | JPEG 字节
#   时间戳是**相对第一帧**的毫秒数 ⇒ 回放时按相邻差值 sleep 即得原速。
#   为什么不把时间戳塞进 TCP 帧头：那会改动已经真机 PASS 的图传协议。
#   时间戳只是存储侧的事，回放时由 K230 控制发送节奏，P4 收到的仍是原样 'JF' 帧。
import network, socket, time, gc, uctypes, os, sys
from machine import Pin, FPIOA
from media.sensor import *
from media.vencoder import *
from media.media import *

PORT           = 5001
WIDTH          = 640
HEIGHT         = 480
KEY_GPIO       = 53          # USER 键（真机已验：下拉输入、按下=高）
LED_RED_GPIO   = 62          # 低电平点亮
LED_GRN_GPIO   = 20
DEBOUNCE_MS    = 180         # < 实测连按间隔 200ms，> 单次按压 160ms
HEARTBEAT_MS   = 2000        # << P4 侧 SO_RCVTIMEO = 10s
REC_DIRNAME    = "rec"
MIN_FREE_MB    = 20          # 卡满即停的阈值：低于这个就停录（留余量给文件系统元数据）
FREE_CHECK_N   = 30          # 每写这么多帧查一次剩余空间（statvfs 不便宜，别每帧查）
MAX_FRAME      = 96 * 1024   # 与 P4 侧 FRAME_MAX_BYTES 一致，超了 P4 会判非法长度断线

ST_LIVE, ST_REC, ST_PLAY = 0, 1, 2
STNAME = ("LIVE", "REC", "PLAY")

gc.collect()
print("RECPLAY start mem_free=%d" % gc.mem_free())


# ============================================================ 存储
def find_sd():
    """找一个可写的文件系统。不猜路径，逐个 statvfs 实测。"""
    for p in ("/sdcard", "/sd", "/"):
        try:
            v = os.statvfs(p)
            free_mb = v[0] * v[3] / 1048576.0
            total_mb = v[0] * v[2] / 1048576.0
            print("RECPLAY sd cand %-8s total=%.1fMB free=%.1fMB" % (p, total_mb, free_mb))
            if free_mb > MIN_FREE_MB:
                return p, free_mb
        except Exception as e:
            print("RECPLAY sd cand %-8s FAIL %s" % (p, repr(e)))
    return None, 0.0


def free_mb(root):
    try:
        v = os.statvfs(root)
        return v[0] * v[3] / 1048576.0
    except Exception:
        return -1.0


SD_ROOT, SD_FREE0 = find_sd()
REC_DIR = None
if SD_ROOT:
    REC_DIR = SD_ROOT.rstrip("/") + "/" + REC_DIRNAME
    try:
        os.mkdir(REC_DIR)
    except OSError:
        pass                      # EEXIST，正常
    print("RECPLAY sd target=%s free=%.1fMB recdir=%s" % (SD_ROOT, SD_FREE0, REC_DIR))
else:
    print("RECPLAY sd NONE -> 录像功能不可用，只做实时图传")


def rec_list():
    """录像文件列表，按序号排序。返回 [(idx, name, bytes)]"""
    out = []
    if not REC_DIR:
        return out
    try:
        for nm in os.listdir(REC_DIR):
            if nm.startswith("REC") and nm.endswith(".MJP"):
                try:
                    idx = int(nm[3:8])
                except Exception:
                    continue
                try:
                    sz = os.stat(REC_DIR + "/" + nm)[6]
                except Exception:
                    sz = 0
                out.append((idx, nm, sz))
    except Exception as e:
        print("RECPLAY listdir FAIL", repr(e))
    out.sort()
    return out


def next_rec_path():
    """序列号命名：取现有最大序号 +1，所以删掉中间的文件也不会撞名。"""
    lst = rec_list()
    nxt = (lst[-1][0] + 1) if lst else 1
    return REC_DIR + "/REC%05d.MJP" % nxt, nxt


# ============================================================ 按键 + LED
fpioa = FPIOA()
fpioa.set_function(KEY_GPIO, getattr(FPIOA, "GPIO%d" % KEY_GPIO))
key = Pin(KEY_GPIO, Pin.IN, Pin.PULL_DOWN)
led_r = led_g = None
try:
    fpioa.set_function(LED_RED_GPIO, getattr(FPIOA, "GPIO%d" % LED_RED_GPIO))
    fpioa.set_function(LED_GRN_GPIO, getattr(FPIOA, "GPIO%d" % LED_GRN_GPIO))
    led_r = Pin(LED_RED_GPIO, Pin.OUT, value=1)     # 1 = 灭
    led_g = Pin(LED_GRN_GPIO, Pin.OUT, value=1)
except Exception as e:
    print("RECPLAY LED skipped:", repr(e))

key_hits = [0]
key_last = [0]


def _on_key(pin):
    t = time.ticks_ms()
    if time.ticks_diff(t, key_last[0]) < DEBOUNCE_MS:
        return
    key_last[0] = t
    key_hits[0] += 1


KEY_MODE = "poll"
try:
    key.irq(trigger=Pin.IRQ_RISING, handler=_on_key)
    KEY_MODE = "irq"
except Exception as e:
    print("RECPLAY key.irq unavailable (%s) -> polling" % repr(e))
print("RECPLAY key=GPIO%d mode=%s baseline=%d" % (KEY_GPIO, KEY_MODE, key.value()))
_poll_prev = [key.value()]


def key_pressed():
    if KEY_MODE == "poll":
        v = key.value()
        if v and not _poll_prev[0]:
            t = time.ticks_ms()
            if time.ticks_diff(t, key_last[0]) >= DEBOUNCE_MS:
                key_last[0] = t
                key_hits[0] += 1
        _poll_prev[0] = v
    n = key_hits[0]
    if n != key_pressed.seen:
        key_pressed.seen = n
        return True
    return False


key_pressed.seen = key_hits[0]


def show_led(state, sending):
    """绿=在发实时/回放，红=录像中（红优先，因为录像是更需要提示的状态）。"""
    try:
        if led_r:
            led_r.value(0 if state == ST_REC else 1)
        if led_g:
            led_g.value(0 if (sending and state != ST_REC) else 1)
    except Exception:
        pass


# ============================================================ 网络
ap = network.WLAN(network.AP_IF)
try:
    print("RECPLAY ap ifconfig=%s" % str(ap.ifconfig()))
except Exception as e:
    print("RECPLAY ap query FAIL", repr(e))

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
try:
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
except Exception:
    pass
srv.bind(socket.getaddrinfo("0.0.0.0", PORT)[0][-1])
srv.listen(1)
try:
    srv.setblocking(True)     # 实测：本端口 listening socket 默认非阻塞，裸 accept 抛 EAGAIN(11)
except Exception as e:
    print("RECPLAY setblocking raised", repr(e))
print("RECPLAY listening on 0.0.0.0:%d" % PORT)

HB = b"JF" + (0).to_bytes(4, "little")


def send_all(cs, buf):
    off = 0
    mv = memoryview(buf)
    while off < len(buf):
        off += cs.send(mv[off:])


def send_meta(cs, text):
    """'JM' 元数据帧。P4 侧若还没加 'JM' 支持会判 bad magic 并重连 —— 所以
    ⬜ 上板前先确认 P4 固件版本，或把 META_ENABLE 关掉只用串口打印。"""
    if not META_ENABLE:
        print("RECPLAY meta(local) %s" % text)
        return
    b = text.encode("utf-8")
    send_all(cs, b"JM" + len(b).to_bytes(4, "little") + b)


META_ENABLE = True        # ⬜ P4 侧 'JM' 支持就绪后保持 True；未就绪时置 False 只打串口


# ============================================================ 命令通道（非阻塞收）
try:
    import select
    _HAVE_SELECT = True
except Exception:
    _HAVE_SELECT = False
print("RECPLAY select module=%s" % _HAVE_SELECT)


class CmdReader:
    """从 TCP 里非阻塞地捞出整行命令。
    为什么不直接 settimeout(0)+recv：那样 send 也会变非阻塞、大帧容易抛 EAGAIN。
    用 poll 只在真有数据时才 recv，socket 全程保持阻塞语义。"""

    def __init__(self, cs):
        self.cs = cs
        self.buf = b""
        self.poller = None
        if _HAVE_SELECT:
            try:
                self.poller = select.poll()
                self.poller.register(cs, select.POLLIN)
            except Exception as e:
                print("RECPLAY poll register FAIL", repr(e))
                self.poller = None

    def poll_line(self):
        if self.poller is None:
            return None                     # 没有 poll 就退化成"不收命令"，按键仍可用
        try:
            if not self.poller.poll(0):
                return None
            b = self.cs.recv(256)
            if not b:
                raise OSError("peer closed")
            self.buf += b
        except OSError as e:
            if e.args and e.args[0] in (11, 115):
                return None
            raise
        if b"\n" not in self.buf:
            if len(self.buf) > 512:
                self.buf = b""              # 防垃圾数据涨爆
            return None
        line, _, self.buf = self.buf.partition(b"\n")
        try:
            return line.decode("utf-8").strip()
        except Exception:
            return None


# ============================================================ 主体
sensor = encoder = link = None
try:
    width = ALIGN_UP(WIDTH, 16)
    sensor = Sensor()
    sensor.reset()
    sensor.set_framesize(width=width, height=HEIGHT, alignment=12)
    sensor.set_pixformat(Sensor.YUV420SP)
    encoder = Encoder()
    encoder.SetOutBufs(8, width, HEIGHT)
    chnAttr = ChnAttrStr(encoder.PAYLOAD_TYPE_JPEG, encoder.H264_PROFILE_MAIN, width, HEIGHT)
    streamData = StreamData()
    encoder.Create(chnAttr)
    link = MediaManager.link(sensor.bind_info()['src'],
                             (VIDEO_ENCODE_MOD_ID, VENC_DEV_ID, encoder.chn))
    encoder.Start()
    sensor.run()
    print("RECPLAY camera+encoder running")

    state = ST_LIVE
    sending = False            # USER 键控制：是否往 P4 发实时画面
    rec_f = None
    rec_t0 = 0
    rec_n = 0
    rec_path = ""
    play_f = None
    play_name = ""
    play_n = 0
    play_paused = False
    play_prev_ts = 0
    play_wall = 0

    def rec_close():
        global rec_f, state
        if rec_f:
            try:
                rec_f.close()
            except Exception:
                pass
        rec_f = None

    def play_close():
        global play_f
        if play_f:
            try:
                play_f.close()
            except Exception:
                pass
        play_f = None

    while True:                                    # 外层：客户端断了等下一个
        os.exitpoint()
        cs = None
        while cs is None:
            os.exitpoint()
            if key_pressed():
                sending = not sending
                show_led(state, sending)
                print("RECPLAY (no client) sending=%s" % sending)
            try:
                cs, peer = srv.accept()
            except OSError as e:
                if e.args[0] in (11, 110, 115):
                    time.sleep_ms(100)
                    continue
                raise
        print("RECPLAY accepted from", peer)
        rd = CmdReader(cs)
        t_hb = time.ticks_ms()
        t_rep = time.ticks_ms()

        try:
            while True:
                os.exitpoint()

                # ---- 1) 按键：切换"发不发实时画面"。录像/回放态下按键=回到实时
                if key_pressed():
                    if state == ST_PLAY:
                        play_close()
                        state = ST_LIVE
                        print("RECPLAY key -> back to LIVE")
                    else:
                        sending = not sending
                        print("RECPLAY key -> sending=%s" % sending)
                    show_led(state, sending)

                # ---- 2) 命令
                cmd = rd.poll_line()
                if cmd:
                    print("RECPLAY cmd %r (state=%s)" % (cmd, STNAME[state]))
                    up = cmd.upper()
                    if up == "REC":
                        if not REC_DIR:
                            send_meta(cs, "ERR no writable sd")
                        elif state == ST_REC:
                            send_meta(cs, "ERR already recording")
                        else:
                            play_close()
                            rec_path, idx = next_rec_path()
                            try:
                                rec_f = open(rec_path, "wb")
                                rec_t0 = time.ticks_ms()
                                rec_n = 0
                                state = ST_REC
                                sending = True          # 录像时也要能看见
                                send_meta(cs, "REC start %s" % rec_path)
                            except Exception as e:
                                rec_f = None
                                send_meta(cs, "ERR open %s" % repr(e))
                    elif up == "RECSTOP":
                        rec_close()
                        if state == ST_REC:
                            state = ST_LIVE
                            send_meta(cs, "REC done %s frames=%d free=%.1fMB"
                                      % (rec_path, rec_n, free_mb(SD_ROOT)))
                    elif up == "LIVE":
                        rec_close()
                        play_close()
                        state = ST_LIVE
                        sending = True
                        send_meta(cs, "LIVE")
                    elif up == "LIST":
                        lst = rec_list()
                        send_meta(cs, "LIST n=%d free=%.1fMB;" % (len(lst), free_mb(SD_ROOT))
                                  + ";".join("%d,%s,%d" % (i, nm, sz) for i, nm, sz in lst))
                    elif up.startswith("PLAY"):
                        parts = cmd.split()
                        lst = rec_list()
                        want = None
                        if len(parts) >= 2:
                            try:
                                k = int(parts[1])
                                for i, nm, sz in lst:
                                    if i == k:
                                        want = nm
                                        break
                            except Exception:
                                pass
                        elif lst:
                            want = lst[-1][1]
                        if want is None:
                            send_meta(cs, "ERR no such recording")
                        else:
                            rec_close()
                            play_close()
                            try:
                                play_f = open(REC_DIR + "/" + want, "rb")
                                play_name = want
                                play_n = 0
                                play_paused = False
                                play_prev_ts = 0
                                play_wall = time.ticks_ms()
                                state = ST_PLAY
                                send_meta(cs, "PLAY %s" % want)
                            except Exception as e:
                                play_f = None
                                send_meta(cs, "ERR open %s" % repr(e))
                    elif up == "PAUSE":
                        play_paused = True
                        send_meta(cs, "PAUSED at frame %d" % play_n)
                    elif up == "RESUME":
                        play_paused = False
                        play_wall = time.ticks_ms()
                        send_meta(cs, "RESUMED at frame %d" % play_n)
                    elif up.startswith("SEEK"):
                        # 拖动：顺序跳过 N 帧。文件是变长帧，只能顺序 skip；
                        # 640x480 JPEG 约 20KB/帧，跳 1000 帧≈20MB 顺序读，够快。
                        parts = cmd.split()
                        tgt = 0
                        try:
                            tgt = int(parts[1])
                        except Exception:
                            pass
                        if play_f is None:
                            send_meta(cs, "ERR not playing")
                        else:
                            try:
                                play_f.seek(0)
                                play_n = 0
                                play_prev_ts = 0
                                while play_n < tgt:
                                    h = play_f.read(8)
                                    if len(h) < 8:
                                        break
                                    ln = int.from_bytes(h[4:8], "little")
                                    play_f.seek(ln, 1)
                                    play_prev_ts = int.from_bytes(h[0:4], "little")
                                    play_n += 1
                                play_wall = time.ticks_ms()
                                send_meta(cs, "SEEK -> frame %d" % play_n)
                            except Exception as e:
                                send_meta(cs, "ERR seek %s" % repr(e))
                    elif up == "STAT":
                        send_meta(cs, "STAT state=%s sending=%s rec_n=%d play=%s/%d free=%.1fMB mem=%d"
                                  % (STNAME[state], sending, rec_n, play_name, play_n,
                                     free_mb(SD_ROOT), gc.mem_free()))
                    else:
                        send_meta(cs, "ERR unknown cmd")

                # ---- 3) 回放态：按文件里的时间戳原速发
                if state == ST_PLAY:
                    if play_paused:
                        now = time.ticks_ms()
                        if time.ticks_diff(now, t_hb) >= HEARTBEAT_MS:
                            t_hb = now
                            send_all(cs, HB)
                        time.sleep_ms(20)
                        continue
                    h = play_f.read(8)
                    if len(h) < 8:
                        send_meta(cs, "PLAY end %s frames=%d" % (play_name, play_n))
                        play_close()
                        state = ST_LIVE
                        show_led(state, sending)
                        continue
                    ts = int.from_bytes(h[0:4], "little")
                    ln = int.from_bytes(h[4:8], "little")
                    if ln == 0 or ln > MAX_FRAME:
                        send_meta(cs, "ERR corrupt frame len=%d at %d" % (ln, play_n))
                        play_close()
                        state = ST_LIVE
                        continue
                    jpg = play_f.read(ln)
                    if len(jpg) < ln:
                        send_meta(cs, "PLAY truncated at frame %d" % play_n)
                        play_close()
                        state = ST_LIVE
                        continue
                    # 原速：按相邻帧时间戳差，扣掉已经流逝的时间
                    dt = ts - play_prev_ts if play_n else 0
                    play_prev_ts = ts
                    if dt > 0:
                        slept = time.ticks_diff(time.ticks_ms(), play_wall)
                        rest = dt - slept
                        if rest > 0:
                            time.sleep_ms(rest if rest < 2000 else 2000)
                    play_wall = time.ticks_ms()
                    send_all(cs, b"JF" + ln.to_bytes(4, "little") + jpg)
                    play_n += 1
                    t_hb = time.ticks_ms()
                    continue

                # ---- 4) 实时/录像态：必须每圈取流并释放，否则 8 个输出缓冲会填满
                encoder.GetStream(streamData)
                parts = []
                size = 0
                if sending or state == ST_REC:
                    for i in range(streamData.pack_cnt):
                        b = uctypes.bytearray_at(streamData.data[i], streamData.data_size[i])
                        parts.append(bytes(b))       # view 在 ReleaseStream 后失效
                        size += len(parts[-1])
                encoder.ReleaseStream(streamData)

                if size and size <= MAX_FRAME:
                    jpg = parts[0] if len(parts) == 1 else b"".join(parts)
                    if state == ST_REC and rec_f:
                        ts = time.ticks_diff(time.ticks_ms(), rec_t0)
                        try:
                            rec_f.write(ts.to_bytes(4, "little"))
                            rec_f.write(size.to_bytes(4, "little"))
                            rec_f.write(jpg)
                            rec_n += 1
                        except Exception as e:
                            send_meta(cs, "ERR write %s" % repr(e))
                            rec_close()
                            state = ST_LIVE
                            show_led(state, sending)
                        if rec_n and (rec_n % FREE_CHECK_N == 0):
                            fm = free_mb(SD_ROOT)
                            if 0 <= fm < MIN_FREE_MB:
                                rec_close()
                                state = ST_LIVE
                                show_led(state, sending)
                                send_meta(cs, "REC stopped: card full (free=%.1fMB) frames=%d"
                                          % (fm, rec_n))
                    if sending:
                        send_all(cs, b"JF" + size.to_bytes(4, "little") + jpg)
                        t_hb = time.ticks_ms()

                if not sending:
                    now = time.ticks_ms()
                    if time.ticks_diff(now, t_hb) >= HEARTBEAT_MS:
                        t_hb = now
                        send_all(cs, HB)

                now = time.ticks_ms()
                if time.ticks_diff(now, t_rep) >= 5000:
                    t_rep = now
                    print("RECPLAY %s sending=%s rec_n=%d play=%d free=%.1fMB mem=%d"
                          % (STNAME[state], sending, rec_n, play_n, free_mb(SD_ROOT),
                             gc.mem_free()))
        except Exception as e:
            print("RECPLAY client gone:", repr(e))
        finally:
            rec_close()
            play_close()
            try:
                cs.close()
            except Exception:
                pass
except KeyboardInterrupt:
    print("RECPLAY interrupted")
except Exception as e:
    sys.print_exception(e)
finally:
    for fn in (lambda: sensor.stop(), lambda: link.destroy(),
               lambda: encoder.Stop(), lambda: encoder.Destroy(),
               lambda: srv.close(), lambda: show_led(ST_LIVE, False)):
        try:
            fn()
        except Exception:
            pass
    gc.collect()
print("RECPLAY exited")
