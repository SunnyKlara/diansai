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
 * ★ 轴向/符号约定 // 待真机验证:
 *   假定 IMU 平贴车底, Z 轴朝上 => gyro[2](gz)=偏航角速度。car_drive 左转为 + 时,
 *   期望 gz 也为 + (逆时针)。实际正负取决于 ICM 芯片贴装朝向, 到货后转一下看符号,
 *   不对就在读数处对 gz 取反(或在此不改, 在 car.c 调用侧统一处理)。
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

#endif /* ATTITUDE_H */
