/*
 * encoder.c - 双编码器【定时采样 正交解码】(不用边沿中断)
 *   enc1: A=PA7  B=PB19      enc2: A=PB20 B=PB21
 *   encoder_poll() 每被调用一次就采一次 A/B, 用 4x 正交状态机更新计数。
 *   由主循环/定时器以固定速率(本工程 ~1kHz 主循环 tick)调用。
 *
 * 为什么不用边沿中断: 电机在环时 A/B 线拾到大量 EMI 快毛刺, 边沿中断会把每个毛刺
 *   都计一次(实测 I 是采样计数的~50倍、且中断吃满 CPU/拖慢主循环)。定时采样只在固定
 *   时刻读电平, 采样间隔内的快毛刺自然被丢弃 → 对 EMI 免疫、CPU 零中断负载。
 *   (2026-07-24 真机: 1kHz 采样的软件计数干净, 边沿中断计数被 EMI 放大50x, 见调试日志)
 * 局限: 可靠跟踪的正交跳变率 ≲ 采样率的一半(~500/s @1kHz)。当前 40% PWM 下真实率仅
 *   ~10~60/s, 余量极大; 若日后全速+高线数丢计数, 升级为专用定时器(5~10kHz)采样或硬件 QEI。
 */
#include "ti_msp_dl_config.h"
#include "encoder.h"

static volatile int32_t g_cnt[2] = {0, 0};
static uint8_t g_state[2] = {0, 0};   /* 上次 (A<<1)|B 状态, 2 bit */

/* 4x 正交解码增量表: 下标 = (旧状态<<2)|新状态; 合法单步跳变 ->+1/-1, 非法(跳2格/无变化)->0
 * 符号约定: 已翻转, 使【正 PWM(电机正转) => 计数/转速为正】。
 * (2026-07-24 真机: 原符号下 正PWM 读到负速度 -> 速度环正反馈发散到饱和; 翻转 QDEC 各项修正。
 *  两路电机在本测试同 PWM 同向、编码器都读反, 故统一翻转即可; 若日后差速需左右异号, 在 car_drive 层处理。) */
static const int8_t QDEC[16] = {
     0, +1, -1,  0,
    -1,  0,  0, +1,
    +1,  0,  0, -1,
     0, -1, +1,  0
};

static uint8_t read_state(uint8_t ch)
{
    if (ch == ENC_1) {
        uint8_t a = DL_GPIO_readPins(GPIOA, GPIO_ENC_ENC1_A_PIN) ? 1u : 0u;   /* PA7  */
        uint8_t b = DL_GPIO_readPins(GPIOB, GPIO_ENC_ENC1_B_PIN) ? 1u : 0u;   /* PB19 */
        return (uint8_t)((a << 1) | b);
    } else {
        uint8_t a = DL_GPIO_readPins(GPIOB, GPIO_ENC_ENC2_A_PIN) ? 1u : 0u;   /* PB20 */
        uint8_t b = DL_GPIO_readPins(GPIOB, GPIO_ENC_ENC2_B_PIN) ? 1u : 0u;   /* PB21 */
        return (uint8_t)((a << 1) | b);
    }
}

void encoder_init(void)
{
    /* 采样式解码: 不使能 GPIO 中断(NVIC), 从根上避免 EMI 毛刺造成的中断风暴。
     * 初始化当前状态, 避免首次 poll 误判一次跳变。 */
    g_cnt[ENC_1] = 0;
    g_cnt[ENC_2] = 0;
    g_state[ENC_1] = read_state(ENC_1);
    g_state[ENC_2] = read_state(ENC_2);
}

/* 采一次 A/B、按正交状态机累加计数。须以固定速率周期性调用(主循环每 tick 调用即可)。 */
void encoder_poll(void)
{
    uint8_t s1 = read_state(ENC_1);
    g_cnt[ENC_1] += QDEC[(g_state[ENC_1] << 2) | s1];
    g_state[ENC_1] = s1;

    uint8_t s2 = read_state(ENC_2);
    g_cnt[ENC_2] += QDEC[(g_state[ENC_2] << 2) | s2];
    g_state[ENC_2] = s2;
}

int32_t encoder_count(uint8_t ch) { return g_cnt[ch & 1]; }
void    encoder_reset(uint8_t ch) { g_cnt[ch & 1] = 0; g_state[ch & 1] = read_state(ch & 1); }
