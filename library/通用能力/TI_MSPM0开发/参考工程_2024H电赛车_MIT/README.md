# 2024hVibe

基于嘉立创天猛星 MSPM0G3507 的 2024 年全国大学生电子设计竞赛 H 题小车练习工程。

项目从 CCS/SysConfig 空工程开始，在 Agent 辅助下完成了 TB6612 双电机驱动、软件正交编码器计数、轮速 PI、八路灰度连续循迹、ICM-45686 相对航向、串口调试和 `A-C-B-D-A` 路径状态机。当前实车版本能够稳定完成一圈测试，板载 `PB21` 用于启动，再次按下可急停。

## MSPM0 Agent Skill

本项目也是 [mc3545dada/mspm0-skill](https://github.com/mc3545dada/mspm0-skill) 的一次完整实车实践。该 Skill 为 Codex、Claude Code、Cursor 等 Agent 提供 MSPM0 + CCS/Keil/CMake、SysConfig、DriverLib、OpenOCD 和电赛项目的开发规则与工具链指引。

欢迎使用这个 Skill 开发自己的 MSPM0 项目，并把实际使用中遇到的问题、板卡适配经验和工作流改进反馈到原仓库：

- 觉得有帮助可以给 [mspm0-skill](https://github.com/mc3545dada/mspm0-skill) 一个 Star。
- 遇到问题或发现文档缺失，可以提交 Issue。
- 新增板卡、IDE、烧录器、SysConfig 示例或调试经验，欢迎提交 PR。
- 本仓库中的开发对话记录可作为 Agent 使用该 Skill 完成真实电赛小车的参考案例。

## 当前硬件

- 嘉立创天猛星 MSPM0G3507
- TB6612 双路直流电机驱动模块
- 两个带 AB 相编码器的直流减速电机
- 感为无 MCU 八路灰度传感器
- ICM-45686 六轴姿态传感器模块
- DAPLink/CMSIS-DAP 无线烧录器和无线串口
- 12 V 车载电源
- 预留 ST7735S 1.8 寸 TFT、TF/W25Q128 日志存储、四按键和蜂鸣器

完整引脚与画板注意事项见 [硬件移交引脚表](docs/hardware_handoff_pinout.md)。

## 软件结构

```text
APP/                 上层控制、航向、循迹和路径状态机
BSP/                 电机、编码器、灰度、UART、IMU 和时间基准
docs/                硬件接口、串口协议和调试记录
tools/               命令行构建与 OpenOCD 烧录脚本
empty.c              主入口
empty.syscfg         SysConfig 唯一配置源
```

不要手动修改 `Debug/ti_msp_dl_config.c/.h` 等生成文件。

## 主要控制周期

- 基础调度 Tick：1 ms
- ICM-45686 采样和航向积分：5 ms
- 编码器测速、轮速 PI、航向和循迹控制：10 ms
- 电机 PWM：10 kHz
- 调试串口：UART0，115200 8N1

## 构建与烧录

本机验证环境：

- TI MSPM0 SDK `2.10.00.04`
- SysConfig `1.26.2`
- TI Arm Clang `4.0.3.LTS`
- OpenOCD MSPM0 支持版本
- CMSIS-DAP/DAPLink，SWD 500 kHz

```powershell
.\tools\build.ps1
.\tools\flash.ps1
```

烧录脚本会写入、校验并执行 reset run。不同电脑需要按实际安装位置调整工具路径。

## 调试接口

串口支持 `status`、`gray`、`line`、`enc`、`imu`、`yaw zero`、`route3 full`、`speed`、`followfine`、`pi`、`lpd` 等命令，详见 [串口协议](docs/serial_protocol.md)。

路线状态机包含以下保护：

- 无标记直线使用 ICM-45686 航向闭环
- C/B/D 点停车原地转向后再继续
- C/D 连续见线确认
- 半圆最小编码器行程和航向窗口
- B/A 持续丢线确认，避免灰度瞬时抖动跳阶段
- 转向、距离和整圈超时保护

## 题目与开发记录

仓库根目录中的 `topic_*.png` 是题目图片。

`rollout-2026-07-09T15-44-33-019f45d5-e65e-77e1-b398-3b609f623d6d.jsonl` 是本次 Agent 协作开发的原始对话导出，包含大量实车测试、串口日志、调参过程、本机绝对路径以及 Codex 导出的加密内部字段。它仅用于复盘和学习，不是固件构建依赖。

## 注意

- 电机测试前先架空车轮并保留急停方式。
- MCU、驱动、编码器、灰度、IMU 和串口必须共地。
- TB6612 `VM` 使用电机电源，逻辑 `VCC` 使用 3.3 V。
- 灰度模拟输出进入 ADC 前应确认不超过 3.3 V。
- `PA18/PA21/PA23/PA2` 属于天猛星特殊引脚，不作普通外设使用。
- `PA10/PA11` 按用户选择专用于 UART0；`PA19/PA20` 保留 SWD。

## License

本项目代码采用 [MIT License](LICENSE)。TI SDK、SysConfig、芯片资料和外部模块资料仍遵循各自原始许可。
