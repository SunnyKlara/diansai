# K230 = AP + TCP server + real camera MJPEG sender  (topology C, the one that works)
#
# Prerequisite: k230_ap_up.py has brought the AP up (SSID K230_AP, gateway
# 192.168.169.1). The RT-Smart network stack keeps it up across MicroPython soft
# reboots, so this script does NOT touch the AP config -- one less variable.
#
# Why this topology: with the P4/C5 as SoftAP, association was unreliable (that AP
# emits WIFI_EVENT_HOME_CHANNEL_CHANGE pairs every ~15.58 s) and the K230's outgoing
# connect() returned ENOTCONN. Flipped, the P4 associates, gets a DHCP lease from the
# K230 (192.168.169.2) and pings it 5/5 -- see README §10.10.
#
# Run:  powershell -File ..\..\tools\k230_repl.ps1 -Port COM3 `
#         -File k230_ap_stream.py -Paste -SoftReset -PasteWaitMs 90000
#
# Frame format (matches the P4 client): 'J' 'F' | uint32 LE length | JPEG bytes
import network, socket, time, gc, uctypes, os
from media.sensor import *
from media.vencoder import *
from media.media import *

PORT    = 5001
WIDTH   = 640
HEIGHT  = 480
RUN_MS  = 20000

gc.collect()
print("STREAM mem_free=%d" % gc.mem_free())

ap = network.WLAN(network.AP_IF)
try:
    print("STREAM ap ifconfig=%s status=%s" % (str(ap.ifconfig()), str(ap.status())))
except Exception as e:
    print("STREAM ap query FAIL", repr(e))

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
try:
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
except Exception:
    pass
srv.bind(socket.getaddrinfo("0.0.0.0", PORT)[0][-1])
srv.listen(1)
print("STREAM listening on 0.0.0.0:%d" % PORT)

sensor = None
encoder = None
link = None
cs = None
result = "FAIL"
try:
    # MEASURED: on this port the listening socket is NON-BLOCKING by default, so a
    # bare accept() throws OSError(11)/EAGAIN immediately instead of waiting.
    try:
        srv.setblocking(True)
    except Exception as e:
        print("STREAM setblocking raised", repr(e))
    t_acc = time.ticks_ms()
    while cs is None:
        if time.ticks_diff(time.ticks_ms(), t_acc) > 45000:
            print("STREAM RESULT: FAIL (nobody dialled in within 45 s)")
            raise SystemExit
        try:
            cs, peer = srv.accept()
        except OSError as e:
            if e.args[0] in (11, 110, 115):      # EAGAIN / ETIMEDOUT / EINPROGRESS
                time.sleep_ms(200)
                continue
            raise
    print("STREAM accepted from", peer)

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
    print("STREAM camera+encoder running, streaming %d ms" % RUN_MS)

    n = 0
    total = 0
    err = None
    t0 = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), t0) < RUN_MS:
        os.exitpoint()
        encoder.GetStream(streamData)
        parts = []
        size = 0
        for i in range(streamData.pack_cnt):
            b = uctypes.bytearray_at(streamData.data[i], streamData.data_size[i])
            parts.append(bytes(b))          # the view dies at ReleaseStream
            size += len(parts[-1])
        encoder.ReleaseStream(streamData)
        try:
            buf = b"JF" + size.to_bytes(4, "little")
            for p in parts:
                buf += p                    # one send per frame beats six tiny ones
            off = 0
            mv = memoryview(buf)
            while off < len(buf):
                off += cs.send(mv[off:])
        except Exception as e:
            err = repr(e)
            break
        n += 1
        total += size + 6
    el = time.ticks_diff(time.ticks_ms(), t0)
    print("STREAM sent %d frames %d bytes in %d ms -> %.2f fps  %.2f Mbps" %
          (n, total, el, (n * 1000.0 / el) if el else 0,
           (total * 8.0 / el / 1000.0) if el else 0))
    if err:
        print("STREAM send error:", err)
    result = "PASS" if (n > 0 and not err) else "FAIL"
except Exception as e:
    import sys
    sys.print_exception(e)
finally:
    for fn in (lambda: sensor.stop(), lambda: link.destroy(),
               lambda: encoder.Stop(), lambda: encoder.Destroy(),
               lambda: cs.close(), lambda: srv.close()):
        try:
            fn()
        except Exception:
            pass
    gc.collect()

print("STREAM RESULT: %s" % result)
