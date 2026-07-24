/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "crc.h"
#include "dac.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h" 
#include "user.h" 
#include "gc9a01.h" 
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "hc-sr04.h"
#include "tof.h"
#include "config.h"
#include "cn_font.h"
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

volatile uint8_t uart_rx_byte = 0;
char uart_cmd_buf[96];
volatile uint8_t uart_cmd_idx = 0;
volatile uint8_t uart_cmd_ready = 0;

// 全局变量定义（只在main.c中定义一次）
SystemState system_state = SYSTEM_INIT;
float current_height = 0.0f;       // 当前高度(cm)
float target_height = TARGET_HEIGHT_DEFAULT; // 目标高度(cm)
float sp_ramp = 0.0f;              // 软目标(轨迹斜坡)：从当前高度按限速爬向target,避免大误差冲程
uint16_t pwm_output = 0;           // PWM输出值(0-1000)
uint8_t control_enabled = 0;       // 开机停止态(风扇不转,避免上电即EMI冲掉USB);曲线页按K3启动→预热→自动悬浮

// UI 界面状态（开机直接进高度曲线界面——本题唯一需要的界面）
UiState ui_state = UI_CURVE;
static uint8_t home_sel = 0;        // 主页光标 0=CONTROL 1=CALIB 2=CURVE
static uint8_t calib_step_idx = 1;  // 标定步长挡位 0=细 1=中 2=粗

// Height scope -- LEFT-SCROLLING strip chart. Ring buffer of mapped y pixels;
// newest sample sits at the right edge and the whole trace shifts left each
// frame (continuous forward push, no sweep/wrap). 2*184 = 368 bytes.
static uint8_t  curve_ay[SCOPE_W];  // actual-height y-pixel history
static uint8_t  curve_ty[SCOPE_W];  // target-height y-pixel history
static uint16_t curve_head = 0;     // index for the next sample
static uint8_t  curve_count = 0;    // valid samples (<= SCOPE_W)
static const uint16_t calib_steps[3] = { CALIB_STEP_FINE, CALIB_STEP_MID, CALIB_STEP_COARSE };

// PID参数（数值定义集中在 config.h，这里只做实例化）
float Kp = PID_KP_DEFAULT;
float Ki = PID_KI_DEFAULT;
float Kd = PID_KD_DEFAULT;
float u_hover = PWM_BASE;           // 悬停前馈基准(可串口 uh 在线改)
float pid_error = 0.0f;
float pid_last_error = 0.0f;
float pid_integral = 0.0f;
float pid_filtered_deriv = 0.0f;
float pid_output = 0.0f;
// 运行时可调参数(串口在线调,免烧录)：从config取默认值
float g_ramp_cms    = TARGET_RAMP_CM_S;   // 'r' 目标爬升速率 cm/s
float g_ramp_down_cms = TARGET_RAMP_DOWN_CM_S; // 'rd' 目标下行速率 cm/s(比上行慢,防降目标砸底)
float g_deriv_alpha = PID_DERIV_ALPHA;    // 'f' 球速EMA系数(越大越平滑越滞后)
float g_slew        = (float)PWM_SLEW_PER_TICK; // 'l' PWM每拍限幅
float g_dmax        = D_TERM_CLAMP;        // 'c' D项(Kd*球速)最大PWM贡献钳位,挡假球速踹飞
float g_pwm_min     = PWM_RUN_MIN;         // 'n' 闭环最低速(怠速地板,风扇不停转)
float g_pwm_max     = PWM_RUN_MAX;         // 'x' 闭环最高速(推力天花板,防窜顶)
float g_standby_pwm = STANDBY_PWM;         // 'b' 待机怠速PWM(停止态让风机常转在起飞临界下方,消冷启动死区)
uint8_t g_scope_en = 1;                    // 'e' 闭环时示波器整区刷新使能(0=停刷零阻塞,A/B验证;1=节流刷新)
volatile uint32_t g_serial_ms = SERIAL_PERIOD_MS; // 'w' 串口心跳周期ms(默认80=12.5Hz;'w20'=50Hz快记录,采全分析数据)
volatile uint32_t g_height_sample_count = 0; // 有效高度样本累计数(用于实测帧率F:)

// 串级内环(转速闭环)：默认关闭=单环已验证行为；'y1'打开后内环用tach把风机线性化
volatile uint8_t g_cascade_en = CASCADE_RPM_DEFAULT; // 0=单环 1=串级内环
float g_rpm_ff_a   = RPM_FF_A_DEFAULT;   // PWM->RPM 斜率(前馈)
float g_rpm_ff_b   = RPM_FF_B_DEFAULT;   // PWM->RPM 截距(前馈)
float g_rpm_kp     = RPM_KP_DEFAULT;     // 内环比例
float g_rpm_ki     = RPM_KI_DEFAULT;     // 内环积分
float rpm_integral = 0.0f;               // 内环积分累计
float rpm_setpoint = 0.0f;               // 内环目标转速(遥测)
float g_rpm_alpha = 0.8f;                // 'yf' 内环转速反馈EMA系数(0=不滤,越大越平滑越滞后)
float rpm_meas_filt = 0.0f;              // 转速反馈滤波状态

float last_pwm_output = 0.0f;
float pwm_slew_last = 0.0f;        // 斜率限幅记忆的"上一次实际输出"(PID_Reset 同步到怠速地板)
uint32_t pid_last_time = 0;        // 供 PID_Control 计算 dt（与控制节拍解耦）
float pid_last_current = 0.0f;     // 上一次高度（微分基于高度变化，非误差）

// LADRC 自抗扰(默认关,串口 Z1 开)。对象 y''=b0*u+f;ESO 估 z1≈y z2≈y' z3≈f(总扰动)
volatile uint8_t g_ctrl_mode = CTRL_MODE_DEFAULT; // 0=PID 1=LADRC
float g_b0 = ADRC_B0_DEFAULT;      // 'b0' 输入增益
float g_wc = ADRC_WC_DEFAULT;      // 'wc' 控制器带宽 -> Kp=wc^2 Kd=2wc
float g_wo = ADRC_WO_DEFAULT;      // 'wo' 观测器带宽 -> b1=3wo b2=3wo^2 b3=wo^3
float g_wt = ADRC_WT_DEFAULT;      // 'wt' 跟踪微分器带宽(平滑目标,消起浮冲击)
float g_hover_rpm = HOVER_RPM_DEFAULT; // 'h' 悬停转速(CTRL_MODE=2 外环基准,跨电池稳定)
// α-β 速度观测器(冲±1cm:干净速度替裸差分+重滤波)
float g_deadband = PID_ERROR_DEADBAND;  // 'd' 误差死区
float g_ab_alpha = OBS_ALPHA_DEFAULT;   // 'va' 位置校正增益
float g_ab_beta  = OBS_BETA_DEFAULT;    // 'vb' 速度校正增益
uint8_t g_use_obs = OBS_USE_DEFAULT;    // 'vo' 1=用观测器速度 0=用旧EMA
float obs_x = 0.0f, obs_v = 0.0f;       // 观测器状态(位置/速度)
float adrc_z1 = 0.0f, adrc_z2 = 0.0f, adrc_z3 = 0.0f; // ESO 状态(z3=f_hat,遥测)
float adrc_r1 = 0.0f, adrc_r2 = 0.0f;                 // TD 状态(r1=平滑目标位置,r2=目标速度前馈)

// 两阶段控制
uint8_t boost_mode = 1;            // 1=起飞模式(大风速)，0=PID精调模式
static uint8_t boost_hold_counter = 0;

// 高度滤波
float height_filter_buf[FILTER_SIZE];
uint8_t filter_idx = 0;
uint8_t filter_inited = 0;         // 滤波器是否已初始化

// 预设高度列表
const float preset_heights[] = {10.0f, 15.0f, 20.0f};
uint8_t preset_idx = 1;            // 默认15cm

// 手动标定模式（用于测 u_min / u_hover，串口命令控制，无需重新烧录）
volatile uint8_t  manual_mode = 0; // 1=手动直给PWM，0=自动闭环
volatile uint16_t manual_pwm  = 0; // 手动模式下的PWM值(0-900)

// 风机预热：闭环从"停机"使能时，先让风机空转 FAN_PREHEAT_MS 越过冷启动死区再投入闭环。
static uint32_t control_start_tick = 0; // 本次闭环使能(上升沿)的时刻
static uint8_t  preheating = 0;         // 1=预热中(开环吹怠速,不跑闭环/不积分)
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// 关闭 semihosting 并把 printf 重定向到 UART1（Arm Compiler 6 / armclang 写法）
// AC6 的 stdio.h 已自带 struct __FILE，不能再重定义；用内联汇编声明 __use_no_semihosting。
__asm(".global __use_no_semihosting\n\t");

FILE __stdout;

void _sys_exit(int x)
{
    (void)x;
    while (1) {}
}

void _ttywrch(int ch)
{
    (void)ch;
}

extern UART_HandleTypeDef huart1;

// printf重定向到UART（仅供printf使用，串口调试输出改用UART_SendStr）
int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 10);
    return ch;
}

// 串口直接发送字符串（不经过printf，可靠）
void UART_SendStr(const char *str)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), 100);
}

