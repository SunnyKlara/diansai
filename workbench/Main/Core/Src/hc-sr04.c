#include "hc-sr04.h"
#include "tim.h"
#include "gpio.h"
#include "global.h"
#include "config.h"
#include <math.h>
// 全局变量
// 全局变量
static uint32_t capture_start = 0;    // 上升沿捕获时间
static uint32_t capture_end = 0;      // 下降沿捕获时间
static volatile uint8_t capture_state = 0; // 必须加volatile！
static volatile float distance = -1;  // 最好也加volatile
static volatile uint32_t timeout_counter = 0; // 最好也加volatile

static void delay_us(uint32_t us)
{
    uint32_t ticks = us * 480;
    while (ticks--)
    {
        __NOP();
    }
}

// 发送触发信号（修改为PD11引脚，用微秒级延时）
void Ultrasonic_Trigger(void)
{
  // 拉低至少2us，再发送20us高电平（兼容HC-SR04和HY-SRF05）
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);
  delay_us(2);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);
  delay_us(20); // 20us高电平，兼容两种模块
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);
  
  // 重置状态
  capture_state = 0;
  distance = -1;
  timeout_counter = 0;
  // 重置为上升沿捕获
  __HAL_TIM_SET_CAPTUREPOLARITY(&htim4, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_RISING);
}

// 获取测量距离(cm)
float Ultrasonic_GetDistance(void)
{
  return distance;
}

// 处理超时（需要在主循环中调用）
void Ultrasonic_HandleTimeout(void)
{
  if (capture_state == 1)  // 如果只捕获到上升沿而未捕获到下降沿
  {
    timeout_counter++;
    if (timeout_counter > 50)  // 约50ms超时（主循环1ms调用一次的话）
    {
      capture_state = 0;
      distance = -1;
      // 重置为上升沿捕获
      __HAL_TIM_SET_CAPTUREPOLARITY(&htim4, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_RISING);
    }
  }
}

// 定时器输入捕获回调函数（修改为TIM4）
// 定时器输入捕获回调函数（修改为TIM4）
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM4 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
  {
    if (capture_state == 0)  // 捕获上升沿
    {
      // 记录上升沿时间
      capture_start = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
      // 切换为下降沿捕获
      __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_FALLING);
      capture_state = 1;
      timeout_counter = 0; // 重置超时计数器
    }
    else if (capture_state == 1)  // 捕获下降沿
    {
      // 记录下降沿时间
      capture_end = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
      // 切换回上升沿捕获
      __HAL_TIM_SET_CAPTUREPOLARITY(htim, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_RISING);
      capture_state = 2;
      
      // 计算时间差(考虑溢出情况)
      uint32_t time_diff;
      if (capture_end >= capture_start)
      {
        time_diff = capture_end - capture_start;
      }
      else
      {
        time_diff = (0xFFFF - capture_start) + capture_end;
      }
      
      // 现在TIM4计数器时钟是1MHz，time_diff单位就是微秒！
      float echo_time_us = (float)time_diff;
      
      // 计算距离(cm)：距离 = 时间(us) * 0.0343 / 2 ≈ 时间 / 58
      distance = echo_time_us / ULTRA_US_PER_CM;
      
      // 过滤无效值(HY-SRF05有效范围2-450cm)
      if (distance < 2 || distance > 450)
      {
        distance = -1;
      }
    }
  }
}
u8 qiu_shu = 0;
float chao_ju = 0.0f;
u32 chao_tick = 0;
//void chaoshenbo()
//{
//  if(uwTick - chao_tick < 100)
//    return;
//  chao_tick = uwTick;
//  
//  Ultrasonic_Trigger();
//  
//  // 等待测量完成（最多等待30ms，对应最大距离约5m）
//  uint32_t start = uwTick;
//  while(capture_state != 2 && uwTick - start < 30);
//  
//  float distance = Ultrasonic_GetDistance();
//  
//  // 处理无效值
//  if (distance < 0) {
//    OLED_ShowString(60,48,"----",16);
//  } else {
//    // 显示保留1位小数
//    char buf[8];
//    sprintf(buf, "%.1f", distance);
//    OLED_ShowString(60,48,buf,16);
//  }
//  OLED_UpdateScreen();
//  
//  // 调用超时处理，重置状态
//  Ultrasonic_HandleTimeout();
//}
// 非阻塞超声波测量（含读数跳变验证）
static float last_valid_distance = -1.0f;  // 上一次有效raw_distance
static uint8_t reject_count = 0;

