#ifndef DISP_RUN_H
#define DISP_RUN_H
/*
 * disp_run.h —— LCD「RUN 页」的纯排版层（第 2 项的显示件 + 赛中唯一观测通道）
 *
 * 定位: **纯算法层, 不碰 GC9A01**。吃一组数值 -> 出 6 行**已格式化好的短字符串** +
 *       一个"哪几行变了"的位掩码。真正的画字留在 car.c(它已经拥有 LCD 分页框架)。
 *       ⇒ 可 PC 单测(pc_test/test_disp_run.c)。
 *
 * ── 为什么要"哪几行变了"这个位掩码 ──
 *   本仓库踩过: LCD 全屏重绘把主循环拖慢, 导致"数主循环拍×假设1ms"的 RPM 虚高 5 倍
 *   (`4bbaae5` 修的就是它)。GC9A01 每个字符都要发 set_window + 像素流, 整屏重画很贵。
 *   ⇒ 规矩是**每帧只重画变化的行**。把"变没变"算在纯层里, car.c 那边就只剩
 *      `if (mask & (1<<i)) GC9A01_DrawString(...)` 一句, 想写错都难。
 *
 * ── 为什么这一页在本题是必需件, 不是锦上添花 ──
 *   ① 说明 5 明文要求"**按键启动时计时系统开始计时并显示时间**" ⇒ 运行中必须实时走时;
 *   ② 答疑 Q62"**测试期间仅允许图传工作**" ⇒ 正式测试时无线遥测必须关,
 *      车载 LCD 是我们唯一能看到内部状态的通道(把车捡回来念数/拍照)。
 *
 * 行的取舍(240x240 圆屏, 中间那条最宽 ⇒ 最重要的放中间):
 *   L0  走时      —— 最大号, 放中间, 评委也能瞄一眼
 *   L1  状态      —— READY / RUN / BRAKE / DONE / ABORT
 *   L2  失败原因  —— 仅 ABORT 时非空(TIMEOUT / LOST / MANUAL)
 *   L3  里程      —— "6.14m", 判"到底跑了多远"用, 也是双闸门里程那一半的可视化
 *   L4  球位      —— "+ 4.2mm"; 第 2 项时球在杆上但不考核, 第 4/5/6 项时这是主看的量
 *   L5  跑次+丢线 —— "#2 L0" (第 2 趟 / 本趟丢线 0 次), 一眼看出"这趟数据是第几趟"
 *
 * 状态: 2026-07-29 新建。**PC 单测已过; 真机零验证**(未进 car.c、未上屏)。
 */
#include <stdint.h>

#define DISP_RUN_LINES   6
#define DISP_RUN_LEN    14      /* 每行最多 13 字符 + '\0'。scale=2 时 240px 约放得下 13 个 */

/* 与 task.h 的枚举同序, 但**故意不 include task.h** —— 保持本层可独立单测,
 * 也避免显示层反向依赖任务层。car.c 传值时是 1:1 直传。 */
typedef struct {
    int      state;        /* 0=IDLE 1=RUN 2=BRAKE 3=DONE 4=ABORT（= task_state_t） */
    int      fail;         /* 0=NONE 1=TIMEOUT 2=LOST 3=MANUAL（= task_fail_t） */
    uint32_t elapsed_ms;
    float    dist_mm;
    float    ball_mm;      /* 球位(mm)，无效时传 DISP_RUN_NO_BALL */
    uint32_t n_runs;
    uint32_t n_lost;       /* 本趟丢线段数(排障用；0 最好) */
} disp_run_in_t;

#define DISP_RUN_NO_BALL   (-9999.0f)   /* ball_mm 传这个值 ⇒ 该行显示 "ball --" */

typedef struct {
    char line[DISP_RUN_LINES][DISP_RUN_LEN];
} disp_run_txt_t;

/* 按 in 填好 6 行文本。cur 必须非 NULL。 */
void disp_run_build(const disp_run_in_t *in, disp_run_txt_t *cur);

/* 与上一帧比, 返回位掩码: bit i = 第 i 行需要重画。prev 为 NULL 时返回全 1(首帧全画)。 */
uint32_t disp_run_diff(const disp_run_txt_t *prev, const disp_run_txt_t *cur);

/*
 * ── car.c 那边只剩这么点(等 car.c 放行后贴) ──────────────────────────
 *   static disp_run_txt_t s_prev; static int s_first = 1;
 *   static const int16_t Y[DISP_RUN_LINES] = { 96, 40, 64, 140, 164, 196 };
 *
 *   void disp_run_page(const task_out_t *to, float dist_mm, float ball_mm, uint32_t n_lost)
 *   {
 *       disp_run_in_t in; disp_run_txt_t cur; uint32_t m; int i;
 *       in.state = to->state; in.fail = to->fail; in.elapsed_ms = to->elapsed_ms;
 *       in.dist_mm = dist_mm; in.ball_mm = ball_mm;
 *       in.n_runs = g_task.n_runs; in.n_lost = n_lost;
 *       disp_run_build(&in, &cur);
 *       m = disp_run_diff(s_first ? 0 : &s_prev, &cur);
 *       for (i = 0; i < DISP_RUN_LINES; i++)
 *           if (m & (1u << i))
 *               GC9A01_DrawStringCentered(Y[i], cur.line[i], LCD_WHITE, LCD_BLACK,
 *                                         (i == 0) ? 4 : 2);   // 走时用大号
 *       s_prev = cur; s_first = 0;
 *   }
 *
 * ⚠ 两条：① 覆盖式绘制要给 bg 色(不能透明底)，否则旧数字残留在下面；
 *          本工程 GC9A01_DrawString 的 bg 参数就是干这个的。
 *        ② 行内容变短时(如 "1:02.3" -> "9.9")右边会留残字 ⇒ build 已把每行**右侧补空格**
 *          到固定宽度, 所以覆盖式绘制天然擦干净, car.c 不用额外 FillRect。
 */

#endif /* DISP_RUN_H */
