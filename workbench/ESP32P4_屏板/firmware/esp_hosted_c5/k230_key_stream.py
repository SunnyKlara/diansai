# K230 = AP + TCP server + 摄像头 MJPEG 发送，**USER 键控制起停**（拓扑 C）
#
# 与 k230_ap_stream.py 的区别：
#   · 不限时长（原脚本 RUN_MS=20000 只为做 demo），一直跑
#   · USER 键按一次开始发、再按一次停发；**停发时不关 socket、不停相机**，
#     只是把帧丢掉 —— 这就是用户要的"保持连接、只停发"
#   · 停发期间每 2 s 发一个 6 字节心跳（`'JF'` + uint32(0)，无 payload）。
#     必须有它：P4 侧 video_stream.c 把 SO_RCVTIMEO 设成 VIDEO_RX_TIMEO_S=10 s，
#     停发超过 10 s 会被判超时断连、然后重拨，accept 循环得重来一遍。
#     P4 侧已配套特判 len==0（在取 slot 与 len<4 检查之前）⇒ 认得这个心跳。
#   · 板载 RGB 指示：绿=正在发、红=已停发
#
# ⚠️ 硬事实（本板真机已验，别照抄 Lite-K230D 版的编号）：
#     USER 键 = **GPIO53**，下拉输入，**按下 = 高电平**
#     板载 RGB = 红 62 / 绿 20 / 蓝 63，**低电平点亮**
#     （Lite-K230D 版是 按键 64、RGB 65/66/71 且高电平点亮 —— 两版不可混用）
# ⚠️ 去抖不可省：实测单次按压只有 **110~160 ms**、连按间隔 200~380 ms。
#     主循环一圈约 140 ms（跟帧率走），纯轮询有概率整次错过一按 ⇒ 优先用中断，
#     中断不可用时退回轮询。用了哪条路径由脚本自己打印 KEY_MODE，不靠猜。
#
# 帧格式（与 P4 客户端一致）：'J' 'F' | uint32 LE 长度 | JPEG 字节
#   长度 0 = 暂停心跳（无 payload）
#
# 前置：k230_ap_up.py 已把 AP 起来（SSID K230_AP，网关 192.168.169.1）。
#       RT-Smart 网络栈在 MicroPython 软复位后仍保持 AP，故本脚本不碰 AP 配置。
#
# 手动跑：powershell -File ..\..\tools\k230_repl.ps1 -Port COM3 `
#           -File k230_key_stream.py -Paste -SoftReset -PasteWaitMs 90000
# 开机自启：⬜ `待验证` —— 庐山派 CanMV 固件据称从 SD 卡根目录的 main.py 自启，
#           本仓库**没有实测过**。验法：把本文件拷成卡根 main.py，断电重上电，
#           看串口有没有打出 KEYSTREAM 开头的行。
import network, socket, time, gc, uctypes, os
from machine import Pin, FPIOA
from media.sensor import *
from media.vencoder import *
from media.media import *

PORT          = 5001
WIDTH         = 640
HEIGHT        = 480
KEY_GPIO      = 53          # USER 键（真机已验）
LED_RED_GPIO  = 62          # 低电平点亮
LED_GRN_GPIO  = 20          # 低电平点亮
DEBOUNCE_MS   = 180         # < 实测连按间隔 200 ms，> 单次按压 160 ms
HEARTBEAT_MS  = 2000        # << P4 侧 SO_RCVTIMEO = 10 s
START_STREAMING = False     # 上电默认停发，等按键；改 True 则一连上就发

gc.collect()
print("KEYSTREAM mem_free=%d" % gc.mem_free())

# ---------------------------------------------------------------- 按键 + LED
fpioa = FPIOA()
fpioa.set_function(KEY_GPIO, getattr(FPIOA, "GPIO%d" % KEY_GPIO))
key = Pin(KEY_GPIO, Pin.IN, Pin.PULL_DOWN)

led_r = None
led_g = None
try:
    fpioa.set_function(LED_RED_GPIO, getattr(FPIOA, "GPIO%d" % LED_RED_GPIO))
    fpioa.set_function(LED_GRN_GPIO, getattr(FPIOA, "GPIO%d" % LED_GRN_GPIO))
    led_r = Pin(LED_RED_GPIO, Pin.OUT, value=1)   # 1 = 灭
    led_g = Pin(LED_GRN_GPIO, Pin.OUT, value=1)
except Exception as e:
    print("KEYSTREAM LED init skipped:", repr(e))


def show_state(on):
    """绿=在发，红=停发。LED 是低电平点亮，所以 value(0) 才是亮。"""
    try:
        if led_g:
            led_g.value(0 if on else 1)
        if led_r:
            led_r.value(1 if on else 0)
    except Exception:
        pass


# 按键计数器 + 上次触发时刻。用 list 装是为了在中断回调里改（免 global）。
key_hits = [0]
key_last = [0]


