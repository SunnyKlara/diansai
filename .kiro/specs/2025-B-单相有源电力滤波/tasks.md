# Tasks — 2025 B 单相有源电力滤波

- [x] 1. spec 三件套
- [x] 2. 写《00_深度审题与方案论证.md》五层
- [x] 3. 新建 `config.h`
- [x] 4. 拆 `apf_control.c` → `Algorithm/apf_compensator.{c,h}`（基波提取 + 电流跟踪 PI）
- [x] 5. 拆 `harmonic_analysis.c` → `Algorithm/harmonic_fft.{c,h}`
- [x] 6. 新建 `Drivers/{adc_us_il, pwm_apf, lcd_tft, button}.{c,h}` 占位
- [x] 7. 写 `Core/main.c`（5 状态机 + APF 切换）
- [x] 8. 写 `01_代码/README.md`
- [x] 9. 写《02_硬件/电路设计说明.md》（含非线性负载 / APF H桥 / 采样调理）
- [x] 10. 写《03_报告/设计报告.md》八段式
- [x] 11. 写《04_调试记录/独到经验总结.md》
- [x] 12. getDiagnostics 验证
- [x] 13. 进度看板更新
- [x] 14. commit + push + 合 main
