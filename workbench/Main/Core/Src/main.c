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
uint8_t control_enabled = 0;       // 控制使能标志（开机在菜单，风扇停）

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
float g_deriv_alpha = PID_DERIV_ALPHA;    // 'f' 球速EMA系数(越大越平滑越滞后)
float g_slew        = (float)PWM_SLEW_PER_TICK; // 'l' PWM每拍限幅
float g_dmax        = D_TERM_CLAMP;        // 'c' D项(Kd*球速)最大PWM贡献钳位,挡假球速踹飞
float g_pwm_min     = PWM_RUN_MIN;         // 'n' 闭环最低速(怠速地板,风扇不停转)
float g_pwm_max     = PWM_RUN_MAX;         // 'x' 闭环最高速(推力天花板,防窜顶)
volatile uint32_t g_height_sample_count = 0; // 有效高度样本累计数(用于实测帧率F:)

// 串级内环(转速闭环)：默认关闭=单环已验证行为；'y1'打开后内环用tach把风机线性化
volatile uint8_t g_cascade_en = CASCADE_RPM_DEFAULT; // 0=单环 1=串级内环
float g_rpm_ff_a   = RPM_FF_A_DEFAULT;   // PWM->RPM 斜率(前馈)
float g_rpm_ff_b   = RPM_FF_B_DEFAULT;   // PWM->RPM 截距(前馈)
float g_rpm_kp     = RPM_KP_DEFAULT;     // 内环比例
float g_rpm_ki     = RPM_KI_DEFAULT;     // 内环积分
float rpm_integral = 0.0f;               // 内环积分累计
float rpm_setpoint = 0.0f;               // 内环目标转速(遥测)

