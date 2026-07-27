# K230: camera -> HARDWARE JPEG encoder, measured locally. NO network involved.
#
# Why this exists / why it is standalone:
#   Plan D needs MJPEG because the P4 can only hardware-DECODE JPEG. On the K230
#   that means media.vencoder with PAYLOAD_TYPE_JPEG -- and NONE of the ~30 example
#   folders on the SD card ever uses that payload type, so the ChnAttrStr() call is
#   the one genuinely unverified API in the whole chain. Testing it with the network
#   out of the picture keeps it a single variable (the K230->P4 TCP path is a
#   separate, currently-blocked problem).
#
# Run:  powershell -File ..\..\tools\k230_repl.ps1 -Port COM3 `
#         -File k230_jpeg_test.py -Paste -SoftReset -PasteWaitMs 60000
#
# What it reports: encode fps, JPEG frame size min/avg/max, FFD8..FFD9 marker check,
# and it drops the first frame on the SD card as physical proof.
import time, os, gc, uctypes
from media.sensor import *
from media.vencoder import *
from media.media import *

WIDTH   = 640
HEIGHT  = 480
FRAMES  = 60                       # ~2 s at 30 fps
OUT_JPG = "/sdcard/jpeg_test.jpg"

gc.collect()
print("JPEG mem_free=%d" % gc.mem_free())

width = ALIGN_UP(WIDTH, 16)
sensor = None
encoder = None
link = None
result = "FAIL"

try:
    sensor = Sensor()
    sensor.reset()
    sensor.set_framesize(width=width, height=HEIGHT, alignment=12)
    sensor.set_pixformat(Sensor.YUV420SP)
    print("JPEG sensor configured %dx%d YUV420SP" % (width, HEIGHT))

    encoder = Encoder()
    encoder.SetOutBufs(8, width, HEIGHT)

    # The profile argument is meaningless for JPEG but the constructor still wants
    # one; try the documented H264 constant first, fall back to 0.
    chnAttr = None
    for prof in (encoder.H264_PROFILE_MAIN, 0):
        try:
            chnAttr = ChnAttrStr(encoder.PAYLOAD_TYPE_JPEG, prof, width, HEIGHT)
            print("JPEG ChnAttrStr accepted profile=%s" % str(prof))
            break
        except Exception as e:
            print("JPEG ChnAttrStr(profile=%s) raised %s" % (str(prof), repr(e)))
    if chnAttr is None:
        raise RuntimeError("ChnAttrStr rejected every profile value")

    streamData = StreamData()
    encoder.Create(chnAttr)
    link = MediaManager.link(sensor.bind_info()['src'],
                             (VIDEO_ENCODE_MOD_ID, VENC_DEV_ID, encoder.chn))
    encoder.Start()
    sensor.run()
    print("JPEG pipeline running, grabbing %d frames" % FRAMES)

    n = 0
    total = 0
    smin = 1 << 30
    smax = 0
    first = None
    markers_ok = None
    t0 = time.ticks_ms()
    while n < FRAMES:
        os.exitpoint()
        encoder.GetStream(streamData)
        size = 0
        for i in range(streamData.pack_cnt):
            b = uctypes.bytearray_at(streamData.data[i], streamData.data_size[i])
            size += len(b)
            if first is None:
                first = bytes(b)          # copy before ReleaseStream invalidates it
        encoder.ReleaseStream(streamData)
        if first is not None and markers_ok is None:
            markers_ok = (first[0] == 0xFF and first[1] == 0xD8 and
                          first[-2] == 0xFF and first[-1] == 0xD9)
        n += 1
        total += size
        if size < smin:
            smin = size
        if size > smax:
            smax = size
    el = time.ticks_diff(time.ticks_ms(), t0)

    print("JPEG %d frames in %d ms -> %.2f fps" % (n, el, n * 1000.0 / el if el else 0))
    print("JPEG frame bytes min=%d avg=%d max=%d" % (smin, total // n, smax))
    print("JPEG first-frame markers FFD8..FFD9 = %s (len=%d)" %
          (str(markers_ok), len(first) if first else 0))
    # bitrate the link would have to carry at this size/rate
    print("JPEG implied bitrate at measured fps = %.2f Mbps" %
          ((total * 8.0 / el / 1000.0) if el else 0))

    if first:
        f = open(OUT_JPG, "wb")
        f.write(first)
        f.close()
        print("JPEG wrote first frame to %s" % OUT_JPG)

    result = "PASS" if (markers_ok and n == FRAMES) else "FAIL"
except Exception as e:
    import sys
    sys.print_exception(e)
finally:
    try:
        sensor.stop()
    except Exception:
        pass
    try:
        link.destroy()
    except Exception:
        pass
    try:
        encoder.Stop()
        encoder.Destroy()
    except Exception:
        pass
    gc.collect()

print("JPEG RESULT: %s" % result)