// 微秒级延时函数（STM32H7 480MHz主频）
//void delay_us(uint32_t us)
//{
//    uint32_t ticks = us * 480;
//    while (ticks--)
//    {
//        __NOP();
//    }
//}
// 滑动平均滤波（首次填满缓冲区）
float HeightFilter(float new_height)
{
    if (!filter_inited) {
        for (uint8_t i = 0; i < FILTER_SIZE; i++) {
            height_filter_buf[i] = new_height;
        }
        filter_inited = 1;
        return new_height;
    }
    
    height_filter_buf[filter_idx++] = new_height;
    if (filter_idx >= FILTER_SIZE) {
        filter_idx = 0;
    }
    
    float sum = 0.0f;
    for (uint8_t i = 0; i < FILTER_SIZE; i++) {
        sum += height_filter_buf[i];
    }
    return sum / FILTER_SIZE;
}
// 位置式PID（基准360 + 修正量，微分基于高度变化避免过零踢）
float PID_Control(float current, float target)
{
    float dt = (uwTick - pid_last_time) / 1000.0f;
    if (dt <= 0.0f || dt > 1.0f) dt = (float)CTRL_PERIOD_MS / 1000.0f;
    pid_last_time = uwTick;

    // 误差 = 目标 - 当前（正=球太低需加力，负=球太高需减力）
    pid_error = target - current;

    // 误差死区
    if (pid_error > -PID_ERROR_DEADBAND && pid_error < PID_ERROR_DEADBAND)
        pid_error = 0.0f;

    // 积分项：检查输出是否饱和
    {
        float output_test = PWM_BASE + Kp * pid_error;
        int saturated = (output_test >= PWM_OUTPUT_MAX || output_test <= PWM_OUTPUT_MIN);
        if (!saturated) {
            pid_integral += pid_error * dt;
            if (pid_integral > PID_INTEGRAL_LIMIT) pid_integral = PID_INTEGRAL_LIMIT;
            if (pid_integral < -PID_INTEGRAL_LIMIT) pid_integral = -PID_INTEGRAL_LIMIT;
        }
    }

    // 微分项：基于高度变化率，不是误差变化率（避免误差过零时的"微分踢"）
    // 高度增加(球上升)→raw_deriv为负→减少PWM(减速)
    // 高度减少(球下落)→raw_deriv为正→增加PWM(加速)
    float raw_deriv = -(current - pid_last_current) / dt;
    pid_filtered_deriv = PID_DERIV_ALPHA * pid_filtered_deriv + (1.0f - PID_DERIV_ALPHA) * raw_deriv;

    // PID输出 = 基准 + 比例修正 + 积分修正 + 微分阻尼
    pid_output = PWM_BASE + Kp * pid_error + Ki * pid_integral + Kd * pid_filtered_deriv;

    // 输出限幅
    if (pid_output > PWM_OUTPUT_MAX) pid_output = PWM_OUTPUT_MAX;
    if (pid_output < PWM_OUTPUT_MIN) pid_output = PWM_OUTPUT_MIN;

    last_pwm_output = pid_output;
    pid_last_error = pid_error;
    pid_last_current = current;
    return pid_output;
}

// PID参数重置
void PID_Reset(void)
{
    pid_error = 0.0f;
    pid_last_error = 0.0f;
    pid_integral = 0.0f;
    pid_filtered_deriv = 0.0f;
    pid_output = 0.0f;
    last_pwm_output = PWM_BASE;
    rpm_integral = 0.0f;                 // 串级内环积分清零
    rpm_meas_filt = (float)Fan_GetRPM(); // 转速反馈滤波以当前转速起步,避免投入瞬间从0爬
    rpm_setpoint = g_hover_rpm;          // 内环ISR在外环首次更新前有合理给定(免投入瞬间掉到地板)
    pwm_slew_last = g_pwm_min;           // 斜率限幅从怠速地板起步,避免投入瞬间跌破地板
    adrc_z1 = current_height; adrc_z2 = 0.0f; adrc_z3 = 0.0f;  // ESO 以当前球位起步,扰动估计清零
    adrc_r1 = current_height; adrc_r2 = 0.0f;                  // TD 从当前球位起步,平滑爬向目标
    obs_x = current_height; obs_v = 0.0f;                      // α-β 观测器以当前球位起步
    pid_last_time = uwTick;
    pid_last_current = current_height;  // 用当前高度，避免PID切入时D项突变
    sp_ramp = current_height;           // 软目标从当前球位起步,随后按限速爬向target
    // 不重置filter_inited，让高度滤波器保持连续性
    Ultrasonic_ResetState();  // 重置传感器验证状态（模式切换后重新开始验证）
}
// height(cm) -> scope plot Y pixel (top=full scale, bottom=0). Clamped to the
// waveform band (below the readout strip).
static uint8_t Curve_MapY(float h)
{
    if (h < 0.0f) h = 0.0f;
    if (h > SCOPE_HMAX_CM) h = SCOPE_HMAX_CM;
    return (uint8_t)(SCOPE_Y1 - (h / SCOPE_HMAX_CM) * (SCOPE_Y1 - SCOPE_PLOT_Y0));
}

// (old OLED_CN removed; use GC9A01_DrawCNStr / CN_Center below)

// UI 中文标签（索引来自 cn_font.h）
static const uint8_t LBL_CONTROL[4] = { CN_DING, CN_GAO, CN_KONG, CN_ZHI };  // 定高控制
static const uint8_t LBL_CALIB[4]   = { CN_SHOU, CN_DONG, CN_BIAO, CN_DING };// 手动标定
static const uint8_t LBL_CURVE[4]   = { CN_GAO, CN_DU, CN_QU, CN_XIAN };     // 高度曲线
static const uint8_t LBL_TARGET[2]  = { CN_MU, CN_BIAO };                    // 目标
static const uint8_t LBL_CURNOW[2]  = { CN_DANG, CN_QIAN };                  // 当前
static const uint8_t LBL_QIFEI[2]   = { CN_QI, CN_FEI };                     // 起飞
static const uint8_t LBL_DINGGAO[2] = { CN_DING, CN_GAO };                   // 定高
static const uint8_t LBL_TINGZHI[2] = { CN_TING, CN_ZHI2 };                  // 停止

// centered Chinese string (16px/char) at cx=LCD_CX
static void CN_Center(int16_t y, const uint8_t *idx, uint8_t n, uint16_t fg)
{
    GC9A01_DrawCNStr(LCD_CX - n * 8, y, idx, n, fg, LCD_BLACK, 1);
}

static uint8_t curve_chrome = 0;   // CURVE static frame drawn flag

// redraw static overlays (left cm labels + bottom color-key legend) whose x-range
// intersects [bx0,bx1]; drawn on top of the freshly-cleared plot each frame.
static void Scope_Overlays(int16_t bx0, int16_t bx1)
{
    if (!(bx1 < SCOPE_X0 + 2 || bx0 > SCOPE_X0 + 13)) {           // y-axis cm labels (left)
        GC9A01_DrawString(SCOPE_X0 + 2, (int16_t)Curve_MapY(30.0f) - 4, "30", 8, LCD_LGRAY, LCD_BLACK, 1);
        GC9A01_DrawString(SCOPE_X0 + 2, (int16_t)Curve_MapY(20.0f) - 4, "20", 8, LCD_LGRAY, LCD_BLACK, 1);
        GC9A01_DrawString(SCOPE_X0 + 2, (int16_t)Curve_MapY(10.0f) - 4, "10", 8, LCD_LGRAY, LCD_BLACK, 1);
    }
    if (!(bx1 < SCOPE_X0 + 3 || bx0 > SCOPE_X0 + 35))            // legend: 当前 (green)
        GC9A01_DrawCNStr(SCOPE_X0 + 3, SCOPE_Y1 - 17, LBL_CURNOW, 2, LCD_GREEN, LCD_BLACK, 1);
    if (!(bx1 < SCOPE_X1 - 34 || bx0 > SCOPE_X1 - 2))           // legend: 目标 (red)
        GC9A01_DrawCNStr(SCOPE_X1 - 34, SCOPE_Y1 - 17, LBL_TARGET, 2, LCD_RED, LCD_BLACK, 1);
}

// CURVE: draw the static frame once (title + border). Plot interior (grid +
// scrolling traces + labels/legend) is redrawn every frame by Curve_Render.
void Control_Update(void);   // 前置声明:示波器整区刷新分条时,条间插空跑外环控制(防flush阻塞控制环)
static void Curve_DrawStatic(void)
{
    GC9A01_Clear(LCD_BLACK);
    CN_Center(4, LBL_CURVE, 4, LCD_WHITE);                              // title in top cap
    GC9A01_DrawRect(SCOPE_X0 - 1, SCOPE_Y0 - 1, SCOPE_W + 2, SCOPE_H + 2, LCD_GRAY);
    GC9A01_Flush();
}

