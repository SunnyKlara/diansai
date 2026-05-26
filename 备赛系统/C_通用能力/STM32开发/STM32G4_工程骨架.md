# STM32G4 工程骨架（拿来即用）

> 适用：电源类（2023A / 2025A / 2022A / 2021B）+ 高速控制类  
> 平台：STM32G474 LaunchPad（NUCLEO-G474RE）/ STM32G431 / STM32G491  
> 参考已落地工程：`备赛系统/B_历年真题实战/2023/A_单相逆变器并联/01_代码/`、`2025/A_能量回馈变流器/01_代码/`

---

## 一、4 步上手

```bash
# 1. 复制骨架（电源题选 2025/A，控制题选 2023/A）
xcopy "备赛系统\B_历年真题实战\2025\A_能量回馈变流器\01_代码"  "<新题>\01_代码" /E

# 2. 改 config.h 的 4 个区块（系统/外设/控制/保护）
# 3. 在 CubeMX 里复用已有的引脚分配表（直接 import .ioc）
# 4. 改 main.c 的状态机就能跑起来
```

---

## 二、目录骨架

```
01_代码/
├── README.md
├── config.h                  ← 全局可调参数（必看，所有阈值都在这）
│
├── Core/
│   └── main.c                ← 状态机 + 时序（1ms / 10ms / 100ms / 1s）
│
├── Drivers/                  ← 真实 HAL（直接调 stm32g4xx_hal.h）
│   ├── pwm_xxx.{c,h}         ← TIM / HRTIM 互补 PWM 启停
│   ├── adc_sample.{c,h}      ← ADC + DMA + RMS / DFT
│   ├── display.{c,h}         ← SSD1306 OLED
│   ├── uart_log.{c,h}        ← printf + LOG ON/OFF
│   ├── protection.{c,h}      ← 软件保护（OVP / OCP / OTP）
│   └── encoder.{c,h}         ← TIM Encoder Mode（控制类）
│
├── Algorithm/                ← 与硬件解耦，可 PC 编译
│   ├── pid.{c,h}             ← 通用 PID（位置式 + 增量式）
│   ├── voltage_loop.{c,h}    ← 电压外环 PI（电源类）
│   ├── current_loop.{c,h}    ← 电流内环 PI
│   ├── svpwm.{c,h}           ← SVPWM 算法（三相）
│   └── fft.{c,h}             ← Cooley-Tukey 1024 点 FFT
│
└── tests/                    ← PC 端 Python 金标准
    ├── algo_reference.py     ← 镜像 Algorithm 的 Python 实现
    └── cal_helper.py         ← UART 调参 / 校准助手
```

---

## 三、config.h 模板（直接复制改）

```c
#ifndef __CONFIG_H
#define __CONFIG_H
#include <stdint.h>

/* ============ 1. 系统时钟 ============ */
#define SYS_CLK_HZ              170000000UL    /* G474 max 170MHz */
#define HSE_FREQ_HZ             24000000UL     /* 板载 HSE 8/24MHz */

/* ============ 2. PWM ============ */
#define PWM_FSW_HZ              20000U
#define PWM_PERIOD              ((SYS_CLK_HZ / 2 / PWM_FSW_HZ) - 1)
#define DEAD_TIME_NS            800
#define DEAD_TIME_DTG           68U     /* 800ns @ 85MHz */

/* ============ 3. 输出参数 ============ */
#define VOUT_REF_V              24.0f
#define IOUT_RATED_RMS_A        2.0f
#define FREQ_OUT_HZ             50.0f
#define SAMPLES_PER_CYCLE       400U    /* PWM_FSW_HZ / FREQ_OUT_HZ */

/* ============ 4. 控制环 ============ */
#define VLOOP_PERIOD_MS         10
#define VLOOP_KP                0.020f
#define VLOOP_KI                0.005f
#define MOD_INDEX_MIN           0.10f
#define MOD_INDEX_MAX           0.95f
#define MOD_INDEX_INIT          0.70f
#define RAMP_DURATION_MS        500

/* ============ 5. ADC ============ */
#define ADC_RESOLUTION          4096U
#define ADC_VREF_V              3.3f
#define ADC_OFFSET_RAW          2048U   /* 1.65V 中心 */
#define V_DIVIDER_RATIO         11.0f
#define V_SCALE                 (ADC_VREF_V / (float)ADC_RESOLUTION * V_DIVIDER_RATIO)
#define I_SENSOR_GAIN_VA        0.185f  /* ACS712-5A */
#define I_SCALE                 ((ADC_VREF_V / (float)ADC_RESOLUTION) / I_SENSOR_GAIN_VA)

/* ============ 6. 保护 ============ */
#define VOUT_OVP_THRESHOLD      30.0f
#define IOUT_OCP_THRESHOLD      3.0f
#define VDC_OVP_THRESHOLD       55.0f
#define TEMP_OTP_THRESHOLD      80.0f
#define PROT_CONFIRM_COUNT      3
#define PROT_CHECK_PERIOD_MS    50

/* ============ 7. 显示 ============ */
#define OLED_I2C_ADDR           0x78
#define DISP_PERIOD_MS          200

#endif
```

