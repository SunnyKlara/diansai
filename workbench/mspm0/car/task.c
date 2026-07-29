/*
 * task.c —— 第 2 项任务层实现（纯算法层，见 task.h 文件头的设计判断）
 */
#include "task.h"

/* 32 位毫秒时钟的回绕安全比较：a 是否已到/超过 b。
 * 直接写 (now >= deadline) 在 now 回绕时会瞬间判成"没到"，49.7 天才发生一次，
 * 但代价是整趟跑飞 —— 用差值判号是零成本的正确写法。 */
static int reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static uint32_t elapsed_of(const task_t *T, uint32_t now)
{
    if (T->state == TASK_IDLE) return 0u;
    if (T->state == TASK_DONE || T->state == TASK_ABORT) return T->t_stop - T->t_start;
    return now - T->t_start;
}

void task_init(task_t *T)
{
    if (!T) return;
    T->state = TASK_IDLE;
    T->fail  = TASK_FAIL_NONE;
    T->btn_stable = 0;
    T->btn_last_raw = 0;
    T->btn_raw_at = 0u;
    T->btn_armed = 0;        /* ⚠ 上电默认**不 armed**：若上电时按键正被按住（或引脚悬空读成
                              * 按下），必须先看到一次稳定松开才允许触发，否则会一上电就起跑。 */
    T->t_start = T->t_stop = T->t_brake = T->t_lost = T->t_disp = 0u;
    T->d0 = 0.0f;
    T->n_runs = 0u;          /* 初版写成"读旧值保留跑次"，那是在**未初始化的结构体**上读，
                              * 单测里直接读出 309239330。init 就该把全部字段写定，
                              * 跑次累加靠"只 init 一次"实现，不靠读脏内存。 */
}

void task_abort(task_t *T, uint32_t now_ms, task_fail_t why)
{
    if (!T) return;
    if (T->state == TASK_RUN || T->state == TASK_BRAKE) {
        T->t_stop = now_ms;
        T->state  = TASK_ABORT;
        T->fail   = (why == TASK_FAIL_NONE) ? TASK_FAIL_MANUAL : why;
    } else {
        /* 不在跑：把状态收回 IDLE，让下一次按键还能起跑（急停不该让系统卡死） */
        T->state = TASK_IDLE;
        T->fail  = TASK_FAIL_NONE;
    }
}

/* 按键消抖 + 边沿检测。返回 1 = 本拍产生了一次"有效按下"。
 *
 * ⚠ 这里有个被 PC 单测抓出来的真 bug（初版写错，留注释防以后改回去）：
 *   初版只在观察到 1→0 **跳变**时才 `btn_armed = 1`。而真板子上电时按键**本来就是松开的**、
 *   永远不会出现那个跳变 ⇒ armed 恒 0 ⇒ **按键永远不响应**。板上的症状会是"按了没反应"，
 *   然后人会去怀疑引脚/上拉/消抖时间，查很久。
 *   正解：armed 由**稳定状态**决定（稳定松开就 armed），不由跳变决定。
 *   而"上电时按键正被按住"仍然不许起跑，靠的是 init 时 armed=0 + 只有 stable==0 才 arm。 */
static int btn_edge(task_t *T, uint32_t now, int raw)
{
    int fired = 0;
    raw = raw ? 1 : 0;
    if (raw != T->btn_last_raw) {          /* 原始电平变了 -> 重新起算稳定窗 */
        T->btn_last_raw = raw;
        T->btn_raw_at   = now;
        return 0;
    }
    if ((uint32_t)(now - T->btn_raw_at) < TASK_BTN_DEBOUNCE_MS) return 0;  /* 还没稳定够 */

    if (raw != T->btn_stable) {             /* 稳定电平发生了变化 -> 采纳 */
        T->btn_stable = raw;
        if (raw && T->btn_armed) { fired = 1; T->btn_armed = 0; }
    }
    if (!T->btn_stable) T->btn_armed = 1;   /* 稳定松开（不论这一拍是否刚变化）-> 允许下次触发 */
    return fired;
}

