#ifndef __CONFIG_H
#define __CONFIG_H

/**
 * @file    config.h
 * @brief   校赛B 智能球平衡控制装置 —— 全局可调参数集中管理
 *
 * 仓库规范：PID、采样率、几何标定、限幅等关键参数一律放这里，
 * 禁止散落在 .c 函数体内。改参数只动这一个文件。
 *
 * 标注 [标定] 的项需真机实测后回填（见 04 调试记录 / 作战地图 M1~M2）。
 */

/* ======================= 调度节拍 ======================= */
/* 控制环节拍。受 HC-SR04 最小触发间隔(~60ms)限制，定为 80ms(=12.5Hz)。
 * 升 TOF 后可降到 20ms(50Hz) 提升带宽。*/
#define CTRL_PERIOD_MS        80u
#define DISPLAY_PERIOD_MS     200u    /* OLED 刷新周期 */
#define SERIAL_PERIOD_MS      80u     /* 串口心跳周期 */
#define KEY_PERIOD_MS         20u     /* (兼容保留) */
#define KEY_SCAN_MS           8u      /* 按键扫描周期 */
#define KEY_REPEAT_DELAY_MS   350u    /* 长按多久后开始连发 */
#define KEY_REPEAT_RATE_MS    90u     /* 连发间隔 */
#define SYS_READY_MS          2000u   /* 上电到就绪(题目要求<5s) */

/* ======================= 超声波测距 ======================= */
#define ULTRA_TRIG_PERIOD_MS  80u     /* 触发间隔(>=60ms) */
#define ULTRA_ECHO_TIMEOUT_MS 10u     /* 回波等待超时(40cm约2.3ms,留余量) */
#define ULTRA_US_PER_CM       58.0f   /* 距离(cm)=回波(us)/58 */
#define ULTRA_RAW_MIN_CM      2.0f    /* 有效原始距离下限 */
#define ULTRA_RAW_MAX_CM      44.0f   /* 有效原始距离上限 */
#define ULTRA_MAX_JUMP_CM     10.0f   /* 相邻两次最大允许跳变,超则丢弃 */
#define ULTRA_REJECT_LIMIT    3u      /* 连续丢弃上限,超则强制接受防卡死 */

/* ======================= 几何标定 [标定] ======================= */
/* 高度 = 腔体总高 - 原始测距 - 传感器偏移 - 球径。需用尺子在 10/20/30cm 标定。*/
#define TUBE_HEIGHT_CM        46.0f   /* [标定] 腔体总高 */
#define SENSOR_OFFSET_CM      2.0f    /* [标定] 传感器安装/盲区偏移 */
#define BALL_DIAMETER_CM      4.0f    /* 标准乒乓球直径(测到球顶,需减去得球底/位置) */

/* ======================= 高度滤波 ======================= */
#define FILTER_SIZE           3       /* 滑动平均窗口(越大越平滑,延迟越高;3=低延迟利于D项) */

/* ======================= PWM 执行器 ======================= */
/* TIM8_CH2, 25kHz, 占空比寄存器范围 0~9600（4线风扇规格频率，分辨率拉高约10倍）*/
#define PWM_FULL_SCALE        9600
#define PWM_OUTPUT_MIN        0.0f
#define PWM_OUTPUT_MAX        9600.0f
#define PWM_BASE              3800.0f /* [标定] 悬停前馈基准(~40%,25kHz后重测回填) */
#define PWM_SLEW_PER_TICK     800     /* 每控制周期 PWM 最大变化量(防过冲) */

/* ======================= 两段式控制 ======================= */
#define BOOST_PWM             5800.0f /* [标定] 起飞推力 */
#define BOOST_DROP_RATIO      0.3f    /* 球过高时的减力比例(×PWM_BASE) */
#define PID_ENTER_ERR         8.0f    /* |误差|<此值 进入PID精调 */
#define PID_EXIT_ERR          10.0f   /* |误差|>此值 退回起飞 */
#define BOOST_HOLD_COUNT      5u      /* 连续N个周期确认才切PID(防虚假读数) */
#define BOOST_NEAR_BAND_CM    2.0f    /* 起飞模式内"接近目标"判定带 */

/* ======================= PID 参数 [标定] ======================= */
/* 不稳定直筒(双积分)对象：u = u_hover + Kp*e - Kd*球速 + Ki*积分。
 * D项(速度阻尼)是稳定关键。下列为起调值，务必用串口 kp/ki/kd/uh 在线整定。*/
#define PID_KP_DEFAULT        150.0f
#define PID_KI_DEFAULT        8.0f
#define PID_KD_DEFAULT        120.0f
#define PID_DERIV_ALPHA       0.6f    /* 速度EMA滤波系数(越大越平滑,但更滞后) */
#define PID_ERROR_DEADBAND    0.3f    /* 误差死区±cm */
#define PID_INTEGRAL_LIMIT    200.0f  /* 积分限幅(对应PWM贡献=Ki*此值) */

/* ======================= 标定(CALIB)界面步长挡位 ======================= */
#define CALIB_STEP_FINE       20      /* 步长：细 */
#define CALIB_STEP_MID        100     /* 步长：中 */
#define CALIB_STEP_COARSE     500     /* 步长：粗 */

