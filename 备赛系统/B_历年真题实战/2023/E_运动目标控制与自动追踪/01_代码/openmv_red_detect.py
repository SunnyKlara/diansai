# 2023E OpenMV红色光斑检测脚本（绿色追踪系统用）
#
# 【独到设计】
# 不只是检测位置，还要计算速度！
# 速度信息用于前馈预测，补偿系统延迟。
#
# 普通做法：只发送(cx, cy)
# 我的做法：发送(cx, cy, vx, vy)
# vx, vy是光斑的运动速度（像素/帧）
# 追踪器用速度做100ms的前馈预测，大幅减少滞后
#
# 【另一个关键点】
# 红色激光光斑在白色屏幕上非常亮，容易过曝。
# 需要降低曝光时间，让光斑成为图像中唯一的亮点。
# 这样用亮度阈值就能精确检测，不需要颜色阈值。

import sensor, image, time
from pyb import UART

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)  # 320x240
sensor.skip_frames(time=2000)

# 【关键】降低曝光，让激光光斑成为唯一亮点
sensor.set_auto_exposure(False)
sensor.set_auto_gain(False)
sensor.set_auto_whitebal(False)
sensor.set_brightness(-2)       # 降低亮度
sensor.set_contrast(3)          # 提高对比度
# 如果还是太亮，手动设置曝光时间：
# sensor.__write_reg(0x0C, 0x04)  # 具体寄存器因传感器型号而异

uart = UART(3, 115200)

# 图像中心
CX, CY = 160, 120

# 上一帧的位置（用于计算速度）
prev_x, prev_y = 0.0, 0.0
prev_valid = False

# 低通滤波器（平滑速度估计）
vx_filtered, vy_filtered = 0.0, 0.0
ALPHA = 0.3  # 滤波系数，越小越平滑

clock = time.clock()

while True:
    clock.tick()
    img = sensor.snapshot()

    # 【检测策略】
    # 方案1：用亮度阈值检测（推荐，因为降低了曝光）
    # 红色激光在低曝光下是图像中最亮的点
    blobs = img.find_blobs([(80, 100, 20, 127, 20, 127)],  # 高亮度+偏红
                           pixels_threshold=3,
                           area_threshold=3,
                           merge=True)

    if blobs:
        # 找最亮/最大的blob
        b = max(blobs, key=lambda b: b.pixels())
        cx = b.cx() - CX  # 相对图像中心的偏移
        cy = b.cy() - CY

        # 计算速度
        if prev_valid:
            raw_vx = cx - prev_x
            raw_vy = cy - prev_y
            # 低通滤波
            vx_filtered = ALPHA * raw_vx + (1 - ALPHA) * vx_filtered
            vy_filtered = ALPHA * raw_vy + (1 - ALPHA) * vy_filtered
        else:
            vx_filtered = 0
            vy_filtered = 0

        prev_x, prev_y = cx, cy
        prev_valid = True

        # 发送：位置 + 速度
        msg = "$%.1f,%.1f,%.1f,%.1f\n" % (cx, cy, vx_filtered, vy_filtered)
        uart.write(msg)

        # 调试：画十字和速度箭头
        img.draw_cross(b.cx(), b.cy(), color=(0,255,0), size=10)
        img.draw_arrow(b.cx(), b.cy(),
                       b.cx()+int(vx_filtered*5), b.cy()+int(vy_filtered*5),
                       color=(255,255,0))
    else:
        prev_valid = False
        uart.write("$0,0,0,0\n")  # 未检测到

    # 目标帧率：30fps以上
    # print(clock.fps())
