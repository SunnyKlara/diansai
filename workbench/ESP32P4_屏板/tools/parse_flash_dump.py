# -*- coding: utf-8 -*-
"""解析 esptool dump: flash 0x8000 起 0x8100 字节 = 分区表 + app image 头 + esp_app_desc_t"""
import struct
import sys

PATH = r"d:\diansai\.tmp_pdf\esp32p4\dump_part_app.bin"
data = open(PATH, "rb").read()

PART_TYPE = {0: "app", 1: "data"}
PART_SUB = {
    (0, 0): "factory", (0, 0x10): "ota_0", (0, 0x11): "ota_1", (0, 0x20): "test",
    (1, 0): "otadata", (1, 1): "phy", (1, 2): "nvs", (1, 3): "coredump",
    (1, 4): "nvs_keys", (1, 0x81): "fat", (1, 0x82): "spiffs", (1, 0x83): "littlefs",
}

print("=== 分区表 (flash 0x8000) ===")
off = 0
while off + 32 <= len(data):
    entry = data[off:off + 32]
    if entry[:2] != b"\xaa\x50":
        break
    ptype, psub = entry[2], entry[3]
    poff, psize = struct.unpack("<II", entry[4:12])
    label = entry[12:28].rstrip(b"\x00").decode("utf-8", "replace")
    tname = PART_TYPE.get(ptype, "0x%02x" % ptype)
    sname = PART_SUB.get((ptype, psub), "0x%02x" % psub)
    print("  %-10s %-5s %-9s off=0x%06x size=0x%06x (%d KB)"
          % (label, tname, sname, poff, psize, psize // 1024))
    off += 32

APP = 0x8000  # flash 0x10000
hdr = data[APP:APP + 24]
magic, seg_cnt, spi_mode, spi_ss = hdr[0], hdr[1], hdr[2], hdr[3]
entry_addr, = struct.unpack("<I", hdr[4:8])
chip_id, = struct.unpack("<H", hdr[12:14])
min_rev_full, = struct.unpack("<H", hdr[15:17])
max_rev_full, = struct.unpack("<H", hdr[17:19])
CHIP = {0: "ESP32", 2: "ESP32-S2", 5: "ESP32-C3", 9: "ESP32-S3",
        13: "ESP32-C6", 16: "ESP32-H2", 18: "ESP32-P4"}
print()
print("=== app image 头 (flash 0x10000) ===")
print("  magic=0x%02X (期望 0xE9)  段数=%d  entry=0x%08x" % (magic, seg_cnt, entry_addr))
print("  chip_id=%d (%s)  min_rev=v%d.%d  max_rev=v%d.%d"
      % (chip_id, CHIP.get(chip_id, "?"),
         min_rev_full // 100, min_rev_full % 100,
         max_rev_full // 100, max_rev_full % 100))

d = data[APP + 24 + 8: APP + 24 + 8 + 256]
dmagic, = struct.unpack("<I", d[0:4])
print()
print("=== esp_app_desc_t ===")
if dmagic != 0xABCD5432:
    print("  magic=0x%08X 不是 0xABCD5432 —— 解析失败" % dmagic)
    sys.exit(0)


def s(b):
    return b.rstrip(b"\x00").decode("utf-8", "replace")


print("  工程名   : %s" % s(d[16:48]))
print("  版本     : %s" % s(d[48:80]))
print("  编译时间 : %s %s" % (s(d[96:112]), s(d[80:96])))
print("  IDF 版本 : %s" % s(d[112:144]))
print("  ELF SHA256(前8字节): %s" % d[144:152].hex())
