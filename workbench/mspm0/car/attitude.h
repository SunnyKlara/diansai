/*
 * attitude.h - 六轴姿态解算 (纯算法层, 不依赖 HAL, 可 PC 单独编译验证)
 *
 * 职责: 把 IMU 的 陀螺(°/s) + 加速度(g) 融合成姿态角(度)。
 *   - yaw  : 陀螺 Z 轴去零偏后积分而来。无磁力计绝对参考 => 会缓慢漂移,
 *            仅用于"相对转角"闭环(转弯前 attitude_reset_yaw(a,0), 转到 |yaw|≈目标)。
 *            连续累计(可超 ±360), 便于"转 720°"这类;需要归一化用 attitude_wrap180()。
 *   - pitch/roll : 陀螺积分 + 加速度重力矢量 互补滤波(消除积分漂移)。
 *            地面小车正常行驶 pitch/roll≈0, 可用于上/下坡、侧倾、卡死翻车检测。
 *
 * 与 HAL 无关: 只做浮点运算, 可在 PC 用 gcc 编译跑 test_attitude.c 验证(见同目录)。
 *
 * ★ 轴向/符号约定(本模块的硬约定, 调用侧负责满足):
 *   本模块**固定约定 slot2(下标2) = 竖直轴 = 偏航轴**, slot0/slot1 = 车体水平两轴。
 *   真机上 IMU 的贴装朝向不一定让 Z 朝上(天猛星 2026-07-27 实测重力主要落在 +Y),
 *   所以**调用侧必须先用 attitude_axis_map() 把真实竖直轴置换到 slot2 再喂进来**,
 *   并按需对 slot2 的角速度取反(左转为正)。这两个平台相关的值在 config.h:
 *   CFG_YAW_AXIS / CFG_YAW_SIGN(天猛星运行时还可用命令 a/s 改, 定轴不必重烧)。
 *   => 本模块因此与贴装朝向解耦, 可 PC 单测(pc_test/test_attitude.c 覆盖置换与符号)。
 */
#ifndef ATTITUDE_H
#define ATTITUDE_H

typedef struct {
    float yaw, pitch, roll;   /* 输出姿态角(度); yaw 为连续累计值 */
    float gbias[3];           /* 陀螺零偏(°/s), 静止标定得到, 积分前扣除 */
    float dt;                 /* 采样周期(s), 由调用节拍决定 */
    float alpha;              /* 互补滤波系数[0,1]: 越接近1越信陀螺(短期), 1-alpha 信加速度(长期) */
    int   started;            /* 预留 */
    /* 零偏标定累加器(静止时用) */
    float bacc[3];
    int   bcount;
} attitude_t;

/* 初始化: dt=采样周期(秒), alpha=互补滤波系数(如 200Hz 下 0.98)。清零所有状态。 */
void  attitude_init(attitude_t *a, float dt, float alpha);

/* 清零姿态角(yaw/pitch/roll=0); 不动零偏。 */
void  attitude_reset(attitude_t *a);

/* 把 yaw 直接设为 yaw0(转弯前置 0, 作相对转角基准)。 */
void  attitude_reset_yaw(attitude_t *a, float yaw0);

/* --- 陀螺零偏标定(小车静止时): start -> 多次 sample -> apply --- */
void  attitude_bias_start(attitude_t *a);
void  attitude_bias_sample(attitude_t *a, const float gyro_dps[3]);
void  attitude_bias_apply(attitude_t *a);

/* 走一拍融合: gyro_dps[3]=陀螺(°/s), accel_g[3]=加速度(g)。更新 yaw/pitch/roll。
 * yaw   = 上一 yaw + (gz-bias)*dt         (纯积分)
 * roll  = alpha*(roll +(gx-bias)*dt) + (1-alpha)*atan2(ay,az)
 * pitch = alpha*(pitch+(gy-bias)*dt) + (1-alpha)*atan2(-ax, hypot(ay,az))
 * 仅当加速度模长≈1g(0.5~1.5g)时才用加速度校正, 排除强动态干扰。 */
void  attitude_update(attitude_t *a, const float gyro_dps[3], const float accel_g[3]);

/* 角度归一化到 [-180, 180)。 */
float attitude_wrap180(float deg);

/* ★ 偏航角速度 = 角速度在"竖直方向"上的投影（点积）。
 *   up[3] = 静止时测得的**重力反方向单位向量**(加速度计静止读数归一化, 指向天), 传感器系。
 *   gyro_dps[3] = 原始传感器系角速度(**不需要任何轴向置换**)。
 *
 * 为什么这个比"挑最近的那个轴"好（2026-07-27 定为主路径）:
 *   ① **不要求 IMU 装正**。板子相对底盘歪 θ 度也没关系 —— 挑轴法会引入 cos(θ) 的**永久系统性
 *      比例误差**(θ=14° 时转 90° 差 2.7°, 且每次同方向偏、会累积), 投影法**没有这个误差**。
 *   ② **不需要"放平"这个前置**。挑轴法必须先让某个轴 ≈±1g 才能认出它是竖直轴; 投影法只要求
 *      "标定时的姿态 = 以后跑的姿态", 姿态本身可以是任意倾斜。
 *   ③ **重新标定=按一次键**。换装/挪板/重新接线后, 静止 2s 重标即可, 不用改宏重烧。
 *   代价: up 必须在**真实行驶姿态**下测 —— 这是唯一无法由传感器自己知道的语义信息(它测得出
 *   "现在朝哪", 测不出"这个朝向是不是你以后要跑的朝向"), 所以必然要人给这一下。
 */
float attitude_yaw_rate(const float gyro_dps[3], const float up[3]);

/* 轴向置换: 把 yaw_axis 指定的物理竖直轴(0=X 1=Y 2=Z)循环置换到 slot2, 使上面
 * "slot2=偏航轴" 的约定成立。in/out 均为 [x,y,z] 顺序, 陀螺与加速度都要过同一次置换。
 *   yaw_axis=0: (x,y,z)->(y,z,x)   yaw_axis=1: (x,y,z)->(z,x,y)   其它: 恒等
 * 用**循环**置换而非"直接把 gy 当 yaw"的原因: 循环置换的行列式=+1(是一个真旋转),
 * 右手系与 pitch/roll 的手性都不被破坏; 若只交换两轴(det=-1)会把姿态变成镜像。
 * 允许 out==in(内部先取值再写回, 无别名问题)。 */
void  attitude_axis_map(int yaw_axis, const float in[3], float out[3]);

#endif /* ATTITUDE_H */
