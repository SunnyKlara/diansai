# OpenMV 棋盘扫描脚本
# 功能：俯拍棋盘，识别9个方格中的棋子颜色
# 输出：通过串口发送 "$b0,b1,...,b8\n"
#   bi: 0=空, 1=黑棋, 2=白棋

import sensor, image, time
from pyb import UART, LED

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)  # 320x240
sensor.skip_frames(time=2000)
sensor.set_auto_gain(False)
sensor.set_auto_whitebal(False)

uart = UART(3, 115200)

# ===== 颜色阈值（需要现场标定！） =====
# 黑棋：深色
BLACK_THRESHOLD = (0, 40, -20, 20, -20, 20)
# 白棋：浅色
WHITE_THRESHOLD = (70, 100, -20, 20, -20, 20)
# 棋盘背景：中间色调（根据实际棋盘颜色调整）

# ===== 棋盘定位 =====
# 方法：检测棋盘四个角的黑色边线交叉点
# 简化版：预设棋盘在图像中的位置（需要固定摄像头位置）

# 预设9个方格中心的像素坐标（需要实际标定）
# 格式：(cx, cy)
GRID_CENTERS = [
    (100, 60),  (160, 60),  (220, 60),   # 格1,2,3
    (100, 120), (160, 120), (220, 120),   # 格4,5,6
    (100, 180), (160, 180), (220, 180),   # 格7,8,9
]

# 检测区域大小（像素）
ROI_SIZE = 20

def scan_board(img):
    """扫描棋盘，返回9个格子的状态"""
    result = [0] * 9

    for i in range(9):
        cx, cy = GRID_CENTERS[i]
        roi = (cx - ROI_SIZE//2, cy - ROI_SIZE//2, ROI_SIZE, ROI_SIZE)

        # 获取ROI区域的颜色统计
        stats = img.get_statistics(roi=roi)
        l_mean = stats.l_mean()  # 亮度均值

        # 判断棋子颜色
        if l_mean < 40:
            result[i] = 1   # 黑棋（暗）
        elif l_mean > 70:
            result[i] = 2   # 白棋（亮）
        else:
            result[i] = 0   # 空（棋盘背景色）

        # 在图像上标注
        color = (0,255,0) if result[i]==0 else (255,0,0) if result[i]==1 else (0,0,255)
        img.draw_rectangle(roi, color=color)
        img.draw_string(cx-5, cy-5, str(result[i]), color=color, scale=2)

    return result

# ===== 主循环 =====
while True:
    # 等待STM32发送扫描命令
    if uart.any():
        cmd = uart.readline()
        if cmd and b"SCAN" in cmd:
            # 拍照并分析
            img = sensor.snapshot()
            board = scan_board(img)

            # 发送结果
            msg = "$%d,%d,%d,%d,%d,%d,%d,%d,%d\n" % tuple(board)
            uart.write(msg)

            # LED闪烁表示完成
            LED(1).on()
            time.sleep_ms(100)
            LED(1).off()