---

## 四、main.c 状态机骨架

```c
#include "main.h"
#include "config.h"

typedef enum {
    ST_IDLE = 0,
    ST_PRECHARGE,
    ST_RAMPUP,
    ST_RUN,
    ST_FAULT,
    ST_STOP,
} SysState;

static volatile SysState g_state = ST_IDLE;

static volatile uint32_t tick_1ms = 0;
static volatile uint32_t tick_10ms = 0;
static volatile uint32_t tick_100ms = 0;

void HAL_SYSTICK_Callback(void)
{
    tick_1ms++;
    if (tick_1ms % 10 == 0) tick_10ms++;
    if (tick_1ms % 100 == 0) tick_100ms++;
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_TIM1_Init();      /* PWM */
    MX_ADC1_Init();      /* 采样 */
    MX_I2C1_Init();      /* OLED */
    MX_USART2_UART_Init();/* 调试 */

    Display_Init();
    Protection_Init();
    Control_Init(VOUT_REF_V);

    uint32_t last_10ms = 0, last_100ms = 0;

    while (1) {
        /* 10 ms 任务 */
        if (tick_10ms != last_10ms) {
            last_10ms = tick_10ms;
            switch (g_state) {
            case ST_IDLE:
                if (g_user_start) g_state = ST_PRECHARGE;
                break;
            case ST_PRECHARGE:
                if (vdc_filtered > VDC_PRECHARGE_V) g_state = ST_RAMPUP;
                break;
            case ST_RAMPUP:
                /* 软启动 */
                if (rampup_done()) g_state = ST_RUN;
                break;
            case ST_RUN:
                m_modulation = Control_VoltageLoop(vout_rms);
                /* 写入 PWM */
                break;
            case ST_FAULT:
                PWM_Stop();
                if (g_user_reset) g_state = ST_IDLE;
                break;
            }

            /* 故障检测 */
            if (Protection_Check()) g_state = ST_FAULT;
        }

        /* 100 ms 任务（显示）*/
        if (tick_100ms != last_100ms) {
            last_100ms = tick_100ms;
            Display_Update(&g_meas, g_state);
        }
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        /* PWM 中断：写下一个 SPWM 占空比 */
        float sin_val = SPWM_GetNextValue();
        uint32_t duty = (uint32_t)((1.0f + m_modulation * sin_val) * 0.5f * PWM_PERIOD);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty);
    }
}
```

---

## 五、CubeMX 必勾清单

| 外设 | 配置 |
|---|---|
| **RCC** | HSE Crystal/Ceramic, PLL=170MHz, VCO=340MHz, CPU/AHB/APB1/APB2 = 170/170/170 |
| **TIM1** | 中心对齐 PWM Mode 1, ARR=4249, DTG=68, CH1/CH1N + CH2/CH2N 互补, 启用 Update 中断 |
| **HRTIM** | 仅高速场合（>100kHz）；2025A 必用 |
| **ADC1** | 12bit, 扫描模式, 触发=TIM1 TRGO, DMA 循环, 半完成+完成中断 |
| **DMA1** | ADC1: Memory→Peripheral, Circular, 16bit |
| **I2C1** | 400kHz, 7bit 地址 |
| **USART2** | 115200 8N1, IT 模式 |
| **GPIO** | 启动按键 PC13 上拉输入, 状态 LED PA5 推挽 |
| **SysTick** | 1ms |

---

## 六、ST-Link 烧录 / 调试

