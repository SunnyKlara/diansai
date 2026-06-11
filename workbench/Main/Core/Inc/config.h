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

/* 反馈看门狗：闭环下若超过此时长(ms)没有新的有效高度样本，判定反馈停滞。
 * 此时不再让 PWM 静默冻结在旧值(那会让人误以为在调PID,其实在调死数据)，
 * 改为保持开环悬停前馈 u_hover，并把停滞时长 A: 打进心跳，肉眼即可判断
 * 究竟是"反馈死了"还是"PID没调好"。约 ~7 个高速帧(20ms)。*/
#define CTRL_FEEDBACK_TIMEOUT_MS  150u

#define DISPLAY_PERIOD_MS     200u    /* OLED 刷新周期 */
#define SERIAL_PERIOD_MS      80u     /* 串口心跳周期 */
#define KEY_PERIOD_MS         20u     /* (兼容保留) */
#define KEY_SCAN_MS           8u      /* 按键扫描周期 */
#define KEY_REPEAT_DELAY_MS   350u    /* 长按多久后开始连发 */
#define KEY_REPEAT_RATE_MS    90u     /* 连发间隔 */
#define SYS_READY_MS          2000u   /* 上电到就绪(题目要求<5s) */

/* 风机预热(借鉴 davidoises/AirLevitatorPIDControl)：闭环使能后，先让风机在怠速地板
 * (PWM_RUN_MIN)空转此时长，越过四线风扇"冷启动死区 + 静摩擦 + ~3s 转速爬升"再投入闭环。
 * 这样控制器接管时风机已在转，不会被启动滞后骗成"PID 不稳"。题目要求 <5s 进入工作态，
 * 故 <=3.5s 预热 + 余量仍满足。预热期不积分、不更新 D 项基准。*/
#define FAN_PREHEAT_MS        3000u

/* ======================= 超声波测距 ======================= */
#define ULTRA_TRIG_PERIOD_MS  80u     /* 触发间隔(原始可信值);窄管回波混响需~60-80ms散尽,提到20/40ms会冻结读数(实测).改此值前必须验证fan-on下测高仍跟随球 */
#define ULTRA_ECHO_TIMEOUT_MS 10u     /* 回波等待超时(40cm约2.3ms,留余量) */
#define ULTRA_US_PER_CM       58.0f   /* 距离(cm)=回波(us)/58 */
#define ULTRA_RAW_MIN_CM      2.0f    /* 有效原始距离下限 */
#define ULTRA_RAW_MAX_CM      44.0f   /* 有效原始距离上限 */
#define ULTRA_MAX_JUMP_CM     15.0f   /* 相邻两次最大允许跳变(cm);实测球真实运动峰值~8cm/80ms,5cm阈值会误伤快速运动致H台阶卡顿(2026-06-11诊断).放宽到15只挡真粗故障,Median3兜底孤立假回波;运行时'j'可调 */
#define ULTRA_REJECT_LIMIT    3u      /* 连续丢弃上限,超则强制接受防卡死 */

/* ======================= 测高传感器选择 ======================= */
/* Height feedback source. The control layer only ever touches current_height /
 * height_updated, so swapping the source here changes nothing downstream.
 *   0 = HC-SR04 ultrasonic (legacy, proven on-board, fallback)
 *   1 = VL53L0X ToF laser  (new path, must pass on-board before removing US)
 * Keep at 0 until the laser path is validated fan-on (hand-track ball, +/-1cm). */
#define HEIGHT_SENSOR_TOF     1

/* ======================= VL53L0X ToF laser (bit-bang I2C) ======================= */
/* Driver: ported ST VL53L0X API (core/platform/demo) from the 野火 reference
 * project (workbench/15.VL53L0X激光测距实验). I2C is software bit-bang on the
 * two pins freed by dropping the ultrasonic (TRIG/ECHO), so no CubeMX regen and
 * no extra board pins. Read path is reworked to CONTINUOUS + non-blocking
 * data-ready poll, feeding the same height_updated event the loop already uses.
 *
 * Wiring (drop-in on the freed ultrasonic dupont leads):
 *   VL53L0X   MCU
 *   VCC  ->   3.3V
 *   GND  ->   GND
 *   SCL  ->   PD11  (was TRIG, GPIO push-pull, master drives only)
 *   SDA  ->   PD12  (was ECHO/TIM4_CH1, reconfigured to GPIO in/out)
 *   XSH  ->   3.3V  (single sensor: tie high = always enabled, no addr reset)
 *   INT  ->   NC    (poll VL53L0X_GetMeasurementDataReady each control tick)
 * If onboard pull-ups are absent, add 4.7k from SDA/SCL to 3.3V. */
