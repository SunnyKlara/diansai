# -*- coding: utf-8 -*-
"""
生成 16x16 中文字库头文件 cn_font.h（供 OLED 显示中文用）。
取模格式与 oled.c 的 OLED_ShowChinese 一致：
  - 16x16，每字 32 字节
  - 列扫描，分两页(上8行/下8行)，字节内 LSB(bit0) 在上
  - 数组下标 i = page*16 + col
用法: python gen_cn_font.py
"""
from PIL import Image, ImageFont, ImageDraw

FONT_PATH = r"C:/Windows/Fonts/simhei.ttf"
FONT_SIZE = 16
OUT = r"c:/Users/Klara/Desktop/diansai/workbench/Main/Core/Inc/cn_font.h"

# (汉字, 宏名) —— 顺序即数组下标
CHARS = [
    ("定", "CN_DING"), ("高", "CN_GAO"), ("控", "CN_KONG"), ("制", "CN_ZHI"),
    ("手", "CN_SHOU"), ("动", "CN_DONG"), ("标", "CN_BIAO"), ("度", "CN_DU"),
    ("曲", "CN_QU"),   ("线", "CN_XIAN"), ("起", "CN_QI"),   ("飞", "CN_FEI"),
    ("停", "CN_TING"), ("止", "CN_ZHI2"), ("目", "CN_MU"),   ("步", "CN_BU"),
    ("长", "CN_CHANG"),("当", "CN_DANG"), ("前", "CN_QIAN"),
]

def glyph_bytes(ch, font):
    img = Image.new("1", (16, 16), 0)
    d = ImageDraw.Draw(img)
    bbox = d.textbbox((0, 0), ch, font=font)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    x = (16 - w) // 2 - bbox[0]
    y = (16 - h) // 2 - bbox[1]
    d.text((x, y), ch, font=font, fill=1)
    out = []
    for page in range(2):
        for col in range(16):
            b = 0
            for bit in range(8):
                row = page * 8 + bit
                if img.getpixel((col, row)):
                    b |= (1 << bit)
            out.append(b)
    return out

def main():
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    lines = []
    lines.append("#ifndef __CN_FONT_H")
    lines.append("#define __CN_FONT_H")
    lines.append("// 16x16 中文字库（PIL+simhei 自动生成，勿手改；改字见 tools/gen_cn_font.py）")
    lines.append("")
    for i, (ch, name) in enumerate(CHARS):
        lines.append("#define %-10s %d  // %s" % (name, i, ch))
    lines.append("")
    lines.append("")
    lines.append("// 字模数据较大，只在定义了 CN_FONT_IMPL 的源文件(oled.c)里实例化；")
    lines.append("// 其它文件只引用上面的 CN_xxx 索引宏，避免重复占用 Flash / 未用告警。")
    lines.append("#ifdef CN_FONT_IMPL")
    lines.append("const unsigned char cn16[][32] = {")
    for ch, name in CHARS:
        bs = glyph_bytes(ch, font)
        hexs = ",".join("0x%02X" % b for b in bs)
        lines.append("    {%s}, // %s" % (hexs, ch))
    lines.append("};")
    lines.append("#else")
    lines.append("extern const unsigned char cn16[][32];")
    lines.append("#endif")
    lines.append("")
    lines.append("#endif /* __CN_FONT_H */")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("written:", OUT, "chars:", len(CHARS))

if __name__ == "__main__":
    main()
