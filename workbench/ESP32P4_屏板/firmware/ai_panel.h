// -*- coding: utf-8 -*-
// 自绘验证面板：替代 lv_demo_widgets，用来真机验证「显示可控 + 触摸坐标映射」。
// 由 lvgl_demo.c 在 lvgl_port 锁内调用（LVGL API 必须持锁）。
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 在当前活动屏幕上创建验证面板：
//   - 板子/屏幕信息 + 固件编译时间（证明板上跑的是新固件）
//   - 运行秒数与心跳进度条（证明 LVGL 任务活着）
//   - 触摸坐标/按压计数 + 跟手圆点（一眼看出有无偏移或镜像）
//   - 背光归属判决测试（自解释：关背光前先在屏上留一行字，灭后那行字还看得见
//     就说明 GPIO30 管不着背光）。测试由本模块的 lv_timer 驱动，因此屏上提示
//     与 GPIO 动作严格同步 —— 这是它必须放在这里、而不是留在 main.c 的原因。
void ai_panel_create(void);

#ifdef __cplusplus
}
#endif