#define TOF_SCL_PORT          GPIOD
#define TOF_SCL_PIN           GPIO_PIN_11
#define TOF_SDA_PORT          GPIOD
#define TOF_SDA_PIN           GPIO_PIN_12
#define TOF_I2C_ADDR8         0x52u    /* power-on default 8-bit address (7-bit 0x29) */

/* Timing budget / mode. 0=default(33ms) 1=high-accuracy(200ms) 2=long-range
 * 3=high-speed(20ms). High-speed unlocks the 50Hz loop (set CTRL_PERIOD_MS=20). */
#define TOF_MODE              3u
#define TOF_RAW_MIN_MM        20.0f    /* valid raw distance floor (mm) */
#define TOF_RAW_MAX_MM        500.0f   /* valid raw distance ceiling (mm); tube < this */

/* ToF geometry (independent from the ultrasonic constants so the fallback path
 * is unaffected). height_cm = TOF_ZERO_CM - raw_cm.
 * [CAL] TOF_ZERO_CM = the raw distance (cm) the laser reads when the ball rests
 * at the bottom (height = 0). Measure on-board and fill in. */
#define TOF_ZERO_CM           47.0f

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
#define PWM_BASE              3420.0f /* [辨识实测 2026-06-11] 本机悬停 u_hover@15cm。sysid闭环双点交叉:uh3360→球停11.6/uh3500→停17.8,
                                       * 外推零静差点≈3430;tune_step uh3430→err+0.66→微降到3420。静态增益≈22.7 PWM/cm。
                                       * 注:悬停PWM run-to-run漂±25计数(≈±1cm),是单环锁不住±1cm的根因→串级转速内环治。 */
#define PWM_SLEW_PER_TICK     40      /* [pidcalc≈17,实测取40 验证 std0.89] 每控制周期 PWM 最大变化量。
                                       * 限速=让控制器输出带宽匹配风扇滞后带宽,既滤噪声又不丢控制力。串口 'l' 在线调。 */

/* 闭环运行时的"速度限幅"(占空比下限/上限),区别于物理满量程 PWM_OUTPUT_MIN/MAX。
 *  PWM_RUN_MIN = 怠速地板:闭环运行时风扇绝不低于此值,保持旋转,消除冷启动 ~3s 滞后
 *                与静摩擦复现。设在 lift-off 稍下方(让风扇一直在"做功临界"附近)。
 *  PWM_RUN_MAX = 推力天花板:封住控制器输出上限,防止球被打到顶盖/窜飞。
 * 两者均可串口在线调: nNNNN(min) / xNNNN(max)。E1 升力曲线扫出来后回填。*/
#define PWM_RUN_MIN           3300.0f
#define PWM_RUN_MAX           3668.0f

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
/* [辨识+计算+验证 2026-06-11] 极点配置算出并真机验证的外环增益。
 * sysid 实测 g≈0.333 cm/s²/计数(阶跃对数衰减法);设计 ζ=0.75 ts=2s → wn=2.67。
 *   Kp=wn²/g≈21、Kd=2ζwn/g≈12、f(球速EMA)角频≈5wn→0.76、l≈40、ki 弱积分≈1.1。
 * 真机验证 @15cm: std≈0.89 ptp≈3.2 D std≈3.9(比 kd6/f0.4 的 D std5.4 更干净)。pidcalc.ps1 可重算。
 * 残余 0.13cm/s 慢漂 + 2s振荡(amp~1.3)=悬停点漂移,外环增益治不了→需串级,但当前42Hz同速串级
 *   结构不成立(内环未比外环快,实测越开越差 std0.89→1.6)。冲±1cm 需先把内环解耦到独立高速定时器。 */
#define PID_KP_DEFAULT        21.0f
#define PID_KI_DEFAULT        1.1f
#define PID_KD_DEFAULT        12.0f
#define PID_DERIV_ALPHA       0.76f   /* 球速EMA系数:filt=a*filt_prev+(1-a)*raw。[pidcalc算出=0.76,角频≈5wn]
                                       * 0=裸微分(噪声主导);0.76 真机验证把 D std 5.4→3.9。串口 'f' 在线调。 */
