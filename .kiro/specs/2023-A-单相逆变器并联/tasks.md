# Tasks — 2023 A 单相逆变器并联

- [x] 1. spec 三件套
- [x] 2. 写《00_深度审题与方案论证.md》五层
- [x] 3. 新建 `config.h` 集中所有可调参数
- [x] 4. 把 6 对源文件智能搬移到 Core / Drivers / Algorithm
- [x] 5. 修复所有 include 路径
- [x] 6. 重命名 `报告正文.md` → `设计报告.md`
- [x] 7. 算法层去耦：`spwm` / `control` 不依赖 stm32 HAL
- [x] 8. 新建 `Drivers/pwm_full_bridge.{c,h}` 封装 TIM1 互补 PWM 操作
- [x] 9. 升级 README 为分层说明
- [x] 10. 写经验总结（保留已有的常见故障表，加赛前 8 个坑）
- [x] 11. getDiagnostics 验证 0 错误
- [x] 12. 进度看板更新
- [x] 13. commit + push + 合 main