```bash
# 命令行烧录
STM32_Programmer_CLI.exe -c port=SWD -d xxx.bin 0x08000000 -v -hardRst

# OpenOCD（Linux/macOS）
openocd -f interface/stlink.cfg -f target/stm32g4x.cfg \
        -c "program xxx.elf verify reset exit"
```

---

## 七、串口调试（推荐 LOG 协议）

```c
/* 装置端实现这几条命令，PC 端 cal_helper.py 直接复用 */
void uart_dispatch(const char *cmd)
{
    if (strncmp(cmd, "STAT", 4) == 0) {
        printf("vout=%.2f iout=%.2f m=%.3f vdc=%.1f state=%d\r\n",
               vout_rms, iout_rms, m_modulation, vdc, g_state);
    }
    else if (strncmp(cmd, "PI ", 3) == 0) {
        sscanf(cmd+3, "KP=%f KI=%f", &kp_new, &ki_new);
        Control_SetGains(kp_new, ki_new);
        printf("OK PI KP=%.4f KI=%.4f\r\n", kp_new, ki_new);
    }
    else if (strncmp(cmd, "VREF ", 5) == 0) { ... }
    else if (strncmp(cmd, "START", 5) == 0) { ... }
    else if (strncmp(cmd, "STOP", 4) == 0)  { ... }
    else if (strncmp(cmd, "RST", 3) == 0)   { NVIC_SystemReset(); }
}
```

---

## 八、5 大踩坑

1. **HSE 不起振** → CubeMX 的 RCC HSE Bypass / Crystal 选错；用万用表测 OSC_IN 应有 24MHz
2. **HRTIM 配置错** → 时钟必须是 PLLR×2 = 340MHz；ARR 值不对 → 占空比不对
3. **ADC 触发不同步** → ExternalTrigConv 必须选 TIM1 TRGO；TRGO 来源在 TIM1 → Master Output → Update
4. **DMA 中断不进** → CubeMX 里 ADC 必须勾 DMA Continuous Requests + Circular
5. **Printf 没输出** → 用 retarget.c 重定向 fputc 到 USART2；记得 `setbuf(stdout, NULL);`

---

## 九、PC 端调试快捷键

```bash
# 1. 看实时数据
python tests/cal_helper.py --port COM3 --stat

# 2. 调 PI（不用重新烧录）
python tests/cal_helper.py --port COM3 --tune-pi --kp 0.025 --ki 0.008

# 3. PC 仿真验证（无需硬件）
python tests/cal_helper.py --simulate --kp 0.020 --ki 0.005

# 4. 一键校准 V/I 增益
python tests/cal_helper.py --port COM3 --calib
```

---

## 十、常见 HAL 调用速查

```c
/* GPIO */
HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
GPIO_PinState s = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);

/* PWM */
HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty);

/* ADC + DMA */
HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_buf, ADC_BUF_LEN);
/* 在 HAL_ADC_ConvHalfCpltCallback / HAL_ADC_ConvCpltCallback 处理 */

/* 串口 */
HAL_UART_Transmit(&huart2, (uint8_t*)buf, len, 100);
HAL_UART_Receive_IT(&huart2, &rx_byte, 1);

/* I2C OLED */
HAL_I2C_Master_Transmit(&hi2c1, OLED_ADDR, data, len, 10);

/* Delay */
HAL_Delay(100);    /* 1ms 精度，不能在中断用 */
```

---

## 十一、降级清单（关键）

如果 STM32G474 配置不通：

| 降级 | 操作 | 影响 |
|---|---|---|
| HRTIM → TIM1 中心对齐 | 改 ARR=4249, DTG=68 | 死区 800ns 仍可达，开关频率最高 100kHz |
| 双 ADC → 单 ADC | 用 ADC1 IN1+IN2 多通道扫描 | 采样率减半，仍可工作 |
| HSE 24MHz → HSI 16MHz | RCC Source 改 HSI | 主频降到 144MHz，电源题影响小 |

---

## 十二、参考工程

- **2023A 单相逆变器并联**：状态机 + 软启动 + 电压外环 PI 完整实现
- **2025A 能量回馈变流器**：HRTIM + 三相 ADC + SVPWM + 同步整流（最高难度）
- **2021A 信号失真度测量装置**：1024 点 FFT + 平顶窗（仪表类）
- **2025G 电路模型探究装置**：DDS + 双 ADC 同步 + DAC X-Y（仪表类）