/* ======================= 风扇转速测量(tach) ======================= */
/* 四线风扇 tach 线 → 10k上拉3.3V → PC6(TIM3_CH1, AF2)。实测干净3.3V方波。*/
#define TACH_ENABLE           1
#define TACH_PULSES_PER_REV   2       /* 标准四线风扇每转2脉冲 */
#define TACH_TIMEOUT_MS       250u    /* 超此时间无脉冲判为停转 */

/* ======================= 目标高度 ======================= */
#define TARGET_HEIGHT_MIN     3.0f
#define TARGET_HEIGHT_MAX     40.0f
#define TARGET_HEIGHT_DEFAULT 15.0f
#define TARGET_STEP_BIG       5.0f
#define TARGET_STEP_SMALL     1.0f

/* ======================= OLED controller (deprecated, kept for fonts) ======================= */
/* Display switched to GC9A01 round color TFT (below). oled.c still compiles
 * only to provide font symbols (asc2_*, cn16) reused by gc9a01.c; main.c no
 * longer calls any OLED_* function. */
#define OLED_IS_SH1106        0
#if OLED_IS_SH1106
  #define OLED_COL_OFFSET     2
#else
  #define OLED_COL_OFFSET     0
#endif

/* ======================= GC9A01 1.28in round color TFT ======================= */
/* Interface: hardware SPI1, register-level (no HAL SPI; this project lacks
 * stm32h7xx_hal_spi.c). Wiring (same pins as old OLED, drop-in replace):
 *   GC9A01   MCU
 *   VCC  ->  3.3V
 *   GND  ->  GND
 *   SCL  ->  PB3  (SPI1_SCK,  AF5)
 *   SDA  ->  PB5  (SPI1_MOSI, AF5)
 *   DC   ->  PG13 (GPIO out)
 *   CS   ->  PE6  (GPIO out)
 *   RST  ->  PG14 (GPIO out)
 * NOTE: PB3 used as SPI1_SCK means SWO trace is no longer available
 *       (this project uses SWD + UART heartbeat, so no impact). */

#define LCD_W                 240u
#define LCD_H                 240u

/* SPI baud prescaler. SPI1 kernel clock = pll1_q = 240MHz (proven live).
 * MBR: 1=/4(60M) 2=/8(30M) 3=/16(15M) 4=/32(7.5M) 5=/64(3.75M).
 * Snow/garbled on dupont leads = signal integrity at high SCK. 15MHz showed
 * snow here; /32(7.5MHz) is the stable point on flying wires. Once a proper
 * PCB/short leads are used, can climb back to /16 or /8 for frame rate. */
#define LCD_SPI_MBR           4u

/* MADCTL(0x36) orientation. Bits: MY(0x80) MX(0x40) MV(0x20) ML(0x10) BGR(0x08).
 * Horizontal mirror (char order + glyphs + boxes + curves) is done HERE at the
 * panel, NOT in software, so every absolute coordinate stays consistent.
 * 0x88 -> 0xC8: set MX to un-mirror the horizontal axis; keep MY (vertical ok), BGR.
 * If a future panel needs other framing, try {0x48,0x88,0x28,0xE8,0xC8,0x08}. */
#define LCD_MADCTL            0xC8u

/* Boot self-test: flash RED/GREEN/BLUE then black at startup, to verify the
 * SPI+panel pipe end to end. If you see the color flashes, SPI/RES/init are OK
 * and any later black screen is a drawing issue. Set to 0 once UI shows fine. */
#define LCD_BOOT_SELFTEST     0

/* round-screen geometry */
#define LCD_CX                120     /* center x */
#define LCD_CY                120     /* center y */

/* scope plot area: maximized "real-oscilloscope" graticule, centered at
 * (120,120) and inscribed in the R=120 circle. Box corners sit ~1px inside
 * the glass edge: half-diag = sqrt(92^2+76^2)=119.3 < 120. The numeric
 * readouts and the Chinese legend are overlaid INSIDE the grid (scope style),
 * leaving only the narrow top cap for the title. */
#define SCOPE_X0              28
#define SCOPE_Y0              44
#define SCOPE_W               184
#define SCOPE_H               152
#define SCOPE_X1              (SCOPE_X0 + SCOPE_W - 1)
#define SCOPE_Y1              (SCOPE_Y0 + SCOPE_H - 1)
#define SCOPE_DIV_X           6       /* graticule vertical divisions (~6s/div) */
#define SCOPE_DIV_Y           4       /* horizontal divisions = 10cm/div (grid lines land on the cm labels) */
#define SCOPE_HMAX_CM         40.0f   /* full-scale height of the vertical axis */
#define SCOPE_SPEC_CM         1.0f    /* ±spec for the error readout color (题目稳态误差 ≤±1cm) */
#define SCOPE_RDH             12      /* top readout strip height (px), inside the box */
#define SCOPE_PLOT_Y0         (SCOPE_Y0 + SCOPE_RDH)   /* waveform/grid top (below readout strip) */
#define SCOPE_GAP             12      /* sweep blanking-band width (px) ahead of newest sample = clear erase boundary */

#endif /* __CONFIG_H */