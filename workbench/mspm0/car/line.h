#ifndef LINE_H
#define LINE_H
/*
 * line.h - 光电循迹算法层（阶梯 5）
 *
 * 定位：**纯算法层，不依赖 HAL** ⇒ 可 PC 单测（pc_test/test_line.c）。它吃"N 个通道的原始读数"，
 *       出"横向偏差 + 转向指令 + 状态"。**它不知道传感器接在哪个脚上、是数字还是模拟** ——
 *       这正是它现在就能写、且能验的原因（硬件未到手，见文件末"接入还差什么"）。
 *
 * ── 为什么把"现场标定"做成一等公民 ──
 *   循迹的真正难点从来不是控制律，是**阈值**：赛场地面反射率、环境光、传感器离地高度全都会变，
 *   一个写死的阈值在自家桌上能跑、到赛场就瞎。所以本模块要求先做两次标定：
 *     line_cal_white()  车放在**白底**上采一次
 *     line_cal_black()  车放在**线上**采一次
 *   之后每个通道各自归一化到 0..1000（白=0，黑=1000），阈值就与绝对反射率无关了。
 *   ⇒ 换场地只需重按两下，不用改代码、不用重烧。（这与陀螺"每次上电做 k"是同一条纪律。）
 *
 * ── 对比度不足要报出来，不许硬算 ──
 *   若某通道的白/黑参考差得太小（CFG_LINE_MIN_CONTRAST），说明它离地太高/坏了/根本没对着地面，
 *   此时归一化出来的就是纯噪声，拿它去转向等于让噪声开车。本模块直接判"未标定/不可用"，
 *   而不是给一个看起来正常的数。**"先证反馈可信，再碰控制器"** —— 超声波时代的血泪。
 *
 * ── 符号约定（与全工程一致，别搞反）──
 *   pos[] 里**左侧为正**；err > 0 表示线在车的左边 ⇒ 需要左转 ⇒ w > 0
 *   （w>0=左转 是 car_drive_mix / nav.c / vservo.h 共同的约定）。
 *
 * 状态: 2026-07-27 新建。PC 单测已过；**真机零验证，且硬件尚未接线**。
 */
#include <stdint.h>

#ifndef LINE_MAX_CH
#define LINE_MAX_CH   8      /* 支持的最大通道数（常见是 5 路或 7 路一字排） */
#endif

typedef enum {
    LINE_OK = 0,      /* 至少一个通道压在线上 -> err/w 有效 */
    LINE_LOST,        /* 全部离线：车已经脱线。w 输出"往最后已知方向搜"的转向 */
    LINE_CROSS,       /* 全部在线：十字路口 / 停止线 / 终点区 -> 交给上层数路口或停车 */
    LINE_NOCAL        /* 没标定，或某通道对比度不足 -> **拒绝输出转向**（w=0） */
} line_state_t;

typedef struct {
    /* --- 配置 --- */
    int   n;                      /* 实际通道数 (1..LINE_MAX_CH) */
    float pos[LINE_MAX_CH];       /* 各通道的横向几何位置，建议单位 mm，**车体中线为 0、左为正** */
    int   ref_w[LINE_MAX_CH];     /* 白(离线)参考读数 */
    int   ref_b[LINE_MAX_CH];     /* 黑(在线)参考读数 */
    int   have_w, have_b;         /* 两次标定都做过了吗 */
    int   on_thresh;              /* 归一化后判"在线"的阈值 (0..1000) */
    int   min_contrast;           /* 白黑参考的最小差值，低于此判该通道不可用 */
    float kp, kd;                 /* 转向 PD：err(mm) -> 差速 RPM */
    float w_max;                  /* 转向上限 RPM */
    float search_w;               /* 丢线时的搜索转向幅值 RPM */

    /* --- 运行态（外部只读）--- */
    int   norm[LINE_MAX_CH];      /* 上一拍的归一化值 0..1000，排障/遥测用 */
    int   on_mask;                /* 上一拍哪些通道在线（bit i = 通道 i） */
    float err;                    /* 上一拍横向偏差（与 pos 同单位，左为正） */
    float last_err;               /* 算 D 项用 */
    float last_dir;               /* 最后一次已知的偏离方向：+1 线在左 / -1 线在右 */
    uint32_t lost_ms;             /* 已连续丢线多久（由 line_step 按 dt 累加） */
} line_t;

/* 初始化。pos 为 n 个通道的横向位置（左为正）；传 NULL 则按等间距 CFG 默认铺开。 */
void line_init(line_t *L, int n, const float *pos);

/* 现场标定：raw 为 n 个通道的当前原始读数。
 *   line_cal_white: 车放在**白底**（全部离线）  line_cal_black: 车放在**线上** */
void line_cal_white(line_t *L, const int *raw);
void line_cal_black(line_t *L, const int *raw);

/* 标定齐全 且 每个通道对比度都够 -> 1。为 0 时 line_step 会回 LINE_NOCAL 并输出 w=0。 */
int  line_calibrated(const line_t *L);

/* 哪个通道对比度不足（返回通道号，全都合格返回 -1）。排障用：告诉人去调哪一个探头的高度。 */
int  line_bad_channel(const line_t *L);

/*
 * 走一拍。raw = n 个通道原始读数；dt_s = 真实经过时间（秒）。
 * 输出 *err_out（横向偏差，可为 NULL）与 *w_out（差速指令 RPM，可为 NULL）。
 * 返回状态见 line_state_t。
 * ⚠ **本函数不输出前进速度 v** —— 那是策略：丢线该慢下来还是停下、路口该不该减速，
 *   取决于题目（数路口 / 见停止线停车 / 一圈不脱线），不该由算法层替上层决定。
 */
line_state_t line_step(line_t *L, const int *raw, float dt_s, float *err_out, int *w_out);

/* 归一化一个通道（供测试/遥测单独调用）：白=0 黑=1000，自动兼容"黑读数更低"的传感器。 */
int line_normalize(const line_t *L, int ch, int raw);

#endif /* LINE_H */

/* ── 接入还差什么（传感器到手后约 30 分钟）───────────────────────────────
 * 1. 决定是**数字**输出（每路一个比较器，直接 GPIO 读 0/1）还是**模拟**输出（进 ADC）。
 *    - 数字：便宜、但阈值固定在硬件电位器上，换场地要拧螺丝 ⇒ 本模块的归一化基本用不上
 *      （raw 只有 0/1000 两个值，仍然能跑，只是失去了"软标定"的好处）。
 *    - 模拟：多占 ADC 通道，但**阈值在软件里、换场地按两下就重标** ⇒ 强烈建议走这条。
 * 2. car.syscfg 加 ADC 通道（注意：ADC0 现在 MEM0/1/2 已被两路电机 + 电磁铁占用，
 *    加通道要同步改 endAdd **和 motor.c 的 ADC_SEQ_N**，否则读到陈旧值）。
 *    引脚要过"核心板引出 ∩ 空闲 ∩ 功能"三重闸，并先写进载板 §10.1 再改 syscfg。
 * 3. car.c 加一个 m11 模式：读 raw -> line_step -> drive_closed_loop(v_cruise, w)。
 *    LOST 超时停车的写法照抄 m10（CFG_VS_LOST_MS 那段）。
 * ⚠ 别忘本仓库那条坑：**新模块必须同时进 makefile 和 syscfg**。
 */
