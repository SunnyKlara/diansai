#ifndef TASK_H
#define TASK_H
/*
 * task.h —— 2026-H 第 2 项的任务层：按键启动 · 实时计时 · 启停线到达判定 · 安全停
 *
 * 定位: **纯算法层, 不依赖 HAL/driverlib/newlib**。只吃 (now_ms, 按键电平, 循迹状态, 里程)
 *       -> 出 (状态, 走时, 速度档, 蜂鸣请求, 该不该刷屏)。因此可在 PC 上 gcc 编译并单元测试
 *       (见 pc_test/test_task.c), 与 line.c / nav.c / ball.c 同一条分层纪律。
 *
 * ── 为什么需要这一层(不是"顺手加个按键") ──
 *   题目说明 5 明文: "小车**必须有启动按键和显示装置**, **按键启动时计时系统开始计时并显示时间**"。
 *   审题 §3.2 把它读成隐含约束: **运行中屏幕要实时走时**, 不是只在结束显示总时间;
 *   停车时**计时停止并显示总时间**。⇒ 这是第 2 项 16 分里独立的判分件, 和循迹本身无关。
 *   而 line.c 有意**不**输出前进速度 v(见 line.h 注释: "丢线该慢下来还是停下、路口该不该减速…
 *   不该由算法层替上层决定") ⇒ 那个"上层"就是本文件。
 *
 * ── 五个设计判断(都有依据, 别随手改) ──
 *  1. **计时在"车停住"时停, 不在"检到启停线"时停**。评委按秒表的时机是车停下来那一刻,
 *     内部计时与它同口径, 自查才有意义(Q47 已明确"屏显信息仅为参考"⇒ 内部计时只用于自查)。
 *  2. **必须有起步屏蔽**: 小车起点就在 A 点、启停线就在轮子底下 ⇒ 按键那一瞬 cross 就是真,
 *     不屏蔽会"一启动立刻判定到终点然后停住"。这是本层最容易在赛场当场翻车的一条。
 *  3. **到达判定是双闸门**: 里程 >= 一圈的 TASK_LAP_MIN_MM **且** cross。单靠 cross 太脆
 *     (启停线只 5cm 长、阵列约 8cm ⇒ 只有中间几路变黑, 判据本身就不锐利);
 *     单靠里程会被打滑骗(横向 3~4% 那笔账已实测)。两个都要, 才是"到了"。
 *  4. **失败要有名字**(同 uart_frame.h/nav.c 的规矩): TIMEOUT / LOST / MANUAL 分开计,
 *     现场不用猜"它为什么停了"。
 *  5. **本层只出速度档(STOP/CRUISE/SLOW), 不出 rpm**。具体转速是整定值, 归 config.h/car.c。
 *
 * 状态: 2026-07-29 新建。**PC 单测已过; 真机零验证**(按键/蜂鸣器引脚未接、未进 car.c)。
 */
#include <stdint.h>
#include "config.h"     /* 调参唯一落点(§7.11)。config.h 是纯 #define、不含 HAL ⇒ PC 单测照样能编 */

/* ── 可调参数(config.h §7.11 若已 define 则以它为准, 同 uart_frame.h/linesens.h 的做法) ── */
#ifndef TASK_BTN_DEBOUNCE_MS
#define TASK_BTN_DEBOUNCE_MS   20u    /* 电平须连续稳定这么久才被采纳 */
#endif
#ifndef TASK_ARM_BLIND_MM
#define TASK_ARM_BLIND_MM     300.0f  /* 起步后这么多毫米内忽略 cross(见设计判断 2) */
#endif
#ifndef TASK_LAP_MIN_MM
#define TASK_LAP_MIN_MM      5200.0f  /* 到达闸门: 一圈 6141.6mm 的 ~85%。
                                       * 取 85% 而不是 100%: 里程本身有 ±0.5% 误差 + 循迹走的是
                                       * 曲线内外侧、实走里程与理论周长不等 ⇒ 门限要留余量,
                                       * 反正另一半闸门(cross)保证了位置。 */