// 新高度样本标志（供主循环事件驱动闭环）
volatile uint8_t  height_updated = 0;
volatile uint32_t height_update_tick = 0;

// 原始测距遥测（滤波前的回波距离，cm；用于诊断传感器跳变）
volatile float g_ultra_raw = -1.0f;

// 跳变剔除阈值(cm),运行时可调('j'命令):按物理极限丢弃假回波(球真实速度上限~150cm/s)
volatile float g_max_jump = ULTRA_MAX_JUMP_CM;

// 超声波触发周期(ms),运行时可调('p'命令):窄管回波混响需散尽,默认80ms;
// 对不稳定对象采样率=生命线,可试 50/60ms 看 RAW 是否仍干净.
volatile uint32_t g_ultra_trig_ms = ULTRA_TRIG_PERIOD_MS;

// 原始测距中值滤波(3点)：剔除HC-SR04偶发尖刺
static float med_buf[3];
static uint8_t med_n = 0, med_idx = 0;
static float Median3(float x)
{
    med_buf[med_idx] = x;
    med_idx = (med_idx + 1) % 3;
    if (med_n < 3) { med_n++; return x; }       // 未填满前直通
    float a = med_buf[0], b = med_buf[1], c = med_buf[2];
    float mx = (a > b) ? a : b; mx = (mx > c) ? mx : c;   // max
    float mn = (a < b) ? a : b; mn = (mn < c) ? mn : c;   // min
    return a + b + c - mx - mn;                  // 中值=总和-最大-最小
}

void Ultrasonic_ResetState(void)
{
    last_valid_distance = -1.0f;
    reject_count = 0;
    med_n = 0; med_idx = 0;
}

void Ultrasonic_Measure(void)
{
    static uint32_t measure_tick = 0;
    static uint8_t measure_state = 0; // 0: 空闲, 1: 等待测量完成

    if (measure_state == 0 && uwTick - measure_tick >= g_ultra_trig_ms) {
        // 每 g_ultra_trig_ms 触发一次测量（HC-SR04 最小间隔约60ms，运行时'p'可调）
        Ultrasonic_Trigger();
        measure_state = 1;
        measure_tick = uwTick;
    }
    else if (measure_state == 1) {
        // 检查测量是否完成
        if (capture_state == 2) {
            float raw_distance = Ultrasonic_GetDistance();
            g_ultra_raw = raw_distance;                   // telemetry: pre-filter raw echo distance
            if (raw_distance > ULTRA_RAW_MIN_CM && raw_distance < ULTRA_RAW_MAX_CM) {
                raw_distance = Median3(raw_distance);     // 3点中值剔尖刺
                // 传感器验证：跳变太大则丢弃（连续丢弃达上限后强制接受，防止卡死）
                if (last_valid_distance > 0 &&
                    fabsf(raw_distance - last_valid_distance) > g_max_jump &&
                    reject_count < ULTRA_REJECT_LIMIT) {
                    reject_count++;
                } else {
                    // 高度 = 腔体总高 - 原始测距 - 传感器偏移 - 球径（几何标定见 config.h）
                    current_height = TUBE_HEIGHT_CM - raw_distance - SENSOR_OFFSET_CM - BALL_DIAMETER_CM;
                    if (current_height < 0.0f) current_height = 0.0f;   // 球趴底/空管时钳到0，不显负数
                    current_height = HeightFilter(current_height);
                    last_valid_distance = raw_distance;
                    reject_count = 0;
                    height_updated = 1;                   // 通知闭环：有新样本
                    height_update_tick = uwTick;
                    g_height_sample_count++;              // 帧率遥测(F:)
                }
            }
            measure_state = 0;
        }
        else if (uwTick - measure_tick >= ULTRA_ECHO_TIMEOUT_MS) {
            // 回波等待超时，复位捕获状态
            Ultrasonic_HandleTimeout();
            measure_state = 0;
        }
    }
}