float last_pwm_output = 0.0f;
float pwm_slew_last = 0.0f;        // 斜率限幅记忆的"上一次实际输出"(PID_Reset 同步到怠速地板)
uint32_t pid_last_time = 0;        // 供 PID_Control 计算 dt（与控制节拍解耦）
float pid_last_current = 0.0f;     // 上一次高度（微分基于高度变化，非误差）

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
    pwm_slew_last = g_pwm_min;           // 斜率限幅从怠速地板起步,避免投入瞬间跌破地板
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
    GC9A01_FlushRegion(SCOPE_X0, SCOPE_PLOT_Y0, SCOPE_X1, SCOPE_Y1);

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

    // 干净 PID：误差直接用目标高度（已移除轨迹斜坡 ramp）
    float e = target_height - current_height;
    pid_error = e;

    // 误差死区（抑制末端抖动；±PID_ERROR_DEADBAND）
    float e_db = e;
    if (e_db > -PID_ERROR_DEADBAND && e_db < PID_ERROR_DEADBAND) e_db = 0.0f;

    // 球速=高度变化率。裸微分(g_deriv_alpha=0)等于已验证单环行为；
    // 串口 'f' 把 alpha 从 0 上调=对球速做 EMA 低通,压住 42Hz 下测高噪声经微分放大出的假球速
    // (噪声~15cm/s × Kd 可达 ~800 计数 > 控制区间 192),代价是相位滞后。微分作用在测量上,避免目标突变"微分踢"。
    float v_raw = (current_height - pid_last_current) / dt;
    pid_filtered_deriv = g_deriv_alpha * pid_filtered_deriv + (1.0f - g_deriv_alpha) * v_raw;
    float v = pid_filtered_deriv;   // 实际用于阻尼的球速(已按 alpha 滤波);同时喂遥测 D 字段

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
        case 't': case 'T': {
            float v = (float)atof(c + 1);
            if (v < TARGET_HEIGHT_MIN) v = TARGET_HEIGHT_MIN;
            if (v > TARGET_HEIGHT_MAX) v = TARGET_HEIGHT_MAX;
            target_height = v;
            ui_state = UI_CURVE; curve_chrome = 0;
            manual_mode = 0;
            control_enabled = 1;
            boost_mode = 1;
            PID_Reset();
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
        case 'l': case 'L':            // lNNN: PWM每拍限幅 (刹车速度)
            g_slew = (float)atof(c + 1);
            sprintf(msg, ">>slew=%.0f\r\n", g_slew);
            UART_SendStr(msg);
            break;
        case 'f': case 'F':            // fN.N: 球速EMA系数(0=裸微分 ->0.95=重滤波)。降D项噪声,增相位滞后
            g_deriv_alpha = (float)atof(c + 1);
            if (g_deriv_alpha < 0.0f)  g_deriv_alpha = 0.0f;
            if (g_deriv_alpha > 0.95f) g_deriv_alpha = 0.95f;
            sprintf(msg, ">>deriv_alpha=%.2f\r\n", g_deriv_alpha);
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
        case 'y': case 'Y':            // 串级内环(转速闭环): y1/y0开关 ; yaN/ybN FF映射 ; ypN/yiN 内环PI
            switch (c[1]) {
                case '1': g_cascade_en = 1; break;
                case '0': g_cascade_en = 0; rpm_integral = 0.0f; break;
                case 'a': g_rpm_ff_a = (float)atof(c + 2); break;
                case 'b': g_rpm_ff_b = (float)atof(c + 2); break;
                case 'p': g_rpm_kp   = (float)atof(c + 2); break;
                case 'i': g_rpm_ki   = (float)atof(c + 2); break;
                default: break;
            }
            sprintf(msg, ">>CASCADE en=%u A=%.2f B=%.0f Kp=%.3f Ki=%.3f\r\n",
                    (unsigned)g_cascade_en, g_rpm_ff_a, g_rpm_ff_b, g_rpm_kp, g_rpm_ki);
            UART_SendStr(msg);
            break;
        case 'q': case 'Q':            // q: 打印当前全部可调参数
            sprintf(msg, ">>PARAMS uh=%.0f kp=%.1f ki=%.1f kd=%.1f f=%.2f slew=%.0f min=%.0f max=%.0f T=%.1f\r\n",
                    u_hover, Kp, Ki, Kd, g_deriv_alpha, g_slew, g_pwm_min, g_pwm_max, target_height);
            UART_SendStr(msg);
            sprintf(msg, ">>CASCADE en=%u A=%.2f B=%.0f rKp=%.3f rKi=%.3f\r\n",
                    (unsigned)g_cascade_en, g_rpm_ff_a, g_rpm_ff_b, g_rpm_kp, g_rpm_ki);
            UART_SendStr(msg);
            break;
        default:
            UART_SendStr(">>? cmd: m/+/-/a/tNN/s/g/kpNN/kiNN/kdNN/uhNNNN/lNNN/nNNNN/xNNNN/y(cascade)/q\r\n");
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
    } else {
        // 使能上升沿（从停机/复位态启动）：先预热风机。手动模式风机本就在转，免预热。
        if (!ctrl_en_prev) {
            ctrl_en_prev = 1;
            control_start_tick = uwTick;
            preheating = manual_mode ? 0 : 1;
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
                pwm_output = (uint16_t)g_pwm_min;
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
            pwm_output = (uint16_t)u_hover;
            __HAL_TIM_SetCompare(&htim8, TIM_CHANNEL_2, pwm_output);
            pid_last_current = current_height;
        }
    }

    // 4. OLED显示更新
    if (uwTick - display_tick >= DISPLAY_PERIOD_MS) {
        display_tick = uwTick;
        OLED_Update();
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
    if (uwTick - serial_tick >= SERIAL_PERIOD_MS) {
        serial_tick = uwTick;
        char sbuf[200];
#if HEIGHT_SENSOR_TOF
        float raw_cm = (g_tof_raw_mm < 0.0f) ? -1.0f : g_tof_raw_mm * 0.1f;  // mm->cm
#else
        float raw_cm = g_ultra_raw;
#endif
        int len = sprintf(sbuf, "H:%.1f T:%.1f E:%.1f P:%d M:%d D:%.1f R:%lu RAW:%.1f A:%lu F:%.1f PRE:%u CAS:%u RS:%ld"
#if HEIGHT_SENSOR_TOF
                          " TID:0x%02X TIN:%u TST:%u"
#endif
                          "\r\n",
                          current_height, target_height, pid_error,
                          pwm_output, boost_mode, pid_filtered_deriv,
                          (unsigned long)Fan_GetRPM(), raw_cm,
                          (unsigned long)(uwTick - height_update_tick), g_height_fps,
                          (unsigned)preheating, (unsigned)g_cascade_en, (long)rpm_setpoint
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
