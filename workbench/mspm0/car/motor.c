/*
 * motor.c - DRV8231 双电机驱动实现 (MSPM0 driverlib, TIMA0 4通道PWM)
 * 引脚由 SysConfig 生成: PWM_MOTOR_INST=TIMA0, CC0/1=M1(PA8/PA9), CC2/3=M2(PB12/PB13)。
 */
#include "ti_msp_dl_config.h"
#include "motor.h"

/* 把占空比(0..PERIOD)写到某个 CC 通道 */
static inline void cc_set(uint32_t idx, uint32_t val)
{
    DL_TimerA_setCaptureCompareValue(PWM_MOTOR_INST, val, idx);
}

void motor_init(void)
{
    /* 先把 4 路都设 0(停),再启动计数器 —— 上电电机不乱动(安全) */
    cc_set(DL_TIMER_CC_0_INDEX, 0);
    cc_set(DL_TIMER_CC_1_INDEX, 0);
    cc_set(DL_TIMER_CC_2_INDEX, 0);
    cc_set(DL_TIMER_CC_3_INDEX, 0);
    DL_TimerA_startCounter(PWM_MOTOR_INST);
}

/* ---- 换向保护（反接冲击抑制）----------------------------------------------------------
 * 🔴 **为什么必须有（2026-08-01 真机数据逼出来的）**
 *   符号-幅值驱动下，占空变号 = `IN1`/`IN2` **整体对调** ⇒ 一个**正在旋转**的电机被立刻
 *   反向驱动。这不是"减速"，是**制动 + 反接**：电枢反电动势与外加电压同向叠加在绕组上，
 *   瞬时电流远大于同占空下的正常驱动，而且是微秒级尖峰（万用表与 10Hz 遥测都看不见）。
 * **实测证据**（`_logs/track/lap_040840.csv`，复位前 1.5s，10Hz 采样已撞上 6 次翻转）：
 *     p1: -10 → +40 → +41 → +29 → -10 → -10 → +33 → +40 → +27 → 0 → 0 → 0 → -10
 *     p2: +51 → +37 → -10 → +49 → +55 → +55 → +44 → -10 → +53 → +54 → +53 → +51 → +55
 *   同期 `v1` 在 1..46rpm 剧烈摆动 ⇒ 转速在 0 附近反复穿越（低速 stick-slip 极限环），
 *   而 `-10` 恰是驱动死区 `dz_drv=10` 的负边界 ⇒ 输出在死区两侧来回跳，每跳一次一次反接。
 *   ⚠ 真实翻转频率远高于此：控制环 5kHz，而遥测只有 10Hz（抓到 1/500）。
 * **它解释了全部现象**：复位总在大幅转向时（内轮指令落进死区边缘）· 悬空恒定占空 60s 零复位
 *   （不穿越死区、无反接）· 失败率 ~47% 而非 100%（取决于该趟有没有跑到那种急弯）·
 *   降 `PWM_CAP` 无效（尖峰幅度由反接决定、与占空上限无关）· 同一二进制既成功又失败（取决于轨迹）。
 * **做法**：检测到符号要翻转时，先强制输出 0（H 桥 coast，`IN1=IN2=0`）若干次调用，让绕组
 *   电流自然衰减，再施加反向占空。这是有刷电机驱动的标准做法。
 * ⚠ **按调用次数计、不按时间**：`motor_set` 在主循环里每次迭代都被调用，而主循环频率是变的
 *   （LCD 全屏重绘能把它拖到 ~100ms/圈）⇒ 时间常数不可靠，次数才与频率无关。
 * ⚠ **代价**：低速极限环时会多出一点滑行，末端定位可能略钝。若发现"转不过弯"，先降这个值。
 * `待真机验证` */
static int8_t   s_last_dir[2] = { 0, 0 };   /* 上次**实际施加**的方向: -1/0/+1 */
static uint8_t  s_coast[2]    = { 0, 0 };   /* 还需强制 coast 几次调用 */

