# K230 = AP + TCP server + camera MJPEG sender -- LONG RUN variant for "watch the screen".
#
# Identical to k230_ap_stream.py except for two things:
#   RUN_MS  20 s -> 180 s   (a 20 s window is enough to PROVE the link, but not enough
#                            for a human to walk over, look at the panel and judge colour
#                            / orientation / latency)
#   a progress line every ~2 s so the run can be monitored from the transcript instead of
#   only seeing one summary at the very end.
#
# Prerequisite: k230_ap_up.py has brought the AP up (SSID K230_AP, gateway 192.168.169.1).
# The RT-Smart network stack keeps the AP up across MicroPython soft reboots.
#
# Run:  powershell -File ..\..\tools\k230_repl.ps1 -Port COM3 `
#         -File k230_ap_stream_view.py -Paste -SoftReset -PasteWaitMs 200000
#
# Frame format (matches the P4 client): 'J' 'F' | uint32 LE length | JPEG bytes
import network, socket, time, gc, uctypes, os
from media.sensor import *
from media.vencoder import *
from media.media import *

PORT     = 5001
WIDTH    = 640
HEIGHT   = 480
RUN_MS   = 180000
REPORT_MS = 2000

gc.collect()
print("VIEW mem_free=%d" % gc.mem_free())

ap = network.WLAN(network.AP_IF)
try:
    print("VIEW ap ifconfig=%s status=%s" % (str(ap.ifconfig()), str(ap.status())))
except Exception as e:
    print("VIEW ap query FAIL", repr(e))

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
try:
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
except Exception:
    pass
srv.bind(socket.getaddrinfo("0.0.0.0", PORT)[0][-1])
srv.listen(1)
print("VIEW listening on 0.0.0.0:%d" % PORT)

sensor = None
encoder = None
link = None
cs = None
result = "FAIL"
try:
    # MEASURED: the listening socket is NON-BLOCKING by default on this port, so a bare
    # accept() throws OSError(11)/EAGAIN immediately instead of waiting.
    try:
        srv.setblocking(True)
    except Exception as e:
        print("VIEW setblocking raised", repr(e))
    t_acc = time.ticks_ms()
    while cs is None:
        if time.ticks_diff(time.ticks_ms(), t_acc) > 60000:
            print("VIEW RESULT: FAIL (nobody dialled in within 60 s)")
            raise SystemExit
        try:
            cs, peer = srv.accept()
        except OSError as e:
            if e.args[0] in (11, 110, 115):      # EAGAIN / ETIMEDOUT / EINPROGRESS
                time.sleep_ms(200)
                continue
            raise
    print("VIEW accepted from", peer)

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
    print("VIEW camera+encoder running, streaming %d ms -- LOOK AT THE PANEL NOW" % RUN_MS)

    n = 0
    total = 0
    err = None
    t0 = time.ticks_ms()
    t_rep = t0
    n_rep = 0
    b_rep = 0
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
        n_rep += 1
        b_rep += size + 6
        dt = time.ticks_diff(time.ticks_ms(), t_rep)
        if dt >= REPORT_MS:
            print("VIEW t=%ds  %.1f fps  %.2f Mbps  frame %dB  sent %d" %
                  (time.ticks_diff(time.ticks_ms(), t0) // 1000,
                   n_rep * 1000.0 / dt, b_rep * 8.0 / dt / 1000.0, size, n))
            t_rep = time.ticks_ms()
            n_rep = 0
            b_rep = 0
    el = time.ticks_diff(time.ticks_ms(), t0)
    print("VIEW sent %d frames %d bytes in %d ms -> %.2f fps  %.2f Mbps" %
          (n, total, el, (n * 1000.0 / el) if el else 0,
           (total * 8.0 / el / 1000.0) if el else 0))
    if err:
        print("VIEW send error:", err)
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

print("VIEW RESULT: %s" % result)
