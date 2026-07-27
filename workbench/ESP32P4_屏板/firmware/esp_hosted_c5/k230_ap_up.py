# K230 as the ACCESS POINT (topology C).
#
# Why: with the P4/C5 acting as AP, the P4's own log shows WIFI_EVENT_HOME_CHANNEL_CHANGE
# firing in pairs every ~15.58 s (measured 15584/15580/15585/15585 ms) with nothing of
# ours asking for it -- i.e. that AP keeps leaving its channel, which explains why the
# K230's association only succeeded twice out of many tries. Rather than keep fighting
# it on a stack we already know is version-mismatched, flip the roles: the K230 becomes
# the AP and the P4 becomes the station -- and the P4-as-station path is already PROVEN
# on this hardware (it associated to an ESP-01S AP and pinged it 5/5, README §9.7).
#
# Run:  powershell -File ..\..\tools\k230_repl.ps1 -Port COM3 -File k230_ap_up.py -Paste -SoftReset
#
# The RT-Smart network stack stays up after the script ends (and even across a
# MicroPython soft reboot), so the AP keeps beaconing while the P4 gets reflashed.
import network, time

SSID = "K230_AP"
KEY  = "<K230_AP_PSK>"     # >= 8 chars, WPA2

ap = network.WLAN(network.AP_IF)
try:
    ap.config(ssid=SSID, key=KEY)
    print("AP config ok ssid=%s" % SSID)
except Exception as e:
    print("AP config raised", repr(e))

time.sleep(2)
for fn in ("info", "status", "ifconfig", "active"):
    try:
        print("AP %s -> %s" % (fn, str(getattr(ap, fn)())))
    except Exception as e:
        print("AP %s FAIL %s" % (fn, repr(e)))
print("AP_UP_DONE  -- point the P4 station at %s (its gateway is the address above)" % SSID)