// CURVE: left-scrolling strip chart. Newest sample enters at the right edge,
// the whole trace shifts left. Only horizontal cm grid lines (no vertical lines).
static void Curve_Render(void)
{
    char buf[20];
    if (!curve_chrome) { Curve_DrawStatic(); curve_chrome = 1; }

    // push newest sample
    curve_ay[curve_head] = Curve_MapY(current_height);
    curve_ty[curve_head] = Curve_MapY(target_height);
    curve_head = (curve_head + 1) % SCOPE_W;
    if (curve_count < SCOPE_W) curve_count++;

    // ---- top readout strip (redraw + flush only on value change) ----
    static int16_t last_h10 = -30000, last_t10 = -30000;
    int16_t h10 = (int16_t)(current_height * 10.0f);
    int16_t t10 = (int16_t)(target_height  * 10.0f);
    if (h10 != last_h10 || t10 != last_t10) {
        last_h10 = h10; last_t10 = t10;
        float err = current_height - target_height;
        GC9A01_FillRect(SCOPE_X0, SCOPE_Y0, SCOPE_W, SCOPE_RDH, LCD_BLACK);
        sprintf(buf, "%.1f", current_height);
        GC9A01_DrawString(SCOPE_X0 + 2, SCOPE_Y0 + 1, buf, 16, LCD_GREEN, LCD_BLACK, 0);
        sprintf(buf, "%.1f", target_height);
        GC9A01_DrawString(LCD_CX - 16, SCOPE_Y0 + 1, buf, 16, LCD_RED, LCD_BLACK, 0);
        sprintf(buf, "e%+.1f", err);
        uint16_t ec = (err > -SCOPE_SPEC_CM && err < SCOPE_SPEC_CM) ? LCD_GREEN : LCD_YELLOW;
        GC9A01_DrawString(SCOPE_X1 - 52, SCOPE_Y0 + 1, buf, 16, ec, LCD_BLACK, 0);
        GC9A01_FlushRegion(SCOPE_X0, SCOPE_Y0, SCOPE_X1, SCOPE_Y0 + SCOPE_RDH - 1);
    }

    // ---- plot: clear, horizontal cm grid (no vertical lines), scrolling traces ----
    int16_t plot_h = SCOPE_Y1 - SCOPE_PLOT_Y0 + 1;
    GC9A01_FillRect(SCOPE_X0, SCOPE_PLOT_Y0, SCOPE_W, plot_h, LCD_BLACK);
    GC9A01_DrawHLine(SCOPE_X0, (int16_t)Curve_MapY(10.0f), SCOPE_W, LCD_DGRAY);
    GC9A01_DrawHLine(SCOPE_X0, (int16_t)Curve_MapY(20.0f), SCOPE_W, LCD_DGRAY);
    GC9A01_DrawHLine(SCOPE_X0, (int16_t)Curve_MapY(30.0f), SCOPE_W, LCD_DGRAY);

    // traces: newest at right edge, older to the left (diagonal connect)
    for (uint16_t k = 1; k < curve_count; k++) {
        uint16_t i_n = (curve_head - k + SCOPE_W) % SCOPE_W;          // newer point
        uint16_t i_o = (curve_head - k - 1 + SCOPE_W) % SCOPE_W;      // older point
        int16_t xn = SCOPE_X1 - (int16_t)(k - 1);
        int16_t xo = SCOPE_X1 - (int16_t)k;
        GC9A01_DrawLine(xo, curve_ty[i_o], xn, curve_ty[i_n], LCD_RED);
        GC9A01_DrawLine(xo, curve_ay[i_o], xn, curve_ay[i_n], LCD_GREEN);
    }

    Scope_Overlays(SCOPE_X0, SCOPE_X1);                              // cm labels + legend
    // 分条刷新示波器区:每推一条横带(~16行)就插空"读一次ToF + 跑一拍控制",使整区推屏(轮询SPI
    // ~85ms)期间反馈仍在刷新(A保持低),不再"flush致反馈中断→观测器速度尖峰→触发超±2cm下坠"。
    // 注:A是"距上次读到ToF新样本"的时长,故必须在条间调 Tof_Measure(只插Control_Update用的是旧高度,无效)。
    for (int16_t yb = SCOPE_PLOT_Y0; yb <= SCOPE_Y1; yb += 16) {
        int16_t yb2 = yb + 15; if (yb2 > SCOPE_Y1) yb2 = SCOPE_Y1;
        GC9A01_FlushRegion(SCOPE_X0, yb, SCOPE_X1, yb2);
#if HEIGHT_SENSOR_TOF
        Tof_Measure();             // 条间继续读激光(非阻塞,有新样本才更新)
#else
        Ultrasonic_Measure();
#endif
        if (control_enabled && !manual_mode && !preheating && height_updated) {
            height_updated = 0;
            Control_Update();      // 有新样本则立刻跑一拍外环(与主循环同线程,安全)
        }
    }

    // ---- PWM status strip below the box (redraw + flush only on change) ----
    static uint16_t last_pwm_disp = 0xFFFF;
    if (pwm_output != last_pwm_disp) {
        last_pwm_disp = pwm_output;
        GC9A01_FillRect(SCOPE_X0, SCOPE_PWM_Y0, SCOPE_W, SCOPE_PWM_H, LCD_BLACK);
        sprintf(buf, "PWM%5u", (unsigned)pwm_output);                // numeric duty
        GC9A01_DrawString(LCD_CX - 32, SCOPE_PWM_Y0, buf, 16, LCD_CYAN, LCD_BLACK, 0);
        int16_t bx = LCD_CX - 55, by = SCOPE_PWM_Y0 + 18, bw = 110, bh = 8;  // duty bar
        GC9A01_DrawRect(bx, by, bw, bh, LCD_GRAY);
        int16_t fillw = (int16_t)((uint32_t)(bw - 2) * pwm_output / PWM_FULL_SCALE);
        if (fillw > 0) GC9A01_FillRect(bx + 1, by + 1, fillw, bh - 2, LCD_CYAN);
        GC9A01_FlushRegion(SCOPE_X0, SCOPE_PWM_Y0, SCOPE_X1, SCOPE_PWM_Y0 + SCOPE_PWM_H - 1);
    }
}

// GC9A01 round-screen UI: four pages. Dirty-tracked full redraw; CURVE partial.
void OLED_Update(void)
{
    static UiState last_state = 0xFF;
    static uint8_t  last_sel = 0xFF, last_step = 0xFF, last_boost = 0xFF;
    static int16_t  last_H10 = -30000, last_T10 = -30000;
    static uint16_t last_pwm = 0xFFFF;
    char buf[20];

    if (ui_state == UI_CURVE) { Curve_Render(); last_state = ui_state; return; }

    int16_t H10 = (int16_t)(current_height * 10.0f);
    int16_t T10 = (int16_t)(target_height  * 10.0f);

    uint8_t changed = (ui_state != last_state);
    switch (ui_state) {
    case UI_HOME:
        if (home_sel != last_sel) changed = 1;
        break;
    case UI_CONTROL:
        if (H10 != last_H10 || T10 != last_T10 || pwm_output != last_pwm || boost_mode != last_boost) changed = 1;
        break;
    case UI_CALIB:
        if (H10 != last_H10 || manual_pwm != last_pwm || calib_step_idx != last_step) changed = 1;
        break;
    default:
        break;
    }
    if (!changed) return;

    GC9A01_Clear(LCD_BLACK);

    switch (ui_state) {
    case UI_HOME: {
        const uint8_t *items[3] = { LBL_CONTROL, LBL_CALIB, LBL_CURVE };
        for (uint8_t i = 0; i < 3; i++) {
            int16_t y = 66 + i * 40;
            uint16_t fg = LCD_WHITE;
            if (i == home_sel) {
                GC9A01_FillRect(LCD_CX - 44, y - 4, 88, 24, LCD_CYAN);   // selected highlight
                fg = LCD_BLACK;
            }
            CN_Center(y, items[i], 4, fg);
        }
        break;
    }

    case UI_CONTROL: {
        CN_Center(14, LBL_CONTROL, 4, LCD_WHITE);                        // title
        const uint8_t *st = (!control_enabled) ? LBL_TINGZHI : (boost_mode ? LBL_QIFEI : LBL_DINGGAO);
        uint16_t stc = (!control_enabled) ? LCD_GRAY : (boost_mode ? LCD_ORANGE : LCD_GREEN);
        GC9A01_DrawCNStr(LCD_CX - 16, 40, st, 2, stc, LCD_BLACK, 1);     // status
        sprintf(buf, "%4.1f", current_height);                          // big current height
        GC9A01_DrawString(LCD_CX - 48, 92, buf, 24, LCD_WHITE, LCD_BLACK, 1);
        GC9A01_DrawString(LCD_CX + 52, 102, "cm", 16, LCD_LGRAY, LCD_BLACK, 1);
        GC9A01_DrawCNStr(LCD_CX - 62, 152, LBL_TARGET, 2, LCD_RED, LCD_BLACK, 1);
        sprintf(buf, "%4.1f", target_height);
        GC9A01_DrawString(LCD_CX - 28, 152, buf, 16, LCD_RED, LCD_BLACK, 1);
        sprintf(buf, "P%4d", pwm_output);
        GC9A01_DrawString(LCD_CX + 18, 152, buf, 16, LCD_CYAN, LCD_BLACK, 1);
        break;
    }

    case UI_CALIB: {
        CN_Center(14, LBL_CALIB, 4, LCD_WHITE);                          // title
        sprintf(buf, "x%d", calib_steps[calib_step_idx]);
        GC9A01_DrawString(LCD_CX + 28, 42, buf, 12, LCD_YELLOW, LCD_BLACK, 1);
        sprintf(buf, "%4.1f", current_height);                          // big current height
        GC9A01_DrawString(LCD_CX - 48, 92, buf, 24, LCD_WHITE, LCD_BLACK, 1);
        GC9A01_DrawString(LCD_CX + 52, 102, "cm", 16, LCD_LGRAY, LCD_BLACK, 1);
        sprintf(buf, "P%4d", manual_pwm);
        GC9A01_DrawString(LCD_CX - 58, 152, buf, 16, LCD_CYAN, LCD_BLACK, 1);
        sprintf(buf, "%4.1f%%", manual_pwm * 100.0f / PWM_FULL_SCALE);
        GC9A01_DrawString(LCD_CX + 8, 152, buf, 16, LCD_LGRAY, LCD_BLACK, 1);
        break;
    }

    default:
        break;
    }
    GC9A01_Flush();

    last_state = ui_state;
    last_sel   = home_sel;
    last_step  = calib_step_idx;
    last_boost = boost_mode;
    last_H10   = H10;
    last_T10   = T10;
    last_pwm   = (ui_state == UI_CALIB) ? manual_pwm : pwm_output;
}

