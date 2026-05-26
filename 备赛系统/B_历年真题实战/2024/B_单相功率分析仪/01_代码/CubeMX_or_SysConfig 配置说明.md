# 2024 B 单相功率分析仪 —— SysConfig 配置说明

> 平台：MSP430FR5994 LaunchPad  
> 工具：CCS v12.6+ + MSP430Ware

---

## 1. 时钟（CS 模块）

```
CSCTL1: DCORSEL = 1, DCOFSEL = 6 (8MHz DCO)
CSCTL2: SELA = LFXTCLK (32.768kHz), SELS = DCOCLK, SELM = DCOCLK
CSCTL3: DIVA = 1, DIVS = 1, DIVM = 1
```

代码：
```c
CSCTL0_H = CSKEY_H;
CSCTL1   = DCORSEL | DCOFSEL_3;        // 8 MHz
CSCTL2   = SELA__LFXTCLK | SELS__DCOCLK | SELM__DCOCLK;
CSCTL3   = DIVA__1 | DIVS__1 | DIVM__1;
CSCTL4   = LFXTDRIVE_3 | VLOOFF;
CSCTL0_H = 0;
```

## 2. ADC12_B（双通道顺序）

```
ADC12CTL0:  ADC12SHT0_5 | ADC12ON                    // 32 周期采样时间
ADC12CTL1:  ADC12SHP | ADC12CONSEQ_1                  // 序列采样 / pulse mode
ADC12CTL2:  ADC12RES_2                                // 12 bit
ADC12MCTL0: ADC12INCH_0 | ADC12VRSEL_0                // A0 电压
ADC12MCTL1: ADC12INCH_1 | ADC12EOS | ADC12VRSEL_0     // A1 电流，序列结束
ADC12IER0:  ADC12IE1                                  // MEM1 完成中断
```

## 3. TimerA0（ADC 触发源）

```
TA0CCR0  = 1249;          // 8MHz / (1249+1) = 6.4kHz
TA0CCTL0 = CCIE;
TA0CTL   = TASSEL__SMCLK | MC__UP | TACLR;

// ADC 触发：TimerA0 CCR0 上升沿
ADC12CTL1 |= ADC12SHS_1;  // SHS = TA0.0
```

## 4. RTC_C（1Hz 唤醒）

```
RTCCTL0_H = RTCKEY_H;
RTCCTL13  = RTCMODE | RTCRDYIE;        // 日历模式 + 1秒中断
RTCYEAR   = 2026; ...
RTCCTL0_H = 0;
```

## 5. SPI（HT1621 LCD）

```
UCB0CTLW0 = UCSWRST;
UCB0CTLW0 |= UCMST | UCSYNC | UCMSB | UCCKPL | UCSSEL_2;
UCB0BRW    = 8;            // 8MHz / 8 = 1MHz SPI
P5SEL0    |= BIT0 | BIT2;   // P5.0 MOSI, P5.2 CLK
UCB0CTLW0 &= ~UCSWRST;
```

CS 引脚（P5.4）由 GPIO 手动控制。

## 6. GPIO

| 引脚 | 功能 | 配置 |
|---|---|---|
| P1.0 | A0 ADC | P1SEL1\|=BIT0; P1SEL0\|=BIT0 |
| P1.1 | A1 ADC | 同上 BIT1 |
| P5.0 | UCB0 MOSI | P5SEL0\|=BIT0 |
| P5.2 | UCB0 CLK | P5SEL0\|=BIT2 |
| P5.4 | CS | P5DIR\|=BIT4; P5OUT\|=BIT4 |
| P2.5 | KEY1 | P2DIR&=~BIT5; P2REN\|=BIT5; P2OUT\|=BIT5 (上拉) |

## 7. 低功耗

主循环：
```c
while (1) {
    __bis_SR_register(LPM3_bits | GIE);   // 进 LPM3 等待 RTC 唤醒
    // 唤醒后跑 DFT、刷 LCD
}
```

ISR 里 `__bic_SR_register_on_exit(LPM3_bits)` 退出 LPM3。

## 8. 验证编译

```
Build → 0 errors / 0 warnings
Disasm 看 main 函数最末是否有 BIS.W #0xD8, SR （= LPM3+GIE）
```

## 9. 烧录

```
Run → Debug (F11)
→ 等 "Connected to MSP430FR5994"
→ Resume (F8)
```
