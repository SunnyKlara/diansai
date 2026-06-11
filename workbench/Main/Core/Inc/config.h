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
#define FAN_PREHEAT_PWM       3300.0f  /* 预热暖机PWM:让风机越过静摩擦/冷启动先转起来,但低于悬停(~5000转<6600转)球不起;预热后由mode2起飞boost从底干净冲到目标(吹起球的高预热会致稳态偏高+漂) */

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

/* ToF geometry. height_cm = TOF_ZERO_CM - TOF_SCALE * raw_cm.
 * [CAL 2026-06-12 2-point static least-squares] H = 47.81 - 1.0331 * raw.
 * Points (ball-bottom-to-tube-bottom, static clamped): 10cm->raw36.6, 35cm->raw12.4.
 * Verify @20cm: raw27.1 -> 19.8 (within -0.2cm). Residuals over 10/20/35cm all +/-0.5cm.
 * The slope (1.0331) corrects a real tilt/scale drift that a single zero could not
 * fix (low points read slightly high, high points read low). Rest raw~47 -> H~0.
 * Runtime trim: 'o<zero>' / 'os<scale>'. Re-run 2-pt static cal if rig is bumped. */
/* HOVER-WORKING-POINT calibration [2026-06-12]: the 2-pt STATIC cal above is
 * geometrically correct for a clamped ball, but the SCORING condition is the
 * HOVERING ball. Measured (30s mean, ruler mid-point): hovering display reads
 * ~3cm LOW vs the real ball-bottom (laser is slightly off the tube axis, so a
 * centered hovering ball is hit off-normal -> longer slant range -> H too low).
 * Verified at target 15: with +3 the ball really sits at 15 (display=target=ruler).
 * So the runtime zero carries static-intercept 47.81 + 3.0 hover offset = 50.81.
 * This makes current_height == true ball height in flight, which satisfies BOTH
 * scoring items at once: LCD-vs-ruler (measurement) AND ball-vs-target (control).
 * Re-measure the 3.0 if the laser/rig is re-aligned; trim live with 'o'. */
#define TOF_ZERO_CM           50.81f  /* 47.81 static geom + 3.0 hover offset (see note) */
#define TOF_SCALE_DEFAULT     1.0331f /* 2-pt static cal slope; runtime 'os' to re-trim */
#define TOF_HEIGHT_OFFSET_CM  0.0f    /* legacy, unused (geometry now zero+scale) */

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
#define PWM_RUN_MIN           3800.0f /* [达标mode2] 怠速地板抬到串级工作区下沿 */
#define PWM_RUN_MAX           4100.0f /* [达标mode2] 推力上限,悬停PWM~3960在区间内 */

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
/* [辨识+计算+三高度验证 2026-06-11] 通吃 10/15/20cm 的保守基线。
 * pidcalc 算出 kp21/kd12(ζ0.75 ts2s, g0.333@15cm),但实测对象增益随高度变(20cm更陡):
 *   kp21/kd12 在 10/15cm std0.94 好,到 20cm 极限环 std2.82(PWM两轨bang-bang)。
 * 降到 kp12/kd7 后三高度全稳:10cm std0.73 / 15cm std0.87 / 20cm std0.97,均±2cm内,无需增益调度。
 * 即"略低于极点配置值的保守增益"对 g 的高度变化更鲁棒。冲±1cm 仍需内环高速定时器串级。 */
#define PID_KP_DEFAULT        8.0f    /* [达标2026-06-12 串级mode2开机默认配方] 外环高度PID */
#define PID_KI_DEFAULT        0.6f
#define PID_KD_DEFAULT        12.0f
#define PID_DERIV_ALPHA       0.40f   /* 球速EMA系数:mode2用观测器速度,此为备用路径。[达标用0.40]
                                       * 0=裸微分(噪声主导);0.76 真机验证把 D std 5.4→3.9。串口 'f' 在线调。 */
#define PID_ERROR_DEADBAND    0.6f    /* [达标2026-06-12] 误差死区±cm。d0.6掐断低频极限环又不留稳态误差;'d'在线调 */
#define PID_INTEGRAL_LIMIT    200.0f  /* 积分限幅(对应PWM贡献=Ki*此值) */

/* ======================= 速度观测器(α-β滤波,冲±1cm) ======================= */
/* 激光逐样本跳 ~0.3cm → 裸差分速度被放大成 ±12cm/s 噪声 → 逼出重EMA滤波 → 滞后 → 极限环。
 * α-β 观测器用"匀速模型 + 测量残差"算干净速度,滞后远小于 EMA。
 *   预测: x+=v*dt ; 校正: r=z-x; x+=α*r; v+=(β/dt)*r
 * α 小=信测量少更平滑,β 小=速度更平滑但更滞后。串口 'va'/'vb' 在线调。
 * g_use_obs=1 时 PID 的 D 项用观测器速度(默认1);=0 退回旧 EMA 行为。'vo' 切换。*/
#define OBS_ALPHA_DEFAULT     0.20f   /* [达标2026-06-12] va0.2 */
#define OBS_BETA_DEFAULT      0.05f
#define OBS_USE_DEFAULT       1

