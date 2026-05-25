# Tasks — 2025 G 电路模型探究装置（补缺）

> 当前状态：已有约 80% 完成度，本次目标 → 95%+

## 任务列表

- [x] 1. 创建 spec 三件套（requirements / design / tasks）
- [x] 2. 删除 `01_代码/system_identifier.c`（与 freq_response + signal_processor 重复）
- [x] 3. 新建 `01_代码/oled.h`（接口）
- [x] 4. 新建 `01_代码/oled.c`（功能性占位实现）
- [x] 5. 补充《04_调试记录/调试检查清单.md》— CMSIS-DSP 增益校准章节
- [x] 6. 补充《04_调试记录/调试检查清单.md》— PA4 引脚切换风险
- [x] 7. 补充《04_调试记录/独到经验总结.md》— 链路验证标准流程
- [x] 8. 用 getDiagnostics 验证所有 C 代码无错误
- [x] 9. commit 到 `feature/2025-G-电路模型探究装置` 分支
- [x] 10. push 远程 + 合 main

## 验收

- 整个 `01_代码/` 编译时所有外部符号都可解析（无 undefined reference）
- getDiagnostics 0 错误 0 警告
- 所有产出符合 `.kiro/steering/电赛备赛仓库规范.md`
