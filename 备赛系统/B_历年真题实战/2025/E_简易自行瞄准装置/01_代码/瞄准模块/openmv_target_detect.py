# OpenMV 靶标检测脚本
# 功能：检测靶心位置，通过串口发送给STM32
#
# 靶标特征：
#   - A4紫外感光纸，四周有1.8cm黑色胶带边框
#   - 中心有红色靶心点
#   - 同心圆（红色，半径2/4/6/8/10cm）
#
# 输出格式: "$cx,cy,found\n"
#   cx: 靶心相对图像中心的X偏移（像素，右为正）
#   cy: 靶心相对图像中心的Y偏移（像素，下为正）
#   found: 1=检测到, 0=未检测到

import sensor, image, time
from pyb import UART

# ===== 初始化 =====
sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)  # 320x240
sensor.skip_frames(time=2000)
sensor.set_auto_gain(False)
sensor.set_auto_whitebal(False)

uart = UART(3, 115200)  # UART3: P4=TX, P5=RX

# 图像中心
IMG_CX = 160
IMG_CY = 120

# ===== 靶标检测方案 =====

# 方案1：检测红色靶心点（简单可靠）
RED_THRESHOLD = (30, 80, 40, 80, 10, 60)  # LAB颜色阈值，需要现场调

# 方案2：检测黑色边框矩形，取中心（更鲁棒）
BLACK_THRESHOLD = (0, 30, -20, 20, -20, 20)

def detect_target_by_red(img):
    """通过红色靶心点检测"""
    blobs = img.find_blobs([RED_THRESHOLD],
                           pixels_threshold=10,
                           area_threshold=10,
                           merge=True)
    if blobs:
        # 找最大的红色色块
        largest = max(blobs, key=lambda b: b.pixels())
        cx = largest.cx() - IMG_CX
        cy = largest.cy() - IMG_CY
        return cx, cy, 1
    return 0, 0, 0

def detect_target_by_rect(img):
    """通过黑色边框矩形检测（备用方案）"""
    # 找矩形
    for r in img.find_rects(threshold=10000):
        # A4纸的长宽比约1.414
        w, h = r.w(), r.h()
        if w > 50 and h > 50:  # 过滤太小的
            ratio = max(w,h) / min(w,h)
            if 1.2 < ratio < 1.6:  # 接近A4比例
                cx = r.x() + r.w()//2 - IMG_CX
                cy = r.y() + r.h()//2 - IMG_CY
                return cx, cy, 1
    return 0, 0, 0

# ===== 主循环 =====
clock = time.clock()

while True:
    clock.tick()
    img = sensor.snapshot()

    # 优先用红色检测
    cx, cy, found = detect_target_by_red(img)

    # 红色没找到就用矩形检测
    if not found:
        cx, cy, found = detect_target_by_rect(img)

    # 发送数据
    msg = "$%d,%d,%d\n" % (cx, cy, found)
    uart.write(msg)

    # 调试：在图像上画十字
    if found:
        img.draw_cross(cx + IMG_CX, cy + IMG_CY, color=(0,255,0), size=10)

    # print(clock.fps())  # 调试时打开看帧率