/* ======================= LADRC 自抗扰(冲±1cm+抗扰) ======================= */
/* 对象 y''=b0*u+f。ESO 估 z1≈y,z2≈y',z3≈f(总扰动:悬停漂+g(h)+外扰+未建模),控制律减掉 z3。
 * 详见 04_调试记录/ADRC自抗扰控制方案与实施.md。默认关(走已验证PID),串口 Z1/Z0 切换。
 * b0=sysid实测g;Kp=ωc²,Kd=2ωc;ESO增益 b1=3ωo,b2=3ωo²,b3=ωo³。串口 b0/wc/wo 在线调。*/
#define CTRL_MODE_DEFAULT     2       /* [达标2026-06-12] 开机即串级,评委按键一键用达标配方。0=PWM-PID 1=LADRC 2=RPM串级 */
#define ADRC_B0_DEFAULT       0.333f  /* [sysid实测] 输入增益 g (cm/s²/计数);ADRC对其不敏感 */
#define ADRC_WC_DEFAULT       3.0f    /* 控制器带宽 ωc (rad/s);Kp=ωc² Kd=2ωc */
#define ADRC_WO_DEFAULT       8.0f    /* 观测器带宽 ωo (rad/s);主调旋钮。实测:12噪声爆/5太慢冲顶,8折中(配TD) */
#define ADRC_WT_DEFAULT       1.8f    /* 跟踪微分器带宽 ωt (rad/s);把目标做平滑爬升,消起浮阶跃冲击。小=慢而稳 */

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
#define CASCADE_RPM_DEFAULT   1       /* [达标2026-06-12] 开机即开串级内环(500Hz转速环);'y0'可关 */
/* === CTRL_MODE=2 转速串级 8.87V 实测整定值 (2026-06-12, 500Hz内环ISR) ===
 * [达标 2026-06-12] CTRL_MODE=2 串级 @15cm: std=0.51-0.59, ±1cm内80-98%, ±2cm内100%, 居中。
 * 关键转折: (1)转速反馈EMA滤波(yf,g_rpm_alpha)消tach抖动→内环不再帮倒忙;
 *   (2)hover_rpm必须=真实悬停转速(由mode-0实测PWM反推:3960PWM→6700RPM区,设6600);
 *   (3)kd加大(18)在滤波后干净地提供阻尼; (4)起飞boost自恢复防卡底。
 * 完整配方(Z2前下发,kp/kd/ki与mode0共用全局不能进PID_*_DEFAULT):
 *   h6600 ya2.53 yb-3315 n3800 x4100 kp12 ki1.5 kd18 f0.4 d0 yp0.2 yi0.5 yf0.8 va0.2 vb0.05
 * 注:供电=12V电池经降压模块给风扇8.87V(稳压);PWM->RPM映射随供电方式变,换硬件需 find_hover 重标。 */
/* 串级内环高速定时器频率(Hz)。CTRL_MODE=2 时内环PI移入 TIM4 ISR 以此频率运行,
 * 与外环高度环(ToF事件驱动~42Hz)解耦,实现教科书要求的"内环>=5x外环"带宽分离。
 * 这是单环卡在std~1.0、yp一开就更差的结构根因的修复(见调试日志2026-06-11结论)。
 * 500Hz(>10x外环) + tach转速反馈 -> 对电池放电掉压免疫(锁RPM不锁PWM)。*/
#define RPM_INNER_HZ          500u
#define HOVER_RPM_DEFAULT     6600.0f /* [达标2026-06-12] 15cm悬停转速。由mode-0实测悬停PWM~3960反推(2.53*3960-3315≈6700区),
                                       * 取6600居中。注:之前误设6590/6250是错的(混乱标定),导致串级托不住球。'h'在线调 */
#define RPM_FF_A_DEFAULT      2.53f   /* [实测2026-06-12 find_hover 当前供电] PWM->RPM 斜率: RPM=2.53*PWM-3315 */
#define RPM_FF_B_DEFAULT      -3315.0f/* [实测] PWM->RPM 截距 (rpm)。随供电方式变,换硬件重标 */
#define RPM_KP_DEFAULT        0.2f    /* [达标] 内环比例(yp) */
#define RPM_KI_DEFAULT        0.5f    /* [达标] 内环积分(yi) */
#define RPM_INTEGRAL_LIMIT    2000.0f /* 内环积分限幅(对应最大 PWM 贡献) */

/* 串级起飞 boost(CTRL_MODE=2):气动悬崖+迟滞使"从静止起飞"需 RPM 远高于"维持飞行"。
 * 单一 hover_rpm+小kp 无法兼顾:球一旦坠底,外环命令的 hover+kp*e 低于离地阈值→永远卡底。
 * 解法:球在底(<ENTER)时直接给 hover+BOOST 的高转速把球吹起,升过 EXIT 后交还悬停控制;
 * 若飞行中坠回底部会自动重新 boost(自恢复,杜绝"卡底")。BOOST 需使总转速超过离地阈值。*/
#define CASCADE_LAUNCH_BOOST   600.0f  /* 起飞时叠加在 hover_rpm 上的 RPM(超过离地阈值) */
#define CASCADE_LAUNCH_ENTER   3.0f    /* 高度<此值 → (重新)进入起飞 boost */
#define CASCADE_LAUNCH_EXIT    10.0f   /* 高度>此值 → 退出 boost,交还悬停控制 */

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