// 按界面分发按键动作（key: 1=K1上/加 2=K2进入/切换 3=K3返回 4=K4下/减）
void UI_OnKey(uint8_t key)
{
    switch (ui_state) {
    case UI_HOME:
        if (key == 1) { if (home_sel > 0) home_sel--; }
        else if (key == 4) { if (home_sel < 2) home_sel++; }
        else if (key == 2) {
            if (home_sel == 0) {                 // 定高控制
                ui_state = UI_CONTROL;
                manual_mode = 0; control_enabled = 1; boost_mode = 1;
                PID_Reset();
            } else if (home_sel == 1) {          // 标定采数据
                ui_state = UI_CALIB;
                manual_mode = 1; manual_pwm = 0; control_enabled = 1;
            } else {                             // height curve (closed-loop + plot)
                ui_state = UI_CURVE;
                manual_mode = 0; control_enabled = 1; boost_mode = 1;
                curve_head = 0; curve_count = 0; curve_chrome = 0;
                PID_Reset();
            }
        }
        break;

    case UI_CONTROL:
    case UI_CURVE:
        if (key == 1) {
            target_height += TARGET_STEP_SMALL;
            if (target_height > TARGET_HEIGHT_MAX) target_height = TARGET_HEIGHT_MAX;
        } else if (key == 4) {
            target_height -= TARGET_STEP_SMALL;
            if (target_height < TARGET_HEIGHT_MIN) target_height = TARGET_HEIGHT_MIN;
        } else if (key == 2) {
            preset_idx = (preset_idx + 1) % 3;           // 循环 10/15/20
            target_height = preset_heights[preset_idx];
            boost_mode = 1;
            PID_Reset();
        } else if (key == 3) {                           // K3: 启停切换(始终留在曲线界面)
            if (control_enabled) {
                control_enabled = 0; manual_mode = 0;
                pwm_output = 0;
                __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, 0);
            } else {
                control_enabled = 1; manual_mode = 0; boost_mode = 1;
                curve_head = 0; curve_count = 0; curve_chrome = 0;
                PID_Reset();
            }
        }
        break;

    case UI_CALIB: {
        uint16_t step = calib_steps[calib_step_idx];
        if (key == 1) {
            manual_pwm = (manual_pwm + step > (uint16_t)PWM_OUTPUT_MAX) ? (uint16_t)PWM_OUTPUT_MAX : manual_pwm + step;
        } else if (key == 4) {
            manual_pwm = (manual_pwm < step) ? 0 : manual_pwm - step;
        } else if (key == 2) {
            calib_step_idx = (calib_step_idx + 1) % 3;   // 切步长 20/100/500
        } else if (key == 3) {                           // 返回主页，停机
            ui_state = UI_HOME;
            manual_mode = 0; control_enabled = 0;
            pwm_output = 0;
            __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, 0);
        }
        break;
    }
    }
}
/* USER CODE END 0 */

// 控制更新（固定节拍）：前馈 + PD + 弱积分。
// ===== 串级内环:转速(RPM)PI,在 TIM4 500Hz ISR 中运行(与外环解耦) =====
// 外环(Control_Update,~42Hz)只设 rpm_setpoint;本函数高速闭合转速环->PWM。
// 真正的带宽分离(500Hz>>42Hz)使串级成立;tach 锁转速 -> 对电池放电掉压免疫。
// 仅在 CTRL_MODE==2 且闭环投入(非停机/手动/预热)时由回调调用并写 PWM。
static void Inner_RPM_Update(void)
{
    const float dt = 1.0f / (float)RPM_INNER_HZ;
    float rpm_sp   = rpm_setpoint;                  // 外环给定(32位float读写原子)
    float rpm_raw  = (float)Fan_GetRPM();
    // 转速反馈低通(EMA):tach 逐脉冲量化阶梯+抖动,直接喂内环会让 PWM 抖。先滤再用。
    rpm_meas_filt  = g_rpm_alpha * rpm_meas_filt + (1.0f - g_rpm_alpha) * rpm_raw;
    float rpm_meas = rpm_meas_filt;
    float rpm_err  = rpm_sp - rpm_meas;
    // 前馈:rpm->pwm 逆映射(电压稳态工作点);PI 在其上修正(吸收电压漂/非线性/下垂)
    float ff = (g_rpm_ff_a != 0.0f) ? ((rpm_sp - g_rpm_ff_b) / g_rpm_ff_a) : u_hover;
    float u  = ff + g_rpm_kp * rpm_err + g_rpm_ki * rpm_integral;
    // 条件抗饱和:仅当未顶限时累加内环积分
    float uc = u;
    if (uc > g_pwm_max) uc = g_pwm_max;
    if (uc < g_pwm_min) uc = g_pwm_min;
    if (uc < g_pwm_max && uc > g_pwm_min) {
        rpm_integral += rpm_err * dt;
        if (rpm_integral >  RPM_INTEGRAL_LIMIT) rpm_integral =  RPM_INTEGRAL_LIMIT;
        if (rpm_integral < -RPM_INTEGRAL_LIMIT) rpm_integral = -RPM_INTEGRAL_LIMIT;
    }
    u = uc;
    // 斜率限幅(相对上次实际输出)+ 最终钳位(地板永不被限速拉穿)
    if (u - pwm_slew_last >  g_slew)      u = pwm_slew_last + g_slew;
    else if (pwm_slew_last - u > g_slew)  u = pwm_slew_last - g_slew;
    if (u > g_pwm_max) u = g_pwm_max;
    if (u < g_pwm_min) u = g_pwm_min;
    pwm_slew_last = u;
    pwm_output = (uint16_t)u;
    __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, pwm_output);
}

// TIM4 周期中断(500Hz):驱动串级内环。其它模式/状态下不动 PWM(由主循环/外环负责)。
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM4) return;
    if (g_ctrl_mode == 2 && control_enabled && !manual_mode && !preheating) {
        Inner_RPM_Update();
    }
}

