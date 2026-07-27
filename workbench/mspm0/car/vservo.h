#ifndef VSERVO_H
#define VSERVO_H
/*
 * vservo.h - 反应式视觉伺服控制律（阶梯 6 的控制半段）
 *
 * 定位：**纯逻辑 static inline**（与 cmd_gate.h 同一个套路）⇒ car.c 与 pc_test 用的是同一份
 *       代码，不会出现"两份互相抄的实现"。不依赖 HAL、不依赖 math.h。
 *
 * ── 为什么是"先对准、再前进"，而不是同时做 ──
 *   同时做需要知道相机的**几何**（FOV、安装高度与俯角、目标真实尺寸），才能把像素误差换算成
 *   角度、把面积换算成距离。那套标定在赛场上要花掉半天，而且相机一挪就得重标。
 *   "先把目标转到画面中心，再直着开过去"只需要**一个符号正确的比例项**，不需要任何几何标定 ——
 *   代价是走的是折线不是最短路径，对"开过去把球吸起来"这类任务完全够用。
 *   （这也正是 CONTINUATION_GUIDE 里把视觉降级成"反应式视觉伺服、不建图"的那条路线。）
 *
 * ── 安全默认：没有新鲜目标就停 ──
 *   vs_step() 在 have_target=0 时输出 v=w=0 并回 VS_LOST。**绝不允许按最后一帧坐标继续开** ——
 *   相机掉线后那样做等于闭着眼冲。新鲜度判定在 uart_frame.c（uf_get 只在新鲜且有目标时返 1），
 *   这里只负责"拿不到就停"。两层都做，因为这是唯一会把车开进墙里的失效模式。
 *
 * 状态: 2026-07-27 新建。PC 单测已过；**真机零验证**（相机未到手）。
 *       ⚠ 全部参数是按 640×480 图像**估计**的，换分辨率/换镜头必须重给。
 */
#include <stdint.h>

typedef struct {
    int   center_x;    /* 画面中心像素 x（640 宽 => 320）。目标就是把 cx 拉到这里 */
    int   tol_px;      /* 对准容差（像素）：|cx-center| 小于它就算对准，可以开始前进 */
    float kp_w;        /* 像素误差 -> 差速指令（RPM/像素） */
    float w_max;       /* 差速指令上限（RPM） */
    int   area_stop;   /* 目标面积达到此值 = 够近了，停 */
    float kp_v;        /* 面积差 -> 前进速度（RPM/面积单位） */
    int   v_max;       /* 前进速度上限（RPM） */
    int   v_min;       /* 前进最低有效速度（RPM）：低于电机死区就是原地嗡嗡不动 */
} vs_cfg_t;

typedef enum {
    VS_LOST = 0,       /* 没有新鲜目标 -> 已停车，上层决定搜索还是放弃 */
    VS_TURNING,        /* 正在把目标转到画面中心（此时不前进） */
    VS_APPROACH,       /* 已对准，正在开过去（同时做小幅航向微修） */
    VS_ALIGNED         /* 对准且够近 -> 停车，可以动手（吸球/投放） */
} vs_state_t;

/*
 * 走一拍。cx = 目标中心像素 x，area = 目标面积（像素数）。
 * have_target = 0 时 cx/area 被忽略（上层可以传任意值）。
 * 输出的 v 与 w 单位 = RPM，直接喂 m7 的闭环差速层（左=v-w，右=v+w，w>0=左转）。
 */
static inline vs_state_t vs_step(const vs_cfg_t *c, int have_target,
                                 int cx, int area, int *v, int *w)
{
    int err, wi, vi;

    *v = 0; *w = 0;
    if (!have_target) return VS_LOST;      /* 唯一的硬安全默认：拿不到新鲜目标 -> 停 */

    /* err>0 表示目标在画面**左**侧(cx 偏小) => 要**左转**去追它 => w 取正。
     * 这个符号与全工程一致(w>0=左转, 见 car_drive_mix / nav.c)；符号搞反的现象是
     * "车看到目标就往反方向甩开"，一眼能认出来，但**只有先把约定写死才认得出**。 */
    err = c->center_x - cx;

    wi = (int)(c->kp_w * (float)err);
    if (wi >  (int)c->w_max) wi =  (int)c->w_max;
    if (wi < -(int)c->w_max) wi = -(int)c->w_max;

    if (err > c->tol_px || err < -c->tol_px) {
        *w = wi;                            /* 先原地对准: 不前进, 免得越追越偏 */
        return VS_TURNING;
    }

    if (area >= c->area_stop) return VS_ALIGNED;   /* 对准 + 够近: 停, 交给上层动手 */

    vi = (int)(c->kp_v * (float)(c->area_stop - area));
    if (vi > c->v_max) vi = c->v_max;
    if (vi < c->v_min) vi = c->v_min;       /* 低于死区等于不动, 会卡在"快到了"的位置干嗡嗡 */
    *v = vi;
    *w = wi;                                /* 容差内仍做小幅微修, 免得开过去时慢慢漂出去 */
    return VS_APPROACH;
}

#endif /* VSERVO_H */
