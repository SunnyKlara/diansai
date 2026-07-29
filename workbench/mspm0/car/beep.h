#ifndef BEEP_H
#define BEEP_H
/*
 * beep.h —— 蜂鸣器非阻塞状态机（声光提示 / 计时听觉锚点）
 *
 * 定位: **纯算法层, 不依赖 HAL**。只吃 now_ms -> 出"这一拍蜂鸣器该是高还是低"。
 *       car.c 拿这个 0/1 去写 GPIO。可 PC 单测(pc_test/test_beep.c)。
 *
 * ── 为什么要单独一个状态机, 不直接 delay ──
 *   "嘀嘀"两声若用忙等实现 = 把控制环停掉 200ms。而这 200ms 里编码器采样、速度环、
 *   球控制全停 —— 车会窜、球会跑。本仓库已有同类血泪(LCD 重绘拖慢主循环 ⇒ RPM 虚高 5x)。
 *   ⇒ 蜂鸣器**必须**是"到期时刻 + 剩余次数"的查询式状态机。
 *
 * ── 三个设计判断 ──
 *  1. **覆盖式请求**: 新请求直接打断旧的。理由: ABORT 的长鸣比 DONE 的双响更该被听到,
 *     排优先级还要多一套规则, 而"后来的更重要"在本题里恰好总成立(异常总在完成之后发生)。
 *  2. **结束必须回 0**。听着像废话, 但"图案跑完忘了关"是这类状态机最常见的 bug,
 *     现场表现是蜂鸣器一直叫、且盖过评委说话。单测里专门跑 2 秒确认恒 0。
 *  3. 时刻比较用**差值判号**, 32 位毫秒回绕(49.7 天)不出错 —— 与 task.c 同一写法。
 *
 * 无源蜂鸣器注意: 本模块出的是"响/不响", 不是方波。无源蜂鸣器要接 PWM 通道,
 *   把本模块的 1/0 当作"PWM 使能/禁止"即可(别拿它当 PWM 波形本身)。
 *
 * 状态: 2026-07-29 新建。**PC 单测已过; 真机零验证**(蜂鸣器未接线)。
 */
#include <stdint.h>
#include "config.h"     /* 图案时长的调参落点见 config.h §7.11 */

/* 图案时长(config.h §7.11 若已 define 则以它为准) */
#ifndef BEEP_SHORT_MS
#define BEEP_SHORT_MS    80u    /* 短鸣: 起跑 */
#endif
#ifndef BEEP_GAP_MS
#define BEEP_GAP_MS     100u    /* 双响之间的间隔 */
#endif
#ifndef BEEP_LONG_MS
#define BEEP_LONG_MS    600u    /* 长鸣: 异常停 */
#endif

typedef enum {
    BEEP_NONE = 0,
    BEEP_SHORT,     /* 短一声   —— 起跑（给评委听觉锚点，也方便录像对时） */
    BEEP_DOUBLE,    /* 两短声   —— 完成 */
    BEEP_LONG       /* 长一声   —— 异常停 */
} beep_pat_t;

typedef struct {
    uint8_t  level;      /* 当前应输出的电平 0/1（外部只读，或用 beep_step 的返回值） */
    uint8_t  pulses;     /* 还剩几个"响"没发完 */
    uint8_t  on_phase;   /* 1 = 正在响，0 = 正在间隔 */
    uint32_t t_next;     /* 当前相位的到期时刻 */
    uint32_t on_ms;      /* 本图案每次响多久 */
    uint32_t gap_ms;     /* 本图案间隔多久 */
} beep_t;

void beep_init(beep_t *B);

/* 请求一个图案（覆盖式）。pat = BEEP_NONE 等价于立即静音。 */
void beep_req(beep_t *B, beep_pat_t pat, uint32_t now_ms);

/* 每拍调一次，返回蜂鸣器应输出的电平 0/1。 */
int  beep_step(beep_t *B, uint32_t now_ms);

/* 1 = 图案还没放完（排障/避免刷屏时被打断用；正常不需要判它）。 */
int  beep_busy(const beep_t *B);

#endif /* BEEP_H */