def _on_key(pin):
    t = time.ticks_ms()
    if time.ticks_diff(t, key_last[0]) < DEBOUNCE_MS:
        return                      # 抖动/同一次按压的多个边沿，吃掉
    key_last[0] = t
    key_hits[0] += 1


KEY_MODE = "poll"
try:
    key.irq(trigger=Pin.IRQ_RISING, handler=_on_key)
    KEY_MODE = "irq"
except Exception as e:
    print("KEYSTREAM key.irq unavailable (%s) -> falling back to polling" % repr(e))
print("KEYSTREAM key=GPIO%d mode=%s baseline=%d" % (KEY_GPIO, KEY_MODE, key.value()))

_poll_prev = [key.value()]


def key_pressed():
    """返回本次调用期间是否发生了一次（已去抖的）按下。两条路径共用一个出口。"""
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

# ---------------------------------------------------------------- 网络
ap = network.WLAN(network.AP_IF)
try:
    print("KEYSTREAM ap ifconfig=%s" % str(ap.ifconfig()))
except Exception as e:
    print("KEYSTREAM ap query FAIL", repr(e))

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
try:
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
except Exception:
    pass
srv.bind(socket.getaddrinfo("0.0.0.0", PORT)[0][-1])
srv.listen(1)
# 实测：本端口上 listening socket 默认是非阻塞的，裸 accept() 会立刻抛 EAGAIN(11)
try:
    srv.setblocking(True)
except Exception as e:
    print("KEYSTREAM setblocking raised", repr(e))
print("KEYSTREAM listening on 0.0.0.0:%d" % PORT)

# ---------------------------------------------------------------- 相机 + 编码器
sensor = None
encoder = None
link = None
HB = b"JF" + (0).to_bytes(4, "little")      # 暂停心跳，预先算好

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
    print("KEYSTREAM camera+encoder running; press USER key to start/stop sending")

    streaming = START_STREAMING
    show_state(streaming)

    while True:                                     # 外层：一个客户端断了就等下一个
        os.exitpoint()
        cs = None
        while cs is None:
            os.exitpoint()
            if key_pressed():                       # 没人连时也响应按键，别让人按了没反应
                streaming = not streaming
                show_state(streaming)
                print("KEYSTREAM (no client) streaming=%s" % streaming)
            try:
                cs, peer = srv.accept()
            except OSError as e:
                if e.args[0] in (11, 110, 115):     # EAGAIN / ETIMEDOUT / EINPROGRESS
                    time.sleep_ms(100)
                    continue
                raise
        print("KEYSTREAM accepted from", peer)

        n_sent = 0
        n_drop = 0
        t_hb = time.ticks_ms()
        t_rep = time.ticks_ms()
        try:
            while True:
                os.exitpoint()
                if key_pressed():
                    streaming = not streaming
                    show_state(streaming)
                    print("KEYSTREAM streaming=%s (sent=%d dropped=%d)" %
                          (streaming, n_sent, n_drop))

                # 无论发不发都必须取流并释放：编码器只有 8 个输出缓冲，
                # 停发时不取会填满，恢复时先吐出来的是一堆陈旧帧。
                encoder.GetStream(streamData)
                if streaming:
                    parts = []
                    size = 0
                    for i in range(streamData.pack_cnt):
                        b = uctypes.bytearray_at(streamData.data[i], streamData.data_size[i])
                        parts.append(bytes(b))       # 这个 view 在 ReleaseStream 后失效
                        size += len(parts[-1])
                    encoder.ReleaseStream(streamData)
                    buf = b"JF" + size.to_bytes(4, "little")
                    for p in parts:
                        buf += p                     # 每帧一次 send 比六次小 send 快
                    off = 0
                    mv = memoryview(buf)
                    while off < len(buf):
                        off += cs.send(mv[off:])
                    n_sent += 1
                else:
                    encoder.ReleaseStream(streamData)   # 直接丢
                    n_drop += 1
                    now = time.ticks_ms()
                    if time.ticks_diff(now, t_hb) >= HEARTBEAT_MS:
                        t_hb = now
                        cs.send(HB)                  # 让 P4 知道"我还在，只是暂停"

                now = time.ticks_ms()
                if time.ticks_diff(now, t_rep) >= 5000:
                    t_rep = now
                    print("KEYSTREAM %s sent=%d dropped=%d mem=%d" %
                          ("SENDING" if streaming else "PAUSED", n_sent, n_drop,
                           gc.mem_free()))
        except Exception as e:
            print("KEYSTREAM client gone:", repr(e))
        finally:
            try:
                cs.close()
            except Exception:
                pass
except KeyboardInterrupt:
    print("KEYSTREAM interrupted by user")
except Exception as e:
    import sys
    sys.print_exception(e)
finally:
    for fn in (lambda: sensor.stop(), lambda: link.destroy(),
               lambda: encoder.Stop(), lambda: encoder.Destroy(),
               lambda: srv.close(), lambda: show_state(False)):
        try:
            fn()
        except Exception:
            pass
    gc.collect()

print("KEYSTREAM exited")
