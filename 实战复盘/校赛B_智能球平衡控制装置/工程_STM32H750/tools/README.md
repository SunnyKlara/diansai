# tools 用途速查（只有 3 个脚本真正在用）

调试就这一条流水线：**改码 → `build_flash` 编译烧录 → `serial_tx` 看反馈 / `tune_step` 整定**。

| 脚本 | 干啥 | 典型用法 |
|---|---|---|
| `build_flash.ps1` | 用 Keil UV4 命令行编译 / 烧录（走 ST-Link）。**运行前先关掉 Keil GUI** | `build_flash.ps1 -Action build`（编译）<br>`build_flash.ps1 -Action flash`（烧录） |
| `serial_tx.ps1` | 一次性串口事务：发命令 + 看 N 秒回报。手动调参、查状态用这个 | `serial_tx.ps1 -Port COM8 -Cmd "q" -Watch 3`（查参数）<br>`serial_tx.ps1 -Port COM8 -Cmd "m1800" -Watch 4`（手动给PWM） |
| `tune_step.ps1` | 一键闭环整定：预热风扇→交闭环→采集→自动分析（含**反馈健康度** `FEEDBACK:` 行） | `tune_step.ps1 -Setup "uh1800;kp0;ki0;kd120;f0.4;c1500;l800;r10" -Target 15 -CaptureS 12` |
| `gen_cn_font.py` | 一次性工具：生成 OLED 中文字库 `cn_font.h`。**不是调试用**，平时不碰 | — |

## 串口命令表（板子认的单字符命令，serial_tx/tune_step 都靠它）

- `mNNN` 手动直给 PWM（0~9600）/ `a` 回自动闭环 / `s` 停 / `g` 启
- `tNN` 设目标高度(cm) / `q` 打印当前全部参数
- 在线调参（免烧录）：`uhNNNN`(悬停前馈) `kpNN` `kiNN` `kdNN` `fN.N`(球速EMA) `lNNN`(PWM斜率限幅) `cNNNN`(D项钳位) `rNN`(目标爬升速率)

## 心跳字段（板子每 80ms 回一行）

`H`高度 `T`目标 `E`误差 `P`PWM `M`模式 `D`滤波球速 `R`转速RPM `RAW`原始测距cm
`A`距上次有效样本ms（**>150 表示反馈停滞**，看门狗会兜底保持 u_hover）
`TID/TIN/TST` 激光自检（0xEE/0/0 = 健康）

## 整定顺序（先验反馈，别在死数据上调 PID）

1. 跑 `tune_step`，**先看 `FEEDBACK:` 行**。停滞就先修光路，别碰 PID。
2. `feedback OK` 后：`kp0 ki0` 只调 `kd` 压住发散 → 加 `kp` 拉回目标 → 弱 `ki` 消静差。