#endif
#ifndef TASK_SLOW_MM
#define TASK_SLOW_MM         5800.0f  /* 走到这里开始预降速: 刹车距离可控 + 启停线采样更密 */
#endif
#ifndef TASK_MAX_MS
#define TASK_MAX_MS         40000u    /* 整趟硬超时 -> ABORT。防"没检到启停线一直转圈" */
#endif
#ifndef TASK_LOST_STOP_MS
#define TASK_LOST_STOP_MS    1200u    /* 连续丢线这么久 -> ABORT(投影脱线已成事实, 别再跑) */
#endif
#ifndef TASK_BRAKE_MS
#define TASK_BRAKE_MS         600u    /* 进 BRAKE 后最多等这么久就判"停住了"(兜底) */
#endif
#ifndef TASK_DISP_MS
#define TASK_DISP_MS          100u    /* 走时刷屏周期。说明 5 要求运行中实时显示 */
#endif

/* ── 状态 ─────────────────────────────────────────────────────────────── */
typedef enum {
    TASK_IDLE = 0,   /* 等按键。屏显 READY */
    TASK_RUN,        /* 计时中 + 循迹跑 */
    TASK_BRAKE,      /* 已判定到达, 正在停(计时**仍在走**, 见设计判断 1) */
    TASK_DONE,       /* 停住了, 走时冻结 */
    TASK_ABORT       /* 异常安全停, fail 里有原因 */
} task_state_t;

typedef enum {
    TASK_FAIL_NONE = 0,
    TASK_FAIL_TIMEOUT,   /* 超过 TASK_MAX_MS 还没到 */
    TASK_FAIL_LOST,      /* 连续丢线超过 TASK_LOST_STOP_MS */
    TASK_FAIL_MANUAL     /* 外部急停(命令 z / 再按一次按键) */
} task_fail_t;

typedef enum { TASK_V_STOP = 0, TASK_V_CRUISE, TASK_V_SLOW } task_vmode_t;

typedef enum {
    TASK_BEEP_NONE = 0,
    TASK_BEEP_START,     /* 短一声: 起跑(给评委一个听觉锚点, 也方便录像对时) */
    TASK_BEEP_DONE,      /* 两声: 完成 */
    TASK_BEEP_ABORT      /* 长一声: 异常停 */
} task_beep_t;

/* ── 输入 ─────────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t now_ms;      /* 毫秒时钟(MCU 上 = g_st / ST_PER_MS) */
    int      btn;         /* 按键**已归一化**的逻辑电平: 1 = 按下。低有效的硬件在 car.c 里取反 */
    int      line_lost;   /* 1 = 循迹判定丢线(全白)。由 line.c 的状态映射 */
    int      line_cross;  /* 1 = 循迹判定 cross(全/多路在线 = 启停线) */
    float    dist_mm;     /* **本趟**已走里程(mm)。car.c 在起跑时清零, 由编码器积分 */
    int      stopped;     /* 1 = 车速已归零(编码器增量 ~0)。没有可信来源时恒传 0, 靠 TASK_BRAKE_MS 兜底 */
} task_in_t;

/* ── 输出 ─────────────────────────────────────────────────────────────── */
typedef struct {
    task_state_t state;
    uint32_t     elapsed_ms;  /* 走时。RUN/BRAKE 中在涨; DONE/ABORT 后冻结 */
    task_vmode_t v_mode;
    task_beep_t  beep;        /* **只在发生的那一拍非 NONE**(边沿语义, 上层不用去重) */
    int          disp_dirty;  /* 1 = 这一拍该刷屏 */
    task_fail_t  fail;
} task_out_t;

/* ── 内部状态 ─────────────────────────────────────────────────────────── */
typedef struct {
    task_state_t state;
    task_fail_t  fail;

    /* 按键消抖: 只认"稳定电平的上升沿", 且必须先稳定松开过一次才允许再次触发
     * (防长按连触发, 也防接触抖动把一次按压变成两次) */
    int      btn_stable;      /* 已被采纳的稳定电平 */
    int      btn_last_raw;    /* 上一拍原始电平 */
    uint32_t btn_raw_at;      /* 原始电平上次变化的时刻 */
    int      btn_armed;       /* 1 = 允许下一次上升沿触发(松开后置 1) */

    uint32_t t_start;         /* 起跑时刻 */
    uint32_t t_stop;          /* 停住时刻(DONE/ABORT) */
    uint32_t t_brake;         /* 进 BRAKE 的时刻 */
    uint32_t t_lost;          /* 本段连续丢线的起始时刻; 0 = 当前不在丢线 */
    uint32_t t_disp;          /* 上次置 disp_dirty 的时刻 */
    float    d0;              /* 起跑时的里程读数(允许上层不清零, 本层自己做差) */
    uint32_t n_runs;          /* 跑过几趟(便于录像/日志对号) */
} task_t;

