# K230 -> P4 link throughput test (NO camera involved on purpose).
#
# Step 1 of the video link: prove Wi-Fi STA + TCP to the P4's SoftAP and MEASURE
# the achievable bitrate with synthetic frames. Keeping the camera and the JPEG
# encoder out of it means a bad number here can only be the radio/SDIO path --
# one variable at a time (repo methodology).
#
# Run it by pasting into the CanMV REPL:
#   powershell -File ..\..\tools\k230_repl.ps1 -Port COM3 -File k230_link_test.py -Paste -PasteWaitMs 40000
#
# Wire format matches the P4 sink: 'J' 'F' | uint32 LE length | payload
# Payload is padded with real JPEG SOI/EOI markers so the sink's sanity check
# stays happy and we are not measuring "sink rejects garbage" instead of speed.
import network, socket, time, gc

gc.collect()
print("LINK mem_free=%d" % gc.mem_free())

SSID   = "P4_STREAM"
KEY    = "<AP_PSK>"
HOST   = "192.168.7.1"   # AP moved off 192.168.4.x on purpose (see softap_example_main.c)
PORT   = 5000
FRAME  = 40 * 1024          # ~ a 640x480 JPEG at decent quality
RUN_MS = 10000              # self-terminating: the capture window must see the summary

sta = network.WLAN(network.STA_IF)

# MEASURED: right after machine.reset() the WLAN driver is not ready yet and
# connect() just times out, while the exact same call works on a board that has
# been up for a while. So: wait until the AP is actually visible in a scan (that
# doubles as "radio is up"), and only then associate -- with retries.
seen = False
t0 = time.ticks_ms()
while time.ticks_diff(time.ticks_ms(), t0) < 25000:
    try:
        for a in sta.scan():
            if a.ssid == SSID.encode():
                print("LINK target visible ch=%s rssi=%s security=%s" % (a.channel, a.rssi, a.security))
                seen = True
                break
    except Exception as e:
        print("LINK scan raised", repr(e))
    if seen:
        break
    time.sleep_ms(500)
if not seen:
    print("LINK RESULT: FAIL (AP %s not visible in scan -- is the P4 powered and running the softap build?)" % SSID)
    raise SystemExit

ok = False
for attempt in range(3):
    try:
        sta.disconnect()
    except Exception:
        pass
    time.sleep_ms(500)
    print("LINK connect attempt %d" % (attempt + 1))
    try:
        sta.connect(SSID, KEY)
    except Exception as e:
        print("LINK connect() raised", repr(e))
    t0 = time.ticks_ms()
    while time.ticks_diff(time.ticks_ms(), t0) < 12000:
        if sta.isconnected() and sta.ifconfig()[0] != "0.0.0.0":
            ok = True
            break
        time.sleep_ms(200)
    if ok:
        break
if not ok:
    print("LINK RESULT: FAIL (association timeout after 3 attempts, status=%s)" % str(sta.status()))
    raise SystemExit
print("LINK associated ifconfig=", sta.ifconfig())
# The AP hands out 192.168.7.x; a 192.168.4.x address here would mean we are
# talking to the car's ESP-01S AP instead (both used to serve 192.168.4.0/24).
if not sta.ifconfig()[0].startswith("192.168.7."):
    print("LINK WARNING: address is not in the P4 AP subnet -- stale lease?")

payload = b"\xff\xd8" + (b"\x5a" * (FRAME - 4)) + b"\xff\xd9"
hdr = b"JF" + len(payload).to_bytes(4, "little")

# MEASURED: the CanMV socket layer wants the canonical form from
# examples/14-Socket/tcp_client.py -- socket(..., 0) with an explicit proto AND an
# address from getaddrinfo(). Passing a plain ("ip", port) tuple to connect() fails
# with OSError(107) even though the interface is up and has a DHCP lease.
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0)
try:
    ai = socket.getaddrinfo(HOST, PORT)
    addr = ai[0][-1]
    print("LINK addrinfo", ai[0])
    s.connect(addr)
except Exception as e:
    print("LINK RESULT: FAIL (tcp connect to %s:%d -> %s)" % (HOST, PORT, repr(e)))
    raise SystemExit
print("LINK tcp connected to %s:%d, pushing %d B frames for %d ms" % (HOST, PORT, FRAME, RUN_MS))

def send_all(sock, buf):
    mv = memoryview(buf)
    off = 0
    while off < len(buf):
        k = sock.send(mv[off:])
        if k is None or k <= 0:
            raise OSError("send returned %s" % str(k))
        off += k

n = 0
t_start = time.ticks_ms()
err = None
while time.ticks_diff(time.ticks_ms(), t_start) < RUN_MS:
    try:
        send_all(s, hdr)
        send_all(s, payload)
    except Exception as e:
        err = repr(e)
        break
    n += 1
el = time.ticks_diff(time.ticks_ms(), t_start)
try:
    s.close()
except Exception:
    pass

total = n * (len(payload) + 6)
mbps = (total * 8) / (el * 1000.0) if el else 0
fps = (n * 1000.0 / el) if el else 0
print("LINK frames=%d bytes=%d ms=%d -> %.2f fps  %.2f Mbps" % (n, total, el, fps, mbps))
if err:
    print("LINK send error:", err)
print("LINK RESULT: %s" % ("PASS" if n > 0 and not err else "FAIL"))
