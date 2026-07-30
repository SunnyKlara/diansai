# K230 固定管钢球位置识别——比赛交付 V4

## 当前结论

V4 实机测试结果符合比赛使用要求：

- 稳定识别速度约 23.7～24.1 FPS。
- UART 实际发送频率与识别频率一致，约 23.7～24.1 Hz。
- 剩余内存约 3717～3747 KB，未持续下降。
- UART 单次写入最大约 2～5 ms，未发现写入阻塞。
- 固定钢球位置的短时波动约 0.02 cm。
- 运行到约 1900 帧后出现的是 USB/IDE 连接断开，日志中没有 Python 异常，也没有内存耗尽迹象。

## K230 最终目录

```text
/sdcard/
├── main.py
├── libs/
│   ├── AIBase.py
│   ├── PipeLine.py
│   ├── Utils.py
│   └── YOLO.py
└── steelball/
    └── fixed_pipe_480_v1/
        ├── fixed_pipe_ball_480_v1.kmodel
        ├── fixed_pipe_position_uart_stable_v4.py
        └── position_calibration.json
```

`main.py`和`fixed_pipe_position_uart_stable_v4.py`内容相同。前者用于上电自动运行，后者用于保留版本名称和手动调试。

## 部署顺序

1. 先备份K230现有的`/sdcard/main.py`。
2. 保留K230当前已经验证可用的`/sdcard/libs`，不要随意覆盖。
3. 将模型复制到：

   ```text
   /sdcard/steelball/fixed_pipe_480_v1/fixed_pipe_ball_480_v1.kmodel
   ```

4. 保留当前K230中已经校准成功的：

   ```text
   /sdcard/steelball/fixed_pipe_480_v1/position_calibration.json
   ```

   本交付包也提供了本次实测校准文件的备份。

5. 调试时，将V4脚本保存到：

   ```text
   /sdcard/steelball/fixed_pipe_480_v1/fixed_pipe_position_uart_stable_v4.py
   ```

6. 确认手动运行稳定后，将同一脚本复制或另存为：

   ```text
   /sdcard/main.py
   ```

7. 断电重启或按复位键，检查是否自动出现：

   ```text
   POSITION_UART_READY
   CALIBRATION_LOADED
   PIPE_BALL_UART_STABLE_V4_READY
   ```

## 不要直接覆盖的文件

- K230上现有且已经正常工作的`/sdcard/libs`。
- 已经校准成功的`position_calibration.json`。
- 未备份的旧`/sdcard/main.py`。

`可选_板端libs_仅缺失时使用`目录只是缺少库文件时报错时的备用，不是要求强制覆盖。

## 比赛前快速检查

- 摄像头与管子没有相对移动。
- `PREVIEW_ENABLE = False`。
- `fps`稳定在约23～24。
- `uart_hz`与`fps`基本一致。
- `heap_kb`没有持续下降。
- `uart_ms_max`通常不超过5ms。
- 控制板能正确忽略`valid=0`。
- K230和控制板共地。
- 舵机不能从K230的USB电源直接取电。

详细说明见`03_说明文档`目录。