#define PID_ERROR_DEADBAND    0.3f    /* 误差死区±cm */
#define PID_INTEGRAL_LIMIT    200.0f  /* 积分限幅(对应PWM贡献=Ki*此值) */

/* ======================= 轨迹软启动 ======================= */
/* 软目标按此速率(cm/s)从当前球位爬向用户目标,使误差始终小、避免大冲程极限环 */
#define TARGET_RAMP_CM_S      6.0f

/* D项输出钳位: |Kd*球速| 的最大PWM贡献。挡住超声波假跳变(非物理球速)把PWM瞬间打满踹飞球 */
#define D_TERM_CLAMP          3000.0f

/* ======================= 标定(CALIB)界面步长挡位 ======================= */
#define CALIB_STEP_FINE       20      /* 步长：细 */
#define CALIB_STEP_MID        100     /* 步长：中 */
#define CALIB_STEP_COARSE     500     /* 步长：粗 */

/* ======================= 风扇转速测量(tach) ======================= */
/* 四线风扇 tach 线 → 10k上拉3.3V → PC6(TIM3_CH1, AF2)。实测干净3.3V方波。*/
#define TACH_ENABLE           1
#define TACH_PULSES_PER_REV   2       /* 标准四线风扇每转2脉冲 */
#define TACH_TIMEOUT_MS       250u    /* 超此时间无脉冲判为停转 */

/* ======================= 串级内环：转速(RPM)闭环 ======================= */
/* 单环把残余浮动压到 ±1cm 后收益递减，根因是四线风扇的执行器非线性(冷区/静摩擦/
 * 供电下垂/温漂)：同一 PWM 在不同工况下转速并不相同，高度环只能"事后"被动补偿。
 * 串级内环用 tach 实测转速把风机线性化：
 *   外环(高度PID,输出仍是 PWM 等效"推力需求" u)  ->  期望转速 rpm_sp = A*u + B
 *   内环(转速PI)   pwm = u + Kp_rpm*(rpm_sp - rpm_meas) + Ki_rpm*∫(rpm误差)
 * 执行器理想(转速随PWM线性)时 rpm_err≈0、pwm≈u —— 与已验证的单环完全一致，零风险；
 * 执行器发懒/下垂时内环自动多给 PWM 把转速顶回"被要求的速度"，从根上压住浮动。
 * 默认关闭(CASCADE_RPM_DEFAULT=0)，真机用 tools/sweep_rpm.ps1 扫出 A/B 后 'y1' 打开。
 * [标定] A/B = PWM->RPM 线性拟合(sweep_rpm.ps1 输出)。Kp_rpm/Ki_rpm 串口 'yp'/'yi' 在线整定。*/
#define CASCADE_RPM_DEFAULT   0       /* 0=单环(已验证) 1=串级内环(未实测) */
#define RPM_FF_A_DEFAULT      2.0f    /* [辨识实测 2026-06-11 sysid fan] PWM->RPM 斜率: RPM=2.0*PWM-167 (悬停3420→6673,对上实测~6654) */
#define RPM_FF_B_DEFAULT      -167.0f /* [辨识实测] PWM->RPM 截距 (rpm) */
#define RPM_KP_DEFAULT        0.05f   /* 内环比例 (PWM-count / rpm误差) */
#define RPM_KI_DEFAULT        0.0f    /* 内环积分 (PWM-count / (rpm·s)) */
#define RPM_INTEGRAL_LIMIT    2000.0f /* 内环积分限幅(对应最大 PWM 贡献) */

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
#define SCOPE_HMAX_CM         40.0f   /* full-scale height of the vertical axis */
#define SCOPE_SPEC_CM         1.0f    /* ±spec for the error readout color (题目稳态误差 ≤±1cm) */
#define SCOPE_RDH             18      /* top readout strip height (px), inside the box (16px font) */
#define SCOPE_PLOT_Y0         (SCOPE_Y0 + SCOPE_RDH)   /* waveform/grid top (below readout strip) */

/* PWM status strip in the empty arc below the scope box (SCOPE_Y1 = 195) */
#define SCOPE_PWM_Y0          198     /* top y of the PWM strip */
#define SCOPE_PWM_H           34      /* PWM strip height: number row + duty bar */

#endif /* __CONFIG_H */