void motor_set(uint8_t ch, int16_t duty)
{
    if (duty > 100)  duty = 100;
    if (duty < -100) duty = -100;

    /* 板级线束朝向(见 config.h §2.1): 让"正占空 => 该侧车轮物理前进"对两侧同时成立。
     * 乘在**最底层**而不是各调用处 —— m5 的 x/y、速度环输出、car_drive_mix 全走这里,
     * 只有在这一点统一才不会漏掉某条路径。2026-07-28 实测右侧 +占空后退 ⇒ 右侧取 -1。 */
    duty = (int16_t)(duty * ((ch == MOTOR_M1) ? CFG_MOT_SIGN_L : CFG_MOT_SIGN_R));

    /* 换向保护（机理与证据见上面那段）。放在朝向符号**之后** ⇒ 判的是**物理方向**翻转，
     * 而不是上层指令的符号（两侧 `CFG_MOT_SIGN_*` 不同构，判上层符号会有一侧判错）。 */
#if CFG_MOT_REV_COAST_N > 0
    {
        int i = (ch == MOTOR_M1) ? 0 : 1;
        int8_t want = (duty > 0) ? 1 : ((duty < 0) ? -1 : 0);
        if (s_coast[i] > 0) {
            s_coast[i]--;
            duty = 0;                       /* 冷却期内一律 coast */
        } else if (want != 0 && s_last_dir[i] != 0 && want != s_last_dir[i]) {
            s_coast[i] = CFG_MOT_REV_COAST_N;   /* 刚要反向 ⇒ 先滑行 N 次调用 */
            duty = 0;
        } else if (want != 0) {
            s_last_dir[i] = want;           /* 只在真正施加了方向时才记住它 */
        }
        /* duty==0 时**不清** s_last_dir: 短暂的 0 之后电机仍在转,
         * `+40 → 0 → -40` 这种序列同样需要保护。 */
    }
#endif

    /* 占空幅值 -> CC 计数 */
    uint32_t mag = (uint32_t)((duty < 0) ? -duty : duty) * MOTOR_PWM_PERIOD / 100u;
    if (mag > MOTOR_PWM_PERIOD) mag = MOTOR_PWM_PERIOD;

    uint32_t in1, in2;
    if (duty >= 0) { in1 = mag; in2 = 0;   }   /* 正转: IN1=PWM, IN2=0 */
    else           { in1 = 0;   in2 = mag; }   /* 反转: IN1=0, IN2=PWM */

    if (ch == MOTOR_M1) {
        cc_set(DL_TIMER_CC_0_INDEX, in1);      /* M1 IN1 = PA8 */
        cc_set(DL_TIMER_CC_1_INDEX, in2);      /* M1 IN2 = PA9 */
    } else {
        cc_set(DL_TIMER_CC_2_INDEX, in1);      /* M2 IN1 = PB12 */
        cc_set(DL_TIMER_CC_3_INDEX, in2);      /* M2 IN2 = PB13 */
    }
}

void motor_stop_all(void)
{
    motor_set(MOTOR_M1, 0);
    motor_set(MOTOR_M2, 0);
}

/* ---- DRV8231 电流采样 (ADC0 序列: MEM0=PA27, MEM1=PA26) ----
 * ⚠ **两块板的 IPROPI 映射是相反的, 别再"统一"成一个值** (真值同时登记在 SSOT §B):
 *   · 老飞线板(2026-07-24 实测): M1←MEM1(PA26)、M2←MEM0(PA27) —— 与 syscfg 假设相反, 属手工飞线接错。
 *   · **新打板载板(2026-07-28 实测, 现役)**: 回到载板设计 §10.1 的设计值 —— **M1←MEM0(PA27)、M2←MEM1(PA26)**。
 *     判据: 只驱 M1 时电流出现在原先报作 I2 的那一路(65→85mA), 只驱 M2 时在 I1(60→84mA), 15%/30% 四组一致。
 *   ⇒ 下面按**新板/设计值**取值。换回老飞线板要把 s0/s1 换回来。
 *   复验法: 单路给占空, 看哪个 ADC 通道跟着动(别信注释, 一分钟能测)。 */