/* ── API ──────────────────────────────────────────────────────────────── */
void task_init(task_t *T);

/* 每个控制拍调一次。out 必须非 NULL。beep 是**边沿**语义(只在那一拍给出)。 */
void task_step(task_t *T, const task_in_t *in, task_out_t *out);

/* 外部急停(命令 z / 保护触发) —— 与内部异常走同一条收尾路径, 避免"急停逻辑被复制两份然后分叉"
 * (这是校赛B 的血泪: 见 car.c 的 stop_all 注释)。 */
void task_abort(task_t *T, uint32_t now_ms, task_fail_t why);

/* 把毫秒格式成给评委看的走时: <60s -> "12.3"; >=60s -> "1:02.3"。返回写入长度。
 * buf 至少 12 字节。**不用 snprintf**(算法层不依赖 newlib, 且 MCU 上 printf 家族又大又慢)。 */
int  task_fmt_time(uint32_t ms, char *buf, int n);

/*
 * ── 接进 car.c 的最小改动(等 car.c 放行后照这个贴, 4 处) ─────────────────
 *
 * 1) car.syscfg 加两个 GPIO(引脚**建议值, 待 syscfg 核**; 避开 SSOT §B 黑名单
 *    PA3/4/5/6 晶振、PA19/20 SWD, 以及已占用的那些):
 *       BTN_START = PA23  输入 + **内部上拉**, 按键另一端接 GND ⇒ 按下读 0
 *       BUZZER    = PB1   推挽输出(有源蜂鸣器直接拉高即响; 无源的要给 PWM)
 *
 * 2) car.c 顶部:  #include "task.h"     static task_t g_task;
 *    init 里:     task_init(&g_task);
 *
 * 3) 主循环(与 LCD 同一节拍即可, 100Hz 足够):
 *       task_in_t  ti;  task_out_t to;
 *       ti.now_ms    = g_st / ST_PER_MS;
 *       ti.btn       = !DL_GPIO_readPins(GPIO_BTN_PORT, GPIO_BTN_START_PIN);  // 低有效 -> 取反
 *       ti.line_lost = (ls == LINE_LOST);
 *       ti.line_cross= (ls == LINE_CROSS);
 *       ti.dist_mm   = (float)((g_cnt1 + g_cnt2) / 2) / CFG_ENC_COUNTS_PER_MM;
 *       ti.stopped   = (abs(d_cnt1) + abs(d_cnt2) < 4);
 *       task_step(&g_task, &ti, &to);
 *       // 速度档 -> 具体 rpm(整定值留在 config.h)
 *       switch (to.v_mode) {
 *         case TASK_V_STOP:   car_drive_mix(0.0f, 0.0f);                    break;
 *         case TASK_V_CRUISE: car_drive_mix(CFG_TASK_V_CRUISE, w_line);      break;
 *         case TASK_V_SLOW:   car_drive_mix(CFG_TASK_V_SLOW,   w_line);      break;
 *       }
 *       if (to.beep != TASK_BEEP_NONE) buzzer_req(to.beep);   // 非阻塞, 别用 delay
 *       if (to.disp_dirty)             disp_run_page(&to);
 *
 * 4) 命令 'z' 里加一句: task_abort(&g_task, g_st / ST_PER_MS, TASK_FAIL_MANUAL);
 *
 * ⚠ 蜂鸣器**不能用忙等实现**("嘀嘀"两声若用 delay 会把控制环停掉 200ms) ⇒ car.c 里做成
 *   一个"到期时刻 + 剩余次数"的小状态机, 每拍查一次。
 */

#endif /* TASK_H */