// 对象=不稳定直筒(双积分)：固定PWM托不住球，必须实时反馈。
// D项(球速阻尼)是稳定关键；u_hover=悬停前馈(球速、误差均0时的PWM)。
void Control_Update(void)
{
    // 真实 dt（事件驱动：两次新样本的实际间隔），限幅防异常
    static uint32_t t_last = 0;
    uint32_t now = uwTick;
    float dt = (t_last == 0) ? ((float)CTRL_PERIOD_MS / 1000.0f) : ((float)(now - t_last) / 1000.0f);
    t_last = now;
    if (dt < 0.02f) dt = 0.02f;
    if (dt > 0.25f) dt = 0.25f;

    // ===== LADRC 自抗扰分支(g_ctrl_mode==1)。TD平滑目标 + ESO估扰 + PD,扰动直接抵消 =====
    if (g_ctrl_mode == 1) {
        // 跟踪微分器(TD):把阶跃目标 target 平滑成 r1(位置)+ r2(速度前馈),消起浮冲击
        // 2阶临界阻尼跟踪,带宽 g_wt:r1''= -2wt*r2 - wt^2*(r1-target)
        adrc_r1 += dt * adrc_r2;
        adrc_r2 += dt * (-2.0f * g_wt * adrc_r2 - g_wt * g_wt * (adrc_r1 - target_height));
        // ESO 增益(三极点配到 -wo)
        float b1 = 3.0f * g_wo;
        float b2 = 3.0f * g_wo * g_wo;
        float b3 = g_wo * g_wo * g_wo;
        float u_prev = (pwm_slew_last - u_hover);   // 上拍等效控制量(相对悬停偏置)
        // ESO 更新(离散欧拉,事件驱动 dt)
        float eo = adrc_z1 - current_height;
        adrc_z1 += dt * (adrc_z2 - b1 * eo);
        adrc_z2 += dt * (adrc_z3 + g_b0 * u_prev - b2 * eo);
        float z3_next = adrc_z3 + dt * (-b3 * eo);
        // 控制律:PD 作用在"跟踪误差"上(含速度前馈 r2),再减掉估计的总扰动 z3
        float Kp_a = g_wc * g_wc;
        float Kd_a = 2.0f * g_wc;
        float u0 = Kp_a * (adrc_r1 - adrc_z1) + Kd_a * (adrc_r2 - adrc_z2);
        float ua = u_hover + (u0 - z3_next) / g_b0;
        // 抗饱和:输出顶限时冻结 z3 积分
        if (ua < g_pwm_max && ua > g_pwm_min) {
            adrc_z3 = z3_next;
        }
        // 斜率限幅 + 上下限钳位
        float u = ua;
        if (u - pwm_slew_last >  g_slew)      u = pwm_slew_last + g_slew;
        else if (pwm_slew_last - u > g_slew)  u = pwm_slew_last - g_slew;
        if (u > g_pwm_max) u = g_pwm_max;
        if (u < g_pwm_min) u = g_pwm_min;
        pwm_slew_last = u;
        pwm_output = (uint16_t)u;
        pid_error = target_height - current_height;          // 遥测 E
        pid_filtered_deriv = adrc_z2;                        // 遥测 D = ESO 估的球速
        pid_output = u;
        pid_last_current = current_height;
        boost_mode = (fabsf(pid_error) > 3.0f) ? 1 : 0;
        __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, pwm_output);
        return;
    }

    // ===== CTRL_MODE==2: 转速串级(外环高度PID,慢,~42Hz) =====
    // 关键:外环只产生"目标转速 rpm_setpoint";内环转速PI->PWM 在 TIM4 500Hz ISR
    // (Inner_RPM_Update) 里跑,与本外环解耦,实现真正的带宽分离。本函数不写 PWM。
    // 速度用 α-β 观测器(干净,不被 0.3cm 激光跳变放大),即"串级+观测器"。
    if (g_ctrl_mode == 2) {
        float e = target_height - current_height;
        pid_error = e;
        // 起飞 boost:球在底部时给超过离地阈值的高转速把它吹起,升过 EXIT 再交还悬停控制。
        // 迟滞(ENTER<EXIT)防抖;飞行中坠底会自动重新 boost(自恢复)。
        static uint8_t casc_launching = 0;
        if (current_height < CASCADE_LAUNCH_ENTER)      casc_launching = 1;
        else if (current_height > CASCADE_LAUNCH_EXIT)  casc_launching = 0;
        if (casc_launching && target_height > CASCADE_LAUNCH_EXIT) {
            rpm_setpoint = g_hover_rpm + CASCADE_LAUNCH_BOOST;  // 交给内环ISR冲到离地转速
            pid_integral = 0.0f;                                // 起飞期不积分
            pid_filtered_deriv = 0.0f;
            obs_x = current_height; obs_v = 0.0f;               // 观测器跟随,避免交接时喷假速度
            pid_last_current = current_height;
            pid_output = rpm_setpoint;
            sp_ramp = current_height;        // 软目标跟随球位,boost交还后从此平滑爬向target
            boost_mode = 1;
            return;
        }
        // 轨迹斜坡:软目标 sp_ramp 按限速爬向 target_height。上行用 g_ramp_cms,下行用更慢的
        // g_ramp_down_cms(降目标时球受重力助推易失速冲过头砸底,下行放缓让控制器始终能刹住)。
        {
            float up_step   = g_ramp_cms * dt;
            float down_step = g_ramp_down_cms * dt;
            if (sp_ramp < target_height - up_step)        sp_ramp += up_step;
            else if (sp_ramp > target_height + down_step) sp_ramp -= down_step;
            else                                          sp_ramp = target_height;
        }
        e = sp_ramp - current_height;        // 控制误差改用软目标(平滑轨迹)
        float e_db = e;
        if (e_db > -g_deadband && e_db < g_deadband) e_db = 0.0f;
        // α-β 速度观测器(与 CTRL_MODE==0 同一套,匀速模型+测量残差)
        obs_x += obs_v * dt;
        float obs_r = current_height - obs_x;
        obs_x += g_ab_alpha * obs_r;
        obs_v += (g_ab_beta / dt) * obs_r;
        float v_raw = (current_height - pid_last_current) / dt;
        pid_filtered_deriv = g_deriv_alpha * pid_filtered_deriv + (1.0f - g_deriv_alpha) * v_raw;
        float v = g_use_obs ? obs_v : pid_filtered_deriv;
        pid_filtered_deriv = v;                              // 遥测 D = 观测器球速
        // 外环:目标转速 = 悬停转速 + Kp*e - Kd*球速 + Ki*积分(Kp/Ki/Kd 单位 = RPM-空间)
        float rpm_sp = g_hover_rpm + Kp * e_db - Kd * v + Ki * pid_integral;
        // 限幅:目标转速夹在悬停±600RPM(防外环命令离谱转速)
        if (rpm_sp > g_hover_rpm + 600.0f) rpm_sp = g_hover_rpm + 600.0f;
        if (rpm_sp < g_hover_rpm - 600.0f) rpm_sp = g_hover_rpm - 600.0f;
        // 条件抗饱和:仅当目标转速未顶限时才累加外环积分
        if (rpm_sp < g_hover_rpm + 600.0f && rpm_sp > g_hover_rpm - 600.0f) {
            pid_integral += e_db * dt;
            if (pid_integral >  PID_INTEGRAL_LIMIT) pid_integral =  PID_INTEGRAL_LIMIT;
            if (pid_integral < -PID_INTEGRAL_LIMIT) pid_integral = -PID_INTEGRAL_LIMIT;
        }
        rpm_setpoint = rpm_sp;       // 交给 500Hz 内环 ISR
        pid_output = rpm_sp;         // 遥测
        pid_last_current = current_height;
        boost_mode = (fabsf(pid_error) > 3.0f) ? 1 : 0;
        return;                      // PWM 由 Inner_RPM_Update() 在 TIM4 ISR 写
    }

    // ===== CTRL_MODE==0: 干净 PWM-PID(已验证基线) =====
    // 干净 PID：误差直接用目标高度（已移除轨迹斜坡 ramp）
    float e = target_height - current_height;
    pid_error = e;
    // 误差死区（抑制末端抖动；±PID_ERROR_DEADBAND）
    float e_db = e;
    if (e_db > -g_deadband && e_db < g_deadband) e_db = 0.0f;

    // α-β 速度观测器:用匀速模型+测量残差算干净速度(滞后远小于EMA,不被0.3cm跳变放大)
    obs_x += obs_v * dt;
    float obs_r = current_height - obs_x;
    obs_x += g_ab_alpha * obs_r;
    obs_v += (g_ab_beta / dt) * obs_r;
    // 旧路径:裸微分+EMA(g_use_obs=0 时回退)
    float v_raw = (current_height - pid_last_current) / dt;
    pid_filtered_deriv = g_deriv_alpha * pid_filtered_deriv + (1.0f - g_deriv_alpha) * v_raw;
    float v = g_use_obs ? obs_v : pid_filtered_deriv;   // D项用的球速;同时喂遥测 D 字段
    pid_filtered_deriv = v;

    // 干净控制律：前馈 + 比例 + 微分阻尼（已移除 D 项钳位）
    float u_pd = u_hover + Kp * e_db - Kd * v;

    // 弱积分（条件积分抗饱和：仅当总输出未顶限时才累加，限幅按运行速度上下限）
    float u_try = u_pd + Ki * pid_integral;
    if (u_try < g_pwm_max && u_try > g_pwm_min) {
        pid_integral += e_db * dt;
        if (pid_integral >  PID_INTEGRAL_LIMIT) pid_integral =  PID_INTEGRAL_LIMIT;
        if (pid_integral < -PID_INTEGRAL_LIMIT) pid_integral = -PID_INTEGRAL_LIMIT;
    }

    // 输出限速：闭环运行时夹在 [g_pwm_min, g_pwm_max] 内（怠速地板+推力天花板），
    // 而非物理满量程。怠速地板让风扇常转(免冷启动滞后)，天花板防窜顶。
    float u = u_pd + Ki * pid_integral;
    if (u > g_pwm_max) u = g_pwm_max;
    if (u < g_pwm_min) u = g_pwm_min;
    pid_output = u;                         // 外环输出 = PWM 等效"推力需求"

    // 串级内环(转速闭环)：把外环需求 u 当作"目标转速"经 tach 实测闭环，线性化执行器。
    // 关闭时(默认)直接走 u，等价已验证单环；打开时内环补偿风机非线性/下垂,从根上压浮动。
    if (g_cascade_en) {
        rpm_setpoint = g_rpm_ff_a * u + g_rpm_ff_b;     // 期望转速 = PWM->RPM 前馈映射
        float rpm_meas = (float)Fan_GetRPM();
        float rpm_err  = rpm_setpoint - rpm_meas;
        // 条件积分抗饱和：仅当内环输出未顶限时才累加
        float u_inner_try = u + g_rpm_kp * rpm_err + g_rpm_ki * rpm_integral;
        if (u_inner_try < g_pwm_max && u_inner_try > g_pwm_min) {
            rpm_integral += rpm_err * dt;
            if (rpm_integral >  RPM_INTEGRAL_LIMIT) rpm_integral =  RPM_INTEGRAL_LIMIT;
            if (rpm_integral < -RPM_INTEGRAL_LIMIT) rpm_integral = -RPM_INTEGRAL_LIMIT;
        }
        u += g_rpm_kp * rpm_err + g_rpm_ki * rpm_integral;
        if (u > g_pwm_max) u = g_pwm_max;
        if (u < g_pwm_min) u = g_pwm_min;
    } else {
        rpm_setpoint = 0.0f;
        rpm_integral = 0.0f;
    }

    // PWM 斜率限幅（每拍最大变化 g_slew，串口 'l' 可调）。相对"上一次实际输出"限速。
    // 起步从怠速地板开始(PID_Reset 已同步 pwm_slew_last=g_pwm_min)，避免投入瞬间从 0 爬升而跌破地板。
    if (u - pwm_slew_last >  g_slew)      u = pwm_slew_last + g_slew;
    else if (pwm_slew_last - u > g_slew)  u = pwm_slew_last - g_slew;
    // 斜率限幅后再做最终上下限钳位：保证输出恒在 [pwm_min, pwm_max] 内（地板永不被限速拉穿）
    if (u > g_pwm_max) u = g_pwm_max;
    if (u < g_pwm_min) u = g_pwm_min;
    pwm_slew_last = u;

    pwm_output = (uint16_t)u;
    pid_last_current = current_height;
    boost_mode = (fabsf(e) > 3.0f) ? 1 : 0;   // 仅供显示:远=起飞 近=定高
    __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, pwm_output);
}

