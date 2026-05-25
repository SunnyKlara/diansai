# Design — 2021 A 信号失真度测量装置

> 详细技术决策见已有的《00_深度审题与方案论证.md》（38/40 评分）。  
> 本文档聚焦"补缺策略"：把扁平代码升级为分层结构 + 补齐缺失模块。

## 现有工程评估

| 模块 | 状态 | 评分 |
|---|---|---|
| 五层深度审题 | ✅ | 38/40（仓库标杆）|
| 做题指南 | ✅ | ★★★★ |
| BOM | ✅ | ★★★★ |
| `config.h` | ✅ | ★★★★★（每个参数都有计算依据）|
| `adc_capture.{c,h}` | ✅ | ★★★★★（MSP432 DMA 坑注释清晰）|
| `freq_detect.{c,h}` | ✅ | ★★★★★（含线性插值改进）|
| `thd_measure.c` | ✅ | ★★★★（缺 .h）|
| `main.c` | ✅ | ★★★★（含完整状态机和多次平均）|
| `README.md` | ✅ | ★★★★ |
| 调试清单 | ✅ | ★★★★ |
| 经验总结 | ✅ | ★★★★ |

## 需要补缺的 3 件事

### 1. 代码分层重构

按 steering 规范，把扁平的 `01_代码/` 升级为：

```
01_代码/
├── README.md
├── config.h
├── Core/
│   └── main.c
├── Drivers/                    # 板级抽象（MSP432 DriverLib）
│   ├── adc_capture.{c,h}       # 已有，移到这里
│   ├── oled.{c,h}              # 新建（main.c 引用了 OLED_Init 等）
│   ├── bt_hc05.{c,h}           # 新建（main.c 引用了 BT_Init 等）
│   ├── button.{c,h}            # 新建
│   └── led_buzzer.{c,h}        # 新建
└── Algorithm/                  # 与硬件解耦
    ├── freq_detect.{c,h}       # 已有，移到这里
    └── thd_measure.{c,h}       # 已有 .c，新建 .h
```

### 2. 补齐 thd_measure.h

main.c 中 `#include "thd_measure.h"` 但工程目录无此文件。  
THDResult_t / measure_thd / thd_init / init_flat_top_window 都需要在 .h 中声明。

### 3. 补齐报告 + 测试数据模板

- `03_报告/设计报告.md` 八段式
- `03_报告/测试数据模板.md`

## 技术方案保留不变

已有方案高度成熟，不重构：
- 一键测量状态机（COARSE → FREQ → RANGE → SYNC → CAPTURE → FFT → THD → DISPLAY → BT → DONE）
- 同步采样 + 平顶窗双保险
- 5 次平均降噪
- 自动量程切换

## 风险

仅一处较小风险：**adc_capture.c 重定位时，文件内对 `config.h` 的相对路径可能需要调整**。
解决：用 `smartRelocate` 工具做移动（自动处理路径），或重写头文件包含为 `"../config.h"`。
