# ADC采样与信号调理 —— 从原理到实物

## 一、STM32片内ADC快速上手

### 基本参数（STM32F407为例）
- 分辨率：12bit（0~4095）
- 采样率：最高2.4MSPS（单通道）
- 输入范围：0~3.3V（不能输入负电压！）
- 通道数：最多16个外部通道

### 最简ADC读取代码
```c
// 单次转换读取
uint16_t ADC_ReadChannel(ADC_HandleTypeDef* hadc, uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
    HAL_ADC_ConfigChannel(hadc, &sConfig);
    
    HAL_ADC_Start(hadc);
    HAL_ADC_PollForConversion(hadc, 10);
    uint16_t val = HAL_ADC_GetValue(hadc);
    HAL_ADC_Stop(hadc);
    
    return val;
}

// 转换为电压
float ADC_ToVoltage(uint16_t adc_val)
{
    return (float)adc_val / 4096.0f * 3.3f;
}
```

### DMA连续采样（高速场景必用）
```c
#define ADC_BUF_SIZE 1024
uint16_t adc_dma_buf[ADC_BUF_SIZE];

// 启动DMA连续采样
void ADC_StartDMA(ADC_HandleTypeDef* hadc)
{
    HAL_ADC_Start_DMA(hadc, (uint32_t*)adc_dma_buf, ADC_BUF_SIZE);
}

// DMA传输完成回调
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    // adc_dma_buf 中已经有 ADC_BUF_SIZE 个采样值
    // 在这里做FFT或其他处理
    process_adc_data(adc_dma_buf, ADC_BUF_SIZE);
}
```

## 二、信号调理电路

### 问题：被测信号不在0~3.3V范围内怎么办？

### 场景1：信号太大（如0~30V）→ 电阻分压
```
Vin ──[R1=100kΩ]──┬──[R2=10kΩ]── GND
                   │
                   └── ADC输入
                   
分压比 = R2/(R1+R2) = 10k/110k = 1/11
30V × 1/11 = 2.73V → 在ADC范围内 ✓
```

### 场景2：信号有负电压（如±5V）→ 偏置+分压
```
Vin ──[R1=20kΩ]──┬──[R2=10kΩ]── 3.3V (偏置)
                  │
                  └── ADC输入

当Vin=-5V: Vadc = (-5×10k + 3.3×20k)/(20k+10k) = 0.53V
当Vin=+5V: Vadc = (5×10k + 3.3×20k)/(20k+10k) = 3.87V → 超了！

需要调整电阻值，确保全范围在0~3.3V内
```

### 场景3：信号太小（如10mV）→ 运放放大
```
         ┌──[Rf=100kΩ]──┐
         │               │
Vin ──[Ri=1kΩ]──[─\     │
                   >──┬──┘── Vout
         ┌──────[+/   │
         │            │
        GND          (增益 = -Rf/Ri = -100)
        
Vout = -100 × Vin
10mV → 1V (在ADC范围内)

注意：反相放大器输出有负电压，需要加偏置
推荐用同相放大器或仪表放大器
```

### 推荐：同相放大器（不反相，更方便）
```
Vin ──[+\
        >──┬── Vout = (1 + Rf/Rg) × Vin
   ┌──[─/  │
   │       │
   └──[Rg]─┤
            │
          [Rf]
            │
           GND

选 Rf=99kΩ, Rg=1kΩ → 增益=100
```

### 运放选型建议
| 场景 | 推荐运放 | 关键参数 |
|------|----------|----------|
| 低频精密测量 | OPA2277 | 低偏置10μV，低噪声 |
| 高速信号(>1MHz) | AD8065 | GBW=145MHz |
| 通用场景 | LM358/TL072 | 便宜，够用 |
| 单电源 | LMV324 | 轨到轨输出 |

## 三、校准方法

### 两点校准（最常用）
```c
// 用万用表测两个已知电压，记录ADC值
// 点1: V1=0.5V, ADC1=620
// 点2: V2=3.0V, ADC2=3720

float adc_to_voltage_calibrated(uint16_t adc_val)
{
    // 线性插值
    float slope = (3.0f - 0.5f) / (3720.0f - 620.0f);  // V/ADC
    float offset = 0.5f - slope * 620.0f;
    return slope * adc_val + offset;
}
```

## 四、电赛中各题目的ADC需求

| 题目 | 采样率需求 | 精度需求 | 推荐方案 |
|------|-----------|----------|----------|
| 2021A THD | ≥1MSPS | 12bit够 | STM32F4片内ADC |
| 2024B 功率 | 10~50kHz | 高精度 | ADS1256(24bit) |
| 2025D TDR | ≥100MSPS | 8bit够 | AD9226或比较器 |
| 2023C LCR | 100kHz~1MHz | 12bit | STM32片内ADC |