// 串口命令解析（标定/调试用，无需重新烧录）：
//   mNNN  进入手动模式并直给PWM=NNN(0~900)，例 m400  —— 用来扫 u_min / u_hover
//   +/-   手动PWM ±10 微调
//   a     退出手动，回自动闭环
//   tNN   设目标高度 NN cm，例 t15
//   s     停机(风扇停)   g  启动闭环
// 单条命令执行（被 Process_UART_Command 按空格/分号切分后逐条调用）
static void Exec_Cmd(char *c)
{
    char msg[160];
    if (c[0] == '\0') return;

    switch (c[0]) {
        case 'm': case 'M': {
            int v = atoi(c + 1);
            if (v < 0) v = 0;
            if (v > (int)PWM_OUTPUT_MAX) v = (int)PWM_OUTPUT_MAX;
            manual_pwm  = (uint16_t)v;
            manual_mode = 1;
            control_enabled = 1;
            ui_state = UI_CURVE; curve_chrome = 0;
            sprintf(msg, ">>MANUAL PWM=%u\r\n", manual_pwm);
            UART_SendStr(msg);
            break;
        }
        case '+':
            if (manual_mode) {
                manual_pwm = (manual_pwm + 10 > (uint16_t)PWM_OUTPUT_MAX) ? (uint16_t)PWM_OUTPUT_MAX : manual_pwm + 10;
                sprintf(msg, ">>MANUAL PWM=%u\r\n", manual_pwm);
                UART_SendStr(msg);
            }
            break;
        case '-':
            if (manual_mode) {
                manual_pwm = (manual_pwm < 10) ? 0 : manual_pwm - 10;
                sprintf(msg, ">>MANUAL PWM=%u\r\n", manual_pwm);
                UART_SendStr(msg);
            }
            break;
        case 'a': case 'A':
            manual_mode = 0;
            control_enabled = 1;
            ui_state = UI_CURVE; curve_head = 0; curve_count = 0; curve_chrome = 0;
            boost_mode  = 1;
            PID_Reset();
            UART_SendStr(">>AUTO (closed-loop)\r\n");
            break;
        case 'o': case 'O':            // o<v>: ToF高度零点/截距; os<v>: 斜率(修支架倾斜/斜率漂移)
            if (c[1] == 's' || c[1] == 'S') {
                g_tof_scale = (float)atof(c + 2);
                sprintf(msg, ">>TOF_SCALE=%.4f\r\n", g_tof_scale);
            } else {
                g_tof_zero = (float)atof(c + 1);
                sprintf(msg, ">>TOF_ZERO=%.2f\r\n", g_tof_zero);
            }
            UART_SendStr(msg);
            break;
        case 't': case 'T': {
            float v = (float)atof(c + 1);
            if (v < TARGET_HEIGHT_MIN) v = TARGET_HEIGHT_MIN;
            if (v > TARGET_HEIGHT_MAX) v = TARGET_HEIGHT_MAX;
            uint8_t t_was_running = (control_enabled && !manual_mode); // 运行中切目标?
            target_height = v;
            ui_state = UI_CURVE; curve_chrome = 0;
            manual_mode = 0;
            control_enabled = 1;
            if (!t_was_running) {       // 仅从停机/手动启动:重置+boost起飞
                boost_mode = 1;
                PID_Reset();
            }
            // 运行中切目标:保留积分/速度状态,靠轨迹斜坡(sp_ramp)平滑过渡,不清积分不boost
            // -> 避免切换瞬间球失去积分补偿先下沉,显著缩短动态跟踪调节时间。
            sprintf(msg, ">>TARGET=%.1f\r\n", target_height);
            UART_SendStr(msg);
            break;
        }
        case 's': case 'S':
            control_enabled = 0;
            manual_mode = 0;
            ui_state = UI_CURVE; curve_chrome = 0;
            pwm_output = 0;
            __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, 0);
            UART_SendStr(">>STOP\r\n");
            break;
        case 'g': case 'G':
            control_enabled = 1;
            manual_mode = 0;
            ui_state = UI_CURVE; curve_head = 0; curve_count = 0; curve_chrome = 0;
            boost_mode  = 1;
            PID_Reset();
            UART_SendStr(">>GO (closed-loop)\r\n");
            break;
        case 'k': case 'K':            // 在线整定: kpNN / kiNN / kdNN ; 单独k查询
            if (c[1] == 'p') Kp = (float)atof(c + 2);
            else if (c[1] == 'i') Ki = (float)atof(c + 2);
            else if (c[1] == 'd') Kd = (float)atof(c + 2);
            sprintf(msg, ">>Kp=%.1f Ki=%.1f Kd=%.1f\r\n", Kp, Ki, Kd);
            UART_SendStr(msg);
            break;
        case 'u': case 'U':            // uhNNNN: 设悬停前馈
            if (c[1] == 'h') u_hover = (float)atof(c + 2);
            sprintf(msg, ">>u_hover=%.0f\r\n", u_hover);
            UART_SendStr(msg);
            break;
        case 'r': case 'R':            // rN.N: 上行目标斜坡速率; rdN.N: 下行斜坡速率(防降目标砸底)
            if (c[1] == 'd' || c[1] == 'D') {
                g_ramp_down_cms = (float)atof(c + 2);
                sprintf(msg, ">>ramp_down=%.1f\r\n", g_ramp_down_cms);
            } else {
                g_ramp_cms = (float)atof(c + 1);
                sprintf(msg, ">>ramp=%.1f\r\n", g_ramp_cms);
            }
            UART_SendStr(msg);
            break;
        case 'l': case 'L':            // lNNN: PWM每拍限幅 (刹车速度)
            g_slew = (float)atof(c + 1);
            sprintf(msg, ">>slew=%.0f\r\n", g_slew);
            UART_SendStr(msg);
            break;
        case 'w': case 'W':            // wNN: 串口心跳周期ms(采样率)。w20=50Hz快记录(看清内环动态),w80=默认12.5Hz省带宽
            {
                int v = atoi(c + 1);
                if (v < 10)  v = 10;          // 115200@~130B/帧 极限~88帧/s,10ms会丢帧
                if (v > 500) v = 500;
                g_serial_ms = (uint32_t)v;
            }
            sprintf(msg, ">>serial_ms=%lu (%.1f Hz)\r\n",
                    (unsigned long)g_serial_ms, 1000.0f / (float)g_serial_ms);
            UART_SendStr(msg);
            break;
        case 'f': case 'F':            // fN.N: 球速EMA系数(0=裸微分 ->0.95=重滤波)。降D项噪声,增相位滞后
            g_deriv_alpha = (float)atof(c + 1);
            if (g_deriv_alpha < 0.0f)  g_deriv_alpha = 0.0f;
            if (g_deriv_alpha > 0.95f) g_deriv_alpha = 0.95f;
            sprintf(msg, ">>deriv_alpha=%.2f\r\n", g_deriv_alpha);
            UART_SendStr(msg);
            break;
        case 'h': case 'H':            // hNNNN: 悬停转速(CTRL_MODE=2 外环基准,跨电池稳定)
            g_hover_rpm = (float)atof(c + 1);
            sprintf(msg, ">>hover_rpm=%.0f\r\n", g_hover_rpm);
            UART_SendStr(msg);
            break;
        case 'd': case 'D':            // dN.N: 误差死区cm(冲±1cm设0试)
            g_deadband = (float)atof(c + 1);
            sprintf(msg, ">>deadband=%.2f\r\n", g_deadband);
            UART_SendStr(msg);
            break;
        case 'v': case 'V':            // 速度观测器: vaN.N(alpha) vbN.NN(beta) vo1/vo0(开关)
            switch (c[1]) {
                case 'a': g_ab_alpha = (float)atof(c + 2); break;
                case 'b': g_ab_beta  = (float)atof(c + 2); break;
                case 'o': g_use_obs  = (c[2] == '1') ? 1 : 0; break;
                default: break;
            }
            sprintf(msg, ">>OBS use=%u alpha=%.2f beta=%.3f\r\n",
                    (unsigned)g_use_obs, g_ab_alpha, g_ab_beta);
            UART_SendStr(msg);
            break;
        case 'j': case 'J':            // jN.N: 测高跳变剔除阈值cm(物理护栏,挡假回波)
            g_max_jump = (float)atof(c + 1);
            sprintf(msg, ">>maxjump=%.1f\r\n", g_max_jump);
            UART_SendStr(msg);
            break;
        case 'p': case 'P':            // pNN: 超声波触发周期ms(采样率,默认80;试50/60看RAW是否仍干净)
            {
                int v = atoi(c + 1);
                if (v < 30) v = 30;            // 低于混响散尽极限会冻结读数
                if (v > 200) v = 200;
                g_ultra_trig_ms = (uint32_t)v;
            }
            sprintf(msg, ">>trig_period=%lu ms\r\n", (unsigned long)g_ultra_trig_ms);
            UART_SendStr(msg);
            break;
        case 'n': case 'N':            // nNNNN: 闭环最低速(怠速地板,风扇不停转)
            g_pwm_min = (float)atof(c + 1);
            if (g_pwm_min < 0) g_pwm_min = 0;
            if (g_pwm_min > PWM_OUTPUT_MAX) g_pwm_min = PWM_OUTPUT_MAX;
            sprintf(msg, ">>pwm_min=%.0f\r\n", g_pwm_min);
            UART_SendStr(msg);
            break;
        case 'x': case 'X':            // xNNNN: 闭环最高速(推力天花板,防窜顶)
            g_pwm_max = (float)atof(c + 1);
            if (g_pwm_max > PWM_OUTPUT_MAX) g_pwm_max = PWM_OUTPUT_MAX;
            if (g_pwm_max < g_pwm_min) g_pwm_max = g_pwm_min;
            sprintf(msg, ">>pwm_max=%.0f\r\n", g_pwm_max);
            UART_SendStr(msg);
            break;
        case 'b': case 'B':            // bNNNN: 待机怠速PWM(停止态风机常转的转速,起飞临界下方)
            g_standby_pwm = (float)atof(c + 1);
            if (g_standby_pwm < 0) g_standby_pwm = 0;
            if (g_standby_pwm > PWM_OUTPUT_MAX) g_standby_pwm = PWM_OUTPUT_MAX;
            sprintf(msg, ">>standby_pwm=%.0f\r\n", g_standby_pwm);
            UART_SendStr(msg);
            break;
        case 'e': case 'E':            // e0/e1: 闭环时示波器整区刷新 关/开(关=控制环零阻塞,A/B验证显示阻塞是否致大摆)
            g_scope_en = (c[1] == '1') ? 1 : 0;
            sprintf(msg, ">>scope_en=%u\r\n", (unsigned)g_scope_en);
            UART_SendStr(msg);
            break;
        case 'y': case 'Y':            // 串级内环(转速闭环): y1/y0开关 ; yaN/ybN FF映射 ; ypN/yiN 内环PI
            switch (c[1]) {
                case '1': g_cascade_en = 1; break;
                case '0': g_cascade_en = 0; rpm_integral = 0.0f; break;
                case 'a': g_rpm_ff_a = (float)atof(c + 2); break;
                case 'b': g_rpm_ff_b = (float)atof(c + 2); break;
                case 'f': g_rpm_alpha = (float)atof(c + 2); break;   // yf: 转速反馈EMA系数
                case 'p': g_rpm_kp   = (float)atof(c + 2); break;
                case 'i': g_rpm_ki   = (float)atof(c + 2); break;
                default: break;
            }
            sprintf(msg, ">>CASCADE en=%u A=%.2f B=%.0f Kp=%.3f Ki=%.3f\r\n",
                    (unsigned)g_cascade_en, g_rpm_ff_a, g_rpm_ff_b, g_rpm_kp, g_rpm_ki);
            UART_SendStr(msg);
            break;
        case 'Z': case 'z':            // LADRC: Z1/Z0开关 ; Zc<wc> Zo<wo> Zb<b0>
            switch (c[1]) {
                case '1': g_ctrl_mode = 1; PID_Reset(); break;
                case '2': g_ctrl_mode = 2; PID_Reset(); break;
                case '0': g_ctrl_mode = 0; break;
                case 'c': g_wc = (float)atof(c + 2); break;
                case 'o': g_wo = (float)atof(c + 2); break;
                case 'b': g_b0 = (float)atof(c + 2); break;
                case 't': g_wt = (float)atof(c + 2); break;
                default: break;
            }
            sprintf(msg, ">>ADRC mode=%u b0=%.3f wc=%.2f wo=%.2f wt=%.2f\r\n",
                    (unsigned)g_ctrl_mode, g_b0, g_wc, g_wo, g_wt);
            UART_SendStr(msg);
            break;
        case 'q': case 'Q':            // q: 打印当前全部可调参数
            sprintf(msg, ">>PARAMS uh=%.0f kp=%.1f ki=%.1f kd=%.1f f=%.2f slew=%.0f min=%.0f max=%.0f T=%.1f\r\n",
                    u_hover, Kp, Ki, Kd, g_deriv_alpha, g_slew, g_pwm_min, g_pwm_max, target_height);
            UART_SendStr(msg);
            sprintf(msg, ">>CASCADE en=%u A=%.2f B=%.0f rKp=%.3f rKi=%.3f hover_rpm=%.0f\r\n",
                    (unsigned)g_cascade_en, g_rpm_ff_a, g_rpm_ff_b, g_rpm_kp, g_rpm_ki, g_hover_rpm);
            UART_SendStr(msg);
            sprintf(msg, ">>ADRC mode=%u b0=%.3f wc=%.2f wo=%.2f wt=%.2f\r\n",
                    (unsigned)g_ctrl_mode, g_b0, g_wc, g_wo, g_wt);
            UART_SendStr(msg);
            break;
        default:
            UART_SendStr(">>? cmd: m/+/-/a/tNN/s/g/kpNN/kiNN/kdNN/uhNNNN/lNNN/wNN/nNNNN/xNNNN/y(cascade)/q\r\n");
            break;
    }
}

