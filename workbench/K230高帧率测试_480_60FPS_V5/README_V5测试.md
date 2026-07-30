# K230 480模型高帧率V5测试

## 改动内容

V5保留原480模型、位置换算、滤波和UART协议，仅修改两处：

1. 显式创建`1920×1080@60FPS`的GC2093传感器。
2. 使用`to_ide=False`彻底关闭底层USBDBG视频传输。

V4稳定版保留在包内，V5不会自动覆盖V4或K230的`main.py`。

## 第一次测试保存位置

把V5脚本上传到：

```text
/sdcard/steelball/fixed_pipe_480_v1/fixed_pipe_position_uart_60fps_v5.py
```

模型和校准文件继续使用：

```text
/sdcard/steelball/fixed_pipe_480_v1/fixed_pipe_ball_480_v1.kmodel
/sdcard/steelball/fixed_pipe_480_v1/position_calibration.json
```

第一次测试不要覆盖`/sdcard/main.py`。

## 正确启动标志

传感器日志必须显示：

```text
find sensor gc2093_csi2 ... output 1920x1080@60
```

程序必须显示：

```text
PIPE_BALL_UART_60FPS_V5_READY ... to_ide=0
```

状态行格式：

```text
BALL_UART_V5 valid=1 ... fps=xx.xx uart_hz=xx.xx tx=... heap_kb=... uart_ms_max=...
```

## 测试标准

至少运行到`tx=5000`，约2～3分钟。

- `fps`与`uart_hz`应基本相同。
- `heap_kb`不能持续下降。
- `uart_ms_max`应维持在较低水平。
- 固定钢球位置不应出现明显跳变。
- 左端、中点、右端位置方向和数值应正确。

帧率判断：

- 仍约23～24FPS：60FPS传感器没有带来收益，进入416×160 ROI模型方案。
- 约30～34FPS：有提升，但仍建议继续做ROI模型。
- 达到约35FPS以上：V5具有直接比赛使用价值。

## 通过测试后

先备份旧`/sdcard/main.py`，然后将V5脚本另存为：

```text
/sdcard/main.py
```

复位K230，确认不用点击IDE运行按钮也会自动启动。

## 回退

如V5无法初始化、位置映射异常或稳定性下降，立即恢复包内：

```text
fixed_pipe_position_uart_stable_v4_backup.py
```

V4已知稳定工作在约23～24FPS。

## 注意

`to_ide=False`后，CanMV右侧预览不再实时刷新。这是正常现象，应通过终端状态行和控制板UART判断运行状态。
