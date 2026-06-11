#ifndef __GLOBAL_H
#define __GLOBAL_H

#include "stm32h7xx_hal.h"

// 系统状态枚举（只在这里定义一次）
typedef enum {
    SYSTEM_INIT,      // 系统初始化
    SYSTEM_READY,     // 系统就绪
    SYSTEM_RUNNING,   // 正常运行
    SYSTEM_ERROR      // 系统错误
} SystemState;

// UI 界面状态机（OLED + 4按键菜单）
typedef enum {
    UI_HOME = 0,      // 主页/模式选择
    UI_CONTROL,       // 定高控制（比赛主界面）
    UI_CALIB,         // 手动PWM标定（采数据）
    UI_CURVE          // 高度曲线（进阶）
} UiState;

// 全局变量声明（extern表示在其他文件中定义）
extern SystemState system_state;
extern float current_height;       // 当前高度(cm)
extern float target_height;        // 目标高度(cm)
extern uint16_t pwm_output;        // PWM输出值(0-1000)
extern uint8_t control_enabled;    // 控制使能标志
extern uint8_t boost_mode;          // 1=起飞模式, 0=PID精调模式

// UI / 标定相关
extern UiState ui_state;            // 当前界面
extern uint8_t key_down;            // 按键按下沿（user.c 中产生，1~4）
extern volatile uint8_t  manual_mode; // 1=手动标定直给PWM
extern volatile uint16_t manual_pwm;  // 手动PWM值
extern volatile uint8_t  height_updated;     // 新高度样本标志(事件驱动闭环)
extern volatile uint32_t height_update_tick; // 新样本时间戳
extern volatile uint32_t g_height_sample_count; // 有效高度样本累计数(实测帧率用)

// PID参数
extern float Kp;
extern float Ki;
extern float Kd;
extern float u_hover;
extern uint32_t pid_last_time;
extern float pid_filtered_deriv;
extern float pid_integral;

// 预设高度列表
extern const float preset_heights[];
extern uint8_t preset_idx;

// 函数声明
void PID_Reset(void);
float HeightFilter(float new_height);
float PID_Control(float current, float target);
void OLED_Update(void);
void Control_Update(void);
void Process_UART_Command(void);
void UI_OnKey(uint8_t key);

#endif // __GLOBAL_H