// 串口命令解析入口：一行可含多条命令，用 空格 / ; / , / Tab 分隔，逐条执行。
// 例：发一行 "uh3500 kp10 ki0.3 kd50 n3400 x3900 t15" 即可一次性全部生效。
void Process_UART_Command(void)
{
    if (!uart_cmd_ready) return;
    uart_cmd_ready = 0;

    char *p = uart_cmd_buf;
    char token[24];
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ';' || *p == ',') p++;  // skip separators
        if (*p == '\0') break;
        uint8_t k = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != ';' && *p != ',' && k < sizeof(token) - 1)
            token[k++] = *p++;
        token[k] = '\0';
        Exec_Cmd(token);     // 逐条派发
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_CRC_Init();
  MX_ADC1_Init();
  MX_DAC1_Init();
  MX_TIM4_Init();
  MX_TIM8_Init();
  /* USER CODE BEGIN 2 */
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_rx_byte, 1);
#if HEIGHT_SENSOR_TOF
  Tof_Init();                  // VL53L0X laser: bit-bang I2C (PD11/PD12), continuous ranging
  // TIM4 (old ultrasonic capture, now free since ToF uses bit-bang I2C) repurposed as
  // the cascade inner-loop timebase. PSC=239 -> 1MHz tick; set ARR for RPM_INNER_HZ.
  // Run-time config only (no CubeMX edit). HAL_TIM_PeriodElapsedCallback drives the
  // RPM PI at this rate, decoupled from the ~42Hz ToF-driven outer loop.
  __HAL_TIM_SET_PRESCALER(&htim4, 239u);
  __HAL_TIM_SET_AUTORELOAD(&htim4, (1000000u / RPM_INNER_HZ) - 1u);
  __HAL_TIM_SET_COUNTER(&htim4, 0u);
  HAL_TIM_Base_Start_IT(&htim4);
#else
  HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_1);