/* 多次采样取平均: 电机 PWM 斩波使 IPROPI 电流呈脉冲(导通相高/续流相~0), 单次异步采样
 * 会随机抓在脉冲任意点 → 读数在 0~峰值间狂跳(实测 t150 时 I 抖 0~627mA)。连采 N 次
 * (跨多个 PWM 周期)取平均, 把斩波纹波平掉, 电流环才拿得到干净反馈。
 * 采样次数 CUR_AVG_N 及其取值依据见 config.h(要更快需上"ADC 触发同步 PWM 定相采样")。 */
/* 序列里的通道数, 必须与 car.syscfg 的 endAdd+1 一致(现在 MEM0/1/2 = M2电流/M1电流/电磁铁电流)。
 * ⚠ **加通道必须同时改这里**: 等待时间不够时 getMemResult 读到的是上一轮的陈旧值 ——
 *   这种错误不报警、不崩、只是数据慢半拍或干脆是别的通道的旧数, 极难从现象看出来。
 * 每通道 sampleTime0=40us(≈1280 周期 @32MHz), 按 1600 周期/通道留余量, 再加 800 周期的固定开销。 */
#define ADC_SEQ_N        3
#define ADC_WAIT_CYCLES  (ADC_SEQ_N * 1600u + 800u)

/* 一次触发, 取回整条序列的三路平均值。任一指针可为 NULL(不需要就别要)。
 * 为什么三路必须在**同一个函数**里取: 序列是一次触发全部转换, 若电机与电磁铁各自去 startConversion,
 * 两边会互相打断对方的序列 —— ADC 只有一个, 触发点也只能有一个。 */
void motor_adc_read_all(uint16_t *m1_raw, uint16_t *m2_raw, uint16_t *mag_raw)
{
    uint32_t s0 = 0, s1 = 0, s2 = 0;
    DL_ADC12_enableConversions(ADC_CUR_INST);
    for (int i = 0; i < CUR_AVG_N; i++) {
        DL_ADC12_startConversion(ADC_CUR_INST);
        delay_cycles(ADC_WAIT_CYCLES);
        s0 += DL_ADC12_getMemResult(ADC_CUR_INST, DL_ADC12_MEM_IDX_0);   /* PA27 */
        s1 += DL_ADC12_getMemResult(ADC_CUR_INST, DL_ADC12_MEM_IDX_1);   /* PA26 */
        s2 += DL_ADC12_getMemResult(ADC_CUR_INST, DL_ADC12_MEM_IDX_2);   /* PA24 = 电磁铁 */
    }
    if (m1_raw)  *m1_raw  = (uint16_t)(s0 / CUR_AVG_N);   /* M1 = MEM0(PA27) —— 新板实测/设计值 */
    if (m2_raw)  *m2_raw  = (uint16_t)(s1 / CUR_AVG_N);   /* M2 = MEM1(PA26) —— 新板实测/设计值 */
    if (mag_raw) *mag_raw = (uint16_t)(s2 / CUR_AVG_N);   /* 电磁铁 = MEM2(PA24) // 待真机验证 */
}

void motor_read_current_raw(uint16_t *m1_raw, uint16_t *m2_raw)
{
    motor_adc_read_all(m1_raw, m2_raw, 0);
}

int32_t motor_current_ma(uint16_t raw)
{
    /* V=raw/4096*3300mV; I(mA)=V/(1.575mA/A × 0.680kΩ) -> raw*3300/CUR_MA_DIV。
     * 除数 CUR_MA_DIV 及其定标依据(待实测校准)见 config.h。 */
    return (int32_t)((uint32_t)raw * 3300u / CUR_MA_DIV);
}
