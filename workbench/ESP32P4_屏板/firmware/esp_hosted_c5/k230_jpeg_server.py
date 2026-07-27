# K230: join the P4's SoftAP, then LISTEN and push real camera JPEG frames.
#
# Direction-B experiment. Direction A (K230 opens the connection to the P4) dies
# with OSError(107)/ENOTCONN on the K230 even though association and DHCP are fine.
# Here the K230 only ACCEPTS, and the P4 dials out -- so if frames flow, the data
# path on the current (version-mismatched) stack is fine and plan D can proceed
# without doing the co-processor OTA first. If it also fails, the OTA really is the
# prerequisite and we stop guessing.
#
# Run:  powershell -File ..\..\tools\k230_repl.ps1 -Port COM3 `
#         -File k230_jpeg_server.py -Paste -SoftReset -PasteWaitMs 90000
#
# Frame format (same as the P4 sink): 'J' 'F' | uint32 LE length | JPEG bytes
import network, socket, time, gc, uctypes, os
from media.sensor import *
from media.vencoder import *
from media.media import *

SSID    = "P4_STREAM"
KEY     = "<AP_PSK>"
PORT    = 5001
WIDTH   = 640
HEIGHT  = 480
RUN_MS  = 20000          # self-terminating so the capture window sees the summary

gc.collect()
print("SRV mem_free=%d" % gc.mem_free())

sta = network.WLAN(network.STA_IF)
seen = False
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < 25000:
    try:
        for a in sta.scan():
            if a.ssid == SSID.encode():
                print("SRV target visible ch=%s rssi=%s" % (a.channel, a.rssi))
                seen = True
                break
    except Exception as e:
        print("SRV scan raised", repr(e))
    if seen:
        break
    time.sleep_ms(500)
if not seen:
    print("SRV RESULT: FAIL (AP not visible)")
    raise SystemExit

ok = False
for attempt in range(3):
    try:
        sta.disconnect()
    except Exception:
        pass
    time.sleep_ms(500)
    print("SRV connect attempt %d" % (attempt + 1))
    try:
        sta.connect(SSID, KEY)
    except Exception as e:
        print("SRV connect() raised", repr(e))
    t1 = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), t1) < 12000:
        if sta.isconnected() and sta.ifconfig()[0] != "0.0.0.0":
            ok = True
            break
        time.sleep_ms(200)
    if ok:
        break
if not ok:
    print("SRV RESULT: FAIL (association timeout, status=%s)" % str(sta.status()))
    raise SystemExit
my_ip = sta.ifconfig()[0]
print("SRV associated ifconfig=", sta.ifconfig())
if not my_ip.startswith("192.168.7."):
    print("SRV WARNING: not in the P4 AP subnet -- stale lease?")

# ---- listen BEFORE starting the camera, so the P4 can dial in while we set up ----
srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
try:
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
except Exception:
    pass
addr = socket.getaddrinfo("0.0.0.0", PORT)[0][-1]
srv.bind(addr)
srv.listen(1)
print("SRV listening on %s:%d -- P4 should dial in" % (my_ip, PORT))

sensor = None
encoder = None
link = None
cs = None
result = "FAIL"
try:
    cs, peer = srv.accept()          # blocks until the P4 connects
    print("SRV accepted from", peer)

    width = ALIGN_UP(WIDTH, 16)
    sensor = Sensor()
    sensor.reset()
    sensor.set_framesize(width=width, height=HEIGHT, alignment=12)
    sensor.set_pixformat(Sensor.YUV420SP)
    encoder = Encoder()
    encoder.SetOutBufs(8, width, HEIGHT)
    # profile=H264_PROFILE_MAIN is what the JPEG payload accepts (measured, see README 10.8)
    chnAttr = ChnAttrStr(encoder.PAYLOAD_TYPE_JPEG, encoder.H264_PROFILE_MAIN, width, HEIGHT)
    streamData = StreamData()
    encoder.Create(chnAttr)
    link = MediaManager.link(sensor.bind_info()['src'],
                             (VIDEO_ENCODE_MOD_ID, VENC_DEV_ID, encoder.chn))
    encoder.Start()
    sensor.run()
    print("SRV camera+encoder running, streaming for %d ms" % RUN_MS)

    n = 0
    total = 0
    err = None
    t2 = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), t2) < RUN_MS:
        os.exitpoint()
        encoder.GetStream(streamData)
        size = 0
        parts = []
        for i in range(streamData.pack_cnt):
            b = uctypes.bytearray_at(streamData.data[i], streamData.data_size[i])
            parts.append(bytes(b))       # copy: the view dies at ReleaseStream
            size += len(parts[-1])
        encoder.ReleaseStream(streamData)
        try:
            hdr = b"JF" + size.to_bytes(4, "little")
            off = 0
            mv = memoryview(hdr)
            while off < len(hdr):
                off += cs.send(mv[off:])
            for p in parts:
                off = 0
                mv = memoryview(p)
                while off < len(p):
                    off += cs.send(mv[off:])
        except Exception as e:
            err = repr(e)
            break
        n += 1
        total += size + 6
    el = time.ticks_diff(time.ticks_ms(), t2)
    print("SRV sent %d frames %d bytes in %d ms -> %.2f fps %.2f Mbps" %
          (n, total, el, (n * 1000.0 / el) if el else 0,
           (total * 8.0 / el / 1000.0) if el else 0))
    if err:
        print("SRV send error:", err)
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

print("SRV RESULT: %s" % result)
