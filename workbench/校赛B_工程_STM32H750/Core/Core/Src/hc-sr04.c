#include "hc-sr04.h"
#include "tim.h"
#include "gpio.h"

// 全局变量
// 全局变量
static uint32_t capture_start = 0;    // 上升沿捕获时间
static uint32_t capture_end = 0;      // 下降沿捕获时间
static volatile uint8_t capture_state = 0; // 必须加volatile！
static volatile float distance = -1;  // 最好也加volatile
static volatile uint32_t timeout_counter = 0; // 最好也加volatile

static void delay_us(uint32_t us)
{
    // 480MHz主频下，每个循环大约需要3个时钟周期
    uint32_t cycles = us * (480 / 3);
    while (cycles--)
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
      distance = echo_time_us / 58.0f;
      
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
void chaoshenbo()
{
  if(uwTick - chao_tick < 100)
    return;
  chao_tick = uwTick;
  
  Ultrasonic_Trigger();
  
  // 等待测量完成（最多等待30ms，对应最大距离约5m）
  uint32_t start = uwTick;
  while(capture_state != 2 && uwTick - start < 30);
  
  float distance = Ultrasonic_GetDistance();
  
  // 处理无效值
  if (distance < 0) {
    OLED_ShowString(60,48,"----",16);
  } else {
    // 显示保留1位小数
    char buf[8];
    sprintf(buf, "%.1f", distance);
    OLED_ShowString(60,48,buf,16);
  }
  OLED_UpdateScreen();
  
  // 调用超时处理，重置状态
  Ultrasonic_HandleTimeout();
}