void task_step(task_t *T, const task_in_t *in, task_out_t *out)
{
    uint32_t now;
    int pressed;

    if (!T || !in || !out) return;
    now = in->now_ms;
    pressed = btn_edge(T, now, in->btn);

    out->beep       = TASK_BEEP_NONE;
    out->disp_dirty = 0;

    switch (T->state) {
    case TASK_IDLE:
        if (pressed) {
            T->state   = TASK_RUN;
            T->fail    = TASK_FAIL_NONE;
            T->t_start = now;
            T->t_brake = 0u;
            T->t_lost  = 0u;
            T->t_disp  = now;
            T->d0      = in->dist_mm;      /* 本层自己做差 ⇒ 上层不必清零编码器 */
            T->n_runs++;
            out->beep       = TASK_BEEP_START;
            out->disp_dirty = 1;
        }
        break;

    case TASK_RUN: {
        float d = in->dist_mm - T->d0;

        /* 再按一次 = 手动急停（说明 5 只要求"启动按键"，多这个功能无害且现场救命） */
        if (pressed) { task_abort(T, now, TASK_FAIL_MANUAL); out->beep = TASK_BEEP_ABORT;
                       out->disp_dirty = 1; break; }

        /* 丢线累计 —— 两级里的第二级（第一级"保持转向继续搜"在 line.c 那边） */
        if (in->line_lost) {
            if (T->t_lost == 0u) T->t_lost = now;
            if ((now - T->t_lost) >= TASK_LOST_STOP_MS) {
                task_abort(T, now, TASK_FAIL_LOST); out->beep = TASK_BEEP_ABORT;
                out->disp_dirty = 1; break;
            }
        } else {
            T->t_lost = 0u;
        }

        if (reached(now, T->t_start + TASK_MAX_MS)) {
            task_abort(T, now, TASK_FAIL_TIMEOUT); out->beep = TASK_BEEP_ABORT;
            out->disp_dirty = 1; break;
        }

        /* ⭐ 到达判定 = 双闸门（里程够 + cross），且起步屏蔽窗内一律不算 */
        if (d >= TASK_ARM_BLIND_MM && d >= TASK_LAP_MIN_MM && in->line_cross) {
            T->state   = TASK_BRAKE;
            T->t_brake = now;
            out->disp_dirty = 1;
        }
        break;
    }

    case TASK_BRAKE:
        /* 计时仍在走：评委按秒表的时机是**车停住**那一刻（task.h 设计判断 1） */
        if (pressed) { task_abort(T, now, TASK_FAIL_MANUAL); out->beep = TASK_BEEP_ABORT;
                       out->disp_dirty = 1; break; }
        if (in->stopped || reached(now, T->t_brake + TASK_BRAKE_MS)) {
            T->t_stop = now;
            T->state  = TASK_DONE;
            out->beep       = TASK_BEEP_DONE;
            out->disp_dirty = 1;
        }
        break;

    case TASK_DONE:
    case TASK_ABORT:
        /* 停稳后再按一次 = 复位到 IDLE 准备下一趟（走时保留到下次起跑前都可读） */
        if (pressed) { T->state = TASK_IDLE; T->fail = TASK_FAIL_NONE; out->disp_dirty = 1; }
        break;

    default:
        T->state = TASK_IDLE;
        break;
    }

    /* 走时刷屏节流：说明 5 要求**运行中实时显示**，所以 RUN/BRAKE 里周期性置 dirty */
    if ((T->state == TASK_RUN || T->state == TASK_BRAKE) &&
        reached(now, T->t_disp + TASK_DISP_MS)) {
        T->t_disp = now;
        out->disp_dirty = 1;
    }

    /* 速度档 */
    if (T->state == TASK_RUN) {
        float d = in->dist_mm - T->d0;
        out->v_mode = (d >= TASK_SLOW_MM) ? TASK_V_SLOW : TASK_V_CRUISE;
    } else {
        out->v_mode = TASK_V_STOP;       /* IDLE/BRAKE/DONE/ABORT 一律不给前进速度 */
    }

    out->state      = T->state;
    out->fail       = T->fail;
    out->elapsed_ms = elapsed_of(T, now);
}

int task_fmt_time(uint32_t ms, char *buf, int n)
{
    uint32_t total_ds = ms / 100u;             /* 0.1s 为单位，向下取整 */
    uint32_t ds  = total_ds % 10u;
    uint32_t s   = (total_ds / 10u) % 60u;
    uint32_t min = (total_ds / 10u) / 60u;
    int i = 0;

    if (!buf || n < 12) return 0;
    if (min > 0u) {
        if (min >= 100u) min = 99u;            /* 显示宽度封顶，别把屏挤爆 */
        if (min >= 10u) buf[i++] = (char)('0' + (min / 10u));
        buf[i++] = (char)('0' + (min % 10u));
        buf[i++] = ':';
        buf[i++] = (char)('0' + (s / 10u));    /* 有分钟时秒必须两位: 1:02.3 */
        buf[i++] = (char)('0' + (s % 10u));
    } else {
        if (s >= 10u) buf[i++] = (char)('0' + (s / 10u));
        buf[i++] = (char)('0' + (s % 10u));
    }
    buf[i++] = '.';
    buf[i++] = (char)('0' + ds);
    buf[i]   = '\0';
    return i;
}