#endif
  HAL_TIM_PWM_Start(&htim8,TIM_CHANNEL_2);
  GC9A01_Init();
  Tach_Init();                 // fan tach: TIM3_CH1 input capture on PC6
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  static uint32_t display_tick = 0;
  static uint32_t serial_tick  = 0;
  static uint32_t fps_tick     = 0;
  static uint32_t fps_last_cnt = 0;
  static float    g_height_fps = 0.0f;
  while (1)
  {
    // 1. 测高（非阻塞，内部更新 current_height + height_updated）
#if HEIGHT_SENSOR_TOF
    Tof_Measure();             // VL53L0X laser, continuous mode (~20-33ms)
#else
    Ultrasonic_Measure();      // HC-SR04 ultrasonic (~80ms)
#endif

    // 2. 按键处理（内部20ms消抖）+ UI 分发
    KEY_Process();
    if (key_down) {
        UI_OnKey(key_down);
        key_down = 0;          // 消费按下沿，避免重复触发
    }

    // 2.5 串口命令（标定/调试，无需重新烧录）
    Process_UART_Command();

    // 3. 控制环：事件驱动——每次有"新高度样本"到达就跑一次闭环。
    //    这样每步控制都用最新数据、dt 与采样真实间隔一致，球速(D项)估计干净，
    //    不会因固定节拍与传感器节拍错相而产生锯齿速度（不稳定对象靠D项稳定，这点关键）。
    static uint8_t ctrl_en_prev = 0;       // 上一拍 control_enabled，用于检测使能上升沿
    if (!control_enabled) {
        height_updated = 0;            // 停机丢弃挂起样本
        ctrl_en_prev = 0;              // 复位边沿：下次从停机使能时重新预热
        // 待机怠速:停止/待机态让风机常转在起飞临界下方(g_standby_pwm),球稳贴底但风机就绪,
        // 消除冷启动死区→后续起飞秒级。上电后延迟 STANDBY_BOOT_DELAY_MS 再启动且PWM缓升,避EMI冲USB。
        static uint32_t sb_tick = 0;
        if ((uint32_t)(uwTick - sb_tick) >= STANDBY_RAMP_MS) {
            sb_tick = uwTick;
            float sb_target = (uwTick >= STANDBY_BOOT_DELAY_MS) ? g_standby_pwm : 0.0f;
            float p = (float)pwm_output;
            if (p < sb_target)      { p += STANDBY_RAMP_STEP; if (p > sb_target) p = sb_target; }
            else if (p > sb_target) { p -= STANDBY_RAMP_STEP; if (p < sb_target) p = sb_target; }
            pwm_output = (uint16_t)p;
            __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, pwm_output);
        }
    } else {
        // 使能上升沿（从停机/复位态启动）：先预热风机。手动模式风机本就在转，免预热。
        if (!ctrl_en_prev) {
            ctrl_en_prev = 1;
            control_start_tick = uwTick;
            // 预热:风机先转起来越过四线风扇冷启动爬升(数秒),控制接管时已在转。
            // mode2(串级)也预热——去掉预热则boost时风机从0硬爬~6s,起飞更慢。
            // 但若已在待机怠速(风机已转在g_standby_pwm附近),无冷启动可越,跳过预热直接起飞
            //   (否则预热会把PWM从待机~4000降到3300再爬,反而更慢)。
            uint8_t fan_warm = (pwm_output >= (uint16_t)(g_standby_pwm - 200.0f));
            preheating = (manual_mode || fan_warm) ? 0 : 1;
        }

        if (manual_mode) {
            // 手动标定模式：直给PWM，跳过闭环（用于测 u_min / u_hover）
            pwm_output = manual_pwm;
            __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, manual_pwm);
            height_updated = 0;
        } else if (preheating) {
            // 预热期：风机空转在怠速地板(g_pwm_min)，不跑闭环、不积分，越过冷启动死区。
            // D项基准跟随当前球位，投入闭环时不会因预热段位移喷出假球速。
            if ((uint32_t)(uwTick - control_start_tick) < FAN_PREHEAT_MS) {
                // 预热只为暖机(让风机越过静摩擦+冷启动死区先转起来),绝不能吹起球——
                // 否则球被预热顶到高位,投入后稳态偏高且持续上漂(实测停16.8/目标15)。
                // 用低于悬停的暖机PWM(~5000转),预热后球仍在底,由mode2起飞boost(hover+600)
                // 从底干净冲到目标(已验证最稳,稳15 std0.4)。注:预热PWM可低于闭环地板g_pwm_min。
                float pre_pwm = FAN_PREHEAT_PWM;
                if (pre_pwm > g_pwm_max) pre_pwm = g_pwm_max;
                pwm_output = (uint16_t)pre_pwm;
                __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, pwm_output);
                height_updated = 0;
                pid_last_current = current_height;
            } else {
                preheating = 0;
                PID_Reset();           // 预热结束，以当前球位为基准干净投入闭环
            }
        } else if (height_updated) {
            height_updated = 0;
            Control_Update();
        } else if ((uint32_t)(uwTick - height_update_tick) > CTRL_FEEDBACK_TIMEOUT_MS) {
            // 反馈看门狗：闭环已使能但长时间收不到新有效样本（激光拒帧/卡死）。
            // 不再让 PWM 静默僵在预热/旧值（那会把人骗去"调PID"），改为保持开环
            // 悬停前馈 u_hover —— 执行器进入已知安全态；同时重置D项基准，使样本
            // 恢复时不会因跨停滞段的大位移喷出假球速。心跳 A: 字段会同步飙升。
            // CTRL_MODE==2(串级):不在此写PWM——内环ISR用tach自持最后一个rpm_setpoint,
            // 转速环本身就是已知安全态,且写了也会被500Hz ISR在2ms内覆盖。
            if (g_ctrl_mode != 2) {
                pwm_output = (uint16_t)u_hover;
                __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, pwm_output);
            }
            pid_last_current = current_height;
        }
    }

    // 4. OLED显示更新
    // 快记录模式(g_serial_ms<=40,即>=25Hz串口)下,屏幕示波器与PC采集冗余,且其~60ms
    // 阻塞式整区刷新(184x152@7.5MHz SPI无DMA)会周期性卡住主循环、压低遥测帧率。
    // 此时把刷新降到500ms,让50Hz遥测不被显示抢走节拍;正常模式仍用200ms。
    // 进一步:闭环运行(球在受控)时整区刷新会冻结控制环致大摆(2026-06-12根因),故闭环时把刷新
    // 节流到 SCOPE_RUN_PERIOD_MS;g_scope_en=0 则闭环时完全停刷(零阻塞,A/B验证)。停止/待机/手动态不节流。
    uint8_t closed_running = (control_enabled && !manual_mode && !preheating);
    uint32_t disp_ms = (g_serial_ms <= 40u) ? 500u : (uint32_t)DISPLAY_PERIOD_MS;
    if (closed_running) disp_ms = SCOPE_RUN_PERIOD_MS;
    if ((uint32_t)(uwTick - display_tick) >= disp_ms) {
        display_tick = uwTick;
        if (!(closed_running && !g_scope_en)) {   // 闭环且g_scope_en=0:跳过显示,控制环零阻塞
            OLED_Update();
        }
    }

    // 5. 串口心跳（喂给上位机/AI 的真机回报数据）
    // 实测高度采样帧率（每秒统计一次有效样本数 → F: Hz）。这是判断激光是否真跑到
    // 50Hz、还是被显示全刷/位带I2C拖慢的直接依据。
    if ((uint32_t)(uwTick - fps_tick) >= 1000u) {
        uint32_t cnt = g_height_sample_count;
        g_height_fps = (float)(cnt - fps_last_cnt) * 1000.0f / (float)(uwTick - fps_tick);
        fps_last_cnt = cnt;
        fps_tick = uwTick;
    }
    if (uwTick - serial_tick >= g_serial_ms) {
        serial_tick = uwTick;
        char sbuf[220];
#if HEIGHT_SENSOR_TOF
        float raw_cm = (g_tof_raw_mm < 0.0f) ? -1.0f : g_tof_raw_mm * 0.1f;  // mm->cm
#else
        float raw_cm = g_ultra_raw;
#endif
        int len = sprintf(sbuf, "H:%.1f T:%.1f E:%.1f P:%d M:%d D:%.1f R:%lu RAW:%.1f A:%lu F:%.1f PRE:%u CAS:%u RS:%ld CM:%u FH:%.2f"
#if HEIGHT_SENSOR_TOF
                          " TID:0x%02X TIN:%u TST:%u"
#endif
                          "\r\n",
                          current_height, target_height, pid_error,
                          pwm_output, boost_mode, pid_filtered_deriv,
                          (unsigned long)Fan_GetRPM(), raw_cm,
                          (unsigned long)(uwTick - height_update_tick), g_height_fps,
                          (unsigned)preheating, (unsigned)g_cascade_en, (long)rpm_setpoint,
                          (unsigned)g_ctrl_mode, adrc_z3
#if HEIGHT_SENSOR_TOF
                          , g_tof_id, g_tof_init, g_tof_status
#endif
                          );
        if (len > 0)
            HAL_UART_Transmit(&huart1, (uint8_t *)sbuf, (uint16_t)len, 50);
    }

    // 6. 系统状态更新（上电就绪）
    if (system_state == SYSTEM_INIT && uwTick > SYS_READY_MS) {
        system_state = SYSTEM_READY;
        pid_last_time = uwTick;
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // 注意：不要在中断回调中调用printf！
        // HAL_UART_Transmit会阻塞中断，导致系统卡顿
        char ch = (char)uart_rx_byte;
        if (ch == '\r' || ch == '\n')
        {
            if (uart_cmd_idx > 0)
            {
                uart_cmd_buf[uart_cmd_idx] = '\0';
                uart_cmd_ready = 1;
                uart_cmd_idx = 0;
            }
        }
        else
        {
            if (uart_cmd_idx < sizeof(uart_cmd_buf) - 1)
            {
                uart_cmd_buf[uart_cmd_idx++] = ch;
            }
            else
            {
                uart_cmd_idx = 0;
            }
        }

        HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_rx_byte, 1);
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
