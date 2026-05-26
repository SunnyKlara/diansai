# 2024 H 自动行驶小车 —— SysConfig 配置说明

> 平台：MSPM0G3507  
> 工具：Code Composer Studio (CCS) v12.6+ + SysConfig  
> 步骤：照本文档勾选 → Generate → 自动生成 `ti_msp_dl_config.{c,h}` → 编译

---

## 1. 新建工程

```
File → New → CCS Project
  Family       : MSPM0
  Variant      : MSPM0G3507
  Connection   : Texas Instruments XDS110 USB Debug Probe
  Project type : C
  Tool-chain   : TI ARM Compiler
  Project Templates: empty (with main.c)  ← 选最小模板
```

新建后**勾选**：`Use SysConfig` → 加入 `*.syscfg` 文件。

---

## 2. SYSCTL（时钟树）

```
SYSCTL
├─ HFCLK Source           : HFXT (External Crystal)
├─ HFXT Freq              : 32 MHz
├─ HFXT Startup Time      : 1.5 ms
├─ MCLK Source            : SYSPLL_CLK2X
├─ SYSPLL Reference Source: HFCLK
├─ SYSPLL Reference Div   : 4    (32M/4=8M)
├─ SYSPLL QDIV            : 9    (8M*10=80M)  → 80 MHz MCLK ✅
├─ MCLK Divider           : 1
├─ ULPCLK Divider         : 2    (40 MHz 给外设)
└─ MFPCLK Source          : SYSOSC (4 MHz, ADC 备用时钟)
```

> 验证：右下方 SysConfig 状态栏 `MCLK = 80.000 MHz`。

---

## 3. GPIO 配置（按引脚分配表）

打开 `GPIO` → Add 7 个 GROUP：

| GROUP | 成员 | 方向 | 初值 | 用途 |
|---|---|---|---|---|
| GPIO_GRP_MOTOR_DIR | PB6/PB7/PB16/PB17 | OUT | 0 | TB6612 方向位 |
| GPIO_GRP_STBY | PB18 | OUT | 0（启动后软件拉 1）| TB6612 STBY |
| GPIO_GRP_LED | PA20/PA21/PA28 | OUT | 0 | LED |
| GPIO_GRP_BUZ | PA31 | OUT | 0 | 蜂鸣器 |
| GPIO_GRP_KEY | PA18/PA19 | IN, PullUp | — | 按键 |
| GPIO_GRP_IMU_INT | PA17 | IN, PullUp | — | MPU6050 中断 |

---

## 4. TIMA0（电机 PWM）

```
TIMA0
├─ Mode                : PWM
├─ Period (in ticks)    : 15999          ← 80MHz / 5kHz - 1
├─ PWM Mode             : Edge Aligned   ← 也可 Center Aligned 减纹波
├─ Compare 2 (CCP2)     : Output Mode = SET on UP / RESET on Compare
│                         初始 CCV = 0   (PB0 = TB6612 PWMA)
├─ Compare 3 (CCP3)     : 同上          (PB1 = TB6612 PWMB)
└─ Interrupts           : 不用
```

**PinMux**：
- PB0 → CCP2
- PB1 → CCP3

---

## 5. TIMG0 + TIMG6（左右编码器 QEI）

```
TIMG0 (左编码器)
├─ Mode                : Quadrature Counter
├─ Resolution mode     : 4× (上下沿都计)
├─ CCP0 input          : PA12 (A 相)
├─ CCP1 input          : PA13 (B 相)
├─ Period              : 65535 (16 bit 计数器)
└─ Interrupts          : Period Match (溢出统计圈数)
```

TIMG6 同样配置，输入引脚 PA21/PA22。

---

## 6. ADC0（5 路红外）

```
ADC0
├─ Resolution          : 12 bit
├─ Reference           : VDDA (3.3V)
├─ Conversion mode     : Sequence (5 通道)
├─ Trigger             : Software trigger (主控制环每 10ms 触发)
├─ Sample time         : 8 ADC clocks
├─ Memory channels     :
│   MEM0 → IN5 (PA24, IR1)
│   MEM1 → IN6 (PA25, IR2)
│   MEM2 → IN7 (PA26, IR3)
│   MEM3 → IN10 (PA27, IR4)
│   MEM4 → IN9 (PA15, IR5)
└─ Interrupts          : Sequence done → IRQ
```

> ADC 触发频率 = 主控制环 100 Hz，远小于 5 通道 × 200ns = 1μs，无瓶颈。

---

## 7. I2C1（MPU6050）

```
I2C1
├─ Mode                : Master
├─ Speed               : 100 kHz Standard
├─ Glitch filter       : 0
├─ Interrupts          : RX/TX 完成（用于非阻塞读 6 字节加速度 + 6 字节角速度）
├─ PinMux              : SDA = PA10, SCL = PA11
```

> SCL/SDA 在 SysConfig 里直接打勾会自动选 PA10/PA11，**无需手动 GPIO 复用**。

---

## 8. UART0（调试串口，板载 XDS110）

```
UART0
├─ Mode                : UART
├─ Baud rate           : 115200
├─ Data bits           : 8, No parity, 1 stop
├─ Interrupts          : RX 中断（接收命令）
├─ PinMux              : TX = PA9, RX = PA8 （LaunchPad 默认）
```

---

## 9. TIMG12（10 ms 系统 Tick）

```
TIMG12
├─ Mode                : Periodic Timer
├─ Period              : 10 ms (Auto-calc ticks)
├─ Interrupts          : Period match → 调 Tick10ms_ISR()
└─ 内部时钟 32kHz LFCLK，省电
```

---

## 10. 中断优先级（NVIC）

CCS → SysConfig → Interrupts → 调整优先级（数字越小越高）：

| IRQ | 优先级 |
|---|---|
| TIMA0 (PWM 更新) | — 用 |
| TIMG12 (10ms tick) | 1 |
| ADC0 (5 IR done) | 2 |
| GPIO_PA17 (MPU INT) | 2 |
| UART0 (RX) | 4 |
| GPIO_PA18/19 (KEY) | 5 |

---

## 11. Generate

按右上角 `Generate` 按钮 → 自动生成：
- `ti_msp_dl_config.h` （配置头）
- `ti_msp_dl_config.c` （配置初始化代码）

`main.c` 里调用：
```c
#include "ti_msp_dl_config.h"

int main(void) {
    SYSCFG_DL_init();   // 自动生成的初始化函数
    Motor_Init();        // 我们的封装
    ...
}
```

---

## 12. 编译

```
Project → Build Project (Ctrl+B)
```

预期：
- `Build Finished. 0 errors, 0 warnings.`
- 输出 `*.out` 文件（可烧录）

烧录：
```
Run → Debug (F11)
```

---

## 13. 完整 SysConfig 文件参考

`01_代码/项目骨架.syscfg.template`（用 TI 官方 GUI 导出后才能填充，留位）

---

## 14. 常见配置错误

| 错误 | 现象 | 修复 |
|---|---|---|
| 时钟选错（用 SYSOSC 32MHz） | PWM 频率不对 | 改 SYSPLL → 80MHz |
| ADC Memory 通道顺序错 | IR1~IR5 数据错位 | 按本文档 MEM0-4 顺序 |
| TIMA0 Period 太大 | PWM 太慢 < 1kHz | Period = 15999 不能改 |
| 编码器 4× 没勾选 | 速度读数差 1/4 | 勾 4× |
| I2C Pin 复用冲突 | 编译报警 conflict | 重启 SysConfig 检查 |