/* ============================================================================
 * Fan tachometer  (register-level, TIM3_CH1 input capture on PC6 / AF2)
 * ----------------------------------------------------------------------------
 * Why register-level & self-contained here:
 *   - TIM3 and TIM3_IRQHandler are unused everywhere in the project (only the
 *     startup weak stub exists), so we override the weak TIM3_IRQHandler below
 *     without touching tim.c / stm32h7xx_it.c / the Keil .uvprojx.
 *   - TIM3 kernel clock = 240MHz (same APB1 timer domain as TIM4, which uses
 *     PSC=239 -> 1MHz). We mirror that: PSC=239 -> 1us tick, ARR=0xFFFF.
 *
 * Signal: 4-wire fan tach line -> 10k pull-up to 3.3V -> PC6. Scope-verified
 *   clean 0..3.3V square wave, ~317Hz at full speed -> 2 pulses/rev -> ~9520RPM.
 *
 * Measurement: capture rising-edge timestamps, period_us = delta (16-bit safe
 *   for a single wrap via &0xFFFF). RPM = 60e6 / (period_us * pulses_per_rev).
 *   Stop is detected by timeout (no edge within TACH_TIMEOUT_MS).
 * ==========================================================================*/
#if TACH_ENABLE

static volatile uint32_t tach_period_us  = 0;   /* latest edge-to-edge period (us) */
static volatile uint32_t tach_last_tick  = 0;   /* uwTick of last captured edge */
static volatile uint16_t tach_last_ccr   = 0;   /* previous capture value */
static volatile uint8_t  tach_have_prev  = 0;   /* first edge has no valid delta */

void Tach_Init(void)
{
    /* --- GPIO: PC6 as AF2 (TIM3_CH1), pull-up (external 10k already present) --- */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOCEN;
    (void)RCC->AHB4ENR;                              /* sync */

    GPIOC->MODER   = (GPIOC->MODER   & ~(3u << (6 * 2))) | (2u << (6 * 2));   /* AF mode */
    GPIOC->OSPEEDR = (GPIOC->OSPEEDR & ~(3u << (6 * 2))) | (1u << (6 * 2));   /* medium  */
    GPIOC->PUPDR   = (GPIOC->PUPDR   & ~(3u << (6 * 2))) | (1u << (6 * 2));   /* pull-up */
    GPIOC->AFR[0]  = (GPIOC->AFR[0]  & ~(0xFu << (6 * 4))) | (2u << (6 * 4)); /* AF2     */

    /* --- TIM3: 1MHz time base, CH1 input capture on rising edges --- */
    RCC->APB1LENR |= RCC_APB1LENR_TIM3EN;
    (void)RCC->APB1LENR;                             /* sync */

    TIM3->CR1  = 0;
    TIM3->PSC  = 239;                                /* 240MHz / 240 = 1MHz -> 1us tick */
    TIM3->ARR  = 0xFFFF;

    /* CCMR1: CC1S=01 (IC1 mapped to TI1), IC1F=0011 (filter, fSAMPLING=fCK_INT, N=8) */
    TIM3->CCMR1 = (1u << 0) | (0x3u << 4);
    /* CCER: CC1P=0 & CC1NP=0 -> rising edge; CC1E=1 -> enable capture */
    TIM3->CCER  = (1u << 0);

    TIM3->EGR  = TIM_EGR_UG;                         /* load PSC, clear counter */
    TIM3->SR   = 0;                                  /* clear stale flags */
    TIM3->DIER = TIM_DIER_CC1IE;                     /* capture interrupt enable */
    TIM3->CR1  |= TIM_CR1_CEN;                       /* start counter */

    NVIC_SetPriority(TIM3_IRQn, 5);                  /* below ultrasonic(TIM4=0) */
    NVIC_EnableIRQ(TIM3_IRQn);

    tach_have_prev = 0;
    tach_period_us = 0;
    tach_last_tick = uwTick;
}

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_CC1IF) {
        uint16_t ccr = (uint16_t)TIM3->CCR1;         /* reading CCR1 clears CC1IF */
        if (tach_have_prev) {
            tach_period_us = (uint32_t)((uint16_t)(ccr - tach_last_ccr));  /* 16-bit wrap-safe */
        }
        tach_last_ccr  = ccr;
        tach_have_prev = 1;
        tach_last_tick = uwTick;
    }
}

uint32_t Fan_GetRPM(void)
{
    /* No edge within timeout -> treat as stopped. */
    if ((uwTick - tach_last_tick) > TACH_TIMEOUT_MS) {
        return 0;
    }
    uint32_t p = tach_period_us;
    if (p == 0) {
        return 0;
    }
    /* RPM = 60 * 1e6 / (period_us * pulses_per_rev) */
    return 60000000UL / (p * (uint32_t)TACH_PULSES_PER_REV);
}

#endif /* TACH_ENABLE */
