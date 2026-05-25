# Tasks — 2025 A 能量回馈的变流器负载试验装置

## 任务列表

### 阶段 0：审题
- [x] 1. 写《00_深度审题与方案论证.md》（五层结构，含 Pd 损耗预算）

### 阶段 1：硬件方案
- [x] 2. 已有 BOM（保留，注释升级建议 IRFB7434）
- [x] 3. 已有电路设计说明（保留）

### 阶段 2：代码骨架重构
- [x] 4. 新建 `config.h` 集中所有可调参数
- [x] 5. 移动 `svpwm_3phase.{c,h}` → `Algorithm/`
- [x] 6. 新建 `Algorithm/voltage_loop.{c,h}`（电压闭环 PI）
- [x] 7. 新建 `Algorithm/rms_meter.{c,h}`（滑动 RMS 计算）
- [x] 8. 新建 `Algorithm/feedback_control.{c,h}`（能量回馈核心 30 分关键）
- [x] 9. 新建 `Drivers/adc_3phase.{c,h}`（接通 ADC 反馈链路）
- [x] 10. 新建 `Drivers/pwm_3phase.{c,h}`（PWM 启停封装）
- [x] 11. 新建驱动占位 `Drivers/{oled, button, led_buzzer}.{c,h}`
- [x] 12. 重写 `Core/main.c`（7 状态机 + 软启动 + 故障）
- [x] 13. 升级 `01_代码/README.md`（分层说明）

### 阶段 3：调试 & 报告
- [x] 14. 升级《04_调试记录/调试检查清单.md》补阶段化条目
- [x] 15. 写《04_调试记录/独到经验总结.md》（含 8 个赛前坑）
- [x] 16. 写《03_报告/设计报告.md》八段式
- [x] 17. 已有《03_报告/测试数据模板.md》（保留）

### 阶段 4：交付
- [x] 18. getDiagnostics 校验所有 C 代码无错误
- [x] 19. commit + push + 合 main
