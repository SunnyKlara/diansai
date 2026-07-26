/*
 * test_attitude.c - attitude.c 六轴姿态解算的 PC 单元测试(纯算法层可离线验证)
 *
 * 编译运行(在本目录):
 *   gcc -O2 -Wall -I.. -o test_attitude test_attitude.c ../attitude.c -lm
 *   ./test_attitude        (Windows: .\test_attitude.exe)
 *
 * 覆盖:
 *   1-5  attitude.c 本体: 陀螺积分得 yaw / 零偏标定与扣除 / 重力校正 pitch&roll / 归一化
 *   6-8  车级集成层(car.c 那段的等效实现): 轴向置换正确性与手性 / **选错轴的失败模式** /
 *        yaw 符号 / 死区经"加回零偏"路径的等效性
 * 这些是与硬件无关的部分, 可在 PC 断言验证; 真机上仍需 L1(定轴) L2(定符号) L3(静态漂移)。
 */
#include "attitude.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int g_fail = 0;

static void check(const char *name, float got, float want, float tol)
{
    float d = got - want; if (d < 0) d = -d;
    if (d <= tol) {
        printf("  [PASS] %-28s got=%8.3f want=%8.3f (tol %.3f)\n", name, got, want, tol);
    } else {
        printf("  [FAIL] %-28s got=%8.3f want=%8.3f (tol %.3f)\n", name, got, want, tol);
        g_fail++;
    }
}

/* 测试1: 恒定偏航角速度积分 -> yaw 应≈角速度*时间 */
static void test_yaw_integration(void)
{
    printf("test_yaw_integration:\n");
    attitude_t a;
    attitude_init(&a, 0.005f, 0.98f);       /* 200Hz */
    float gyro[3] = {0, 0, 90.0f};          /* 90 °/s 绕 Z */
    float acc[3]  = {0, 0, 1.0f};           /* 静态重力(不影响 yaw) */
    for (int i = 0; i < 200; i++) attitude_update(&a, gyro, acc);  /* 1.0s */
    check("yaw after 90dps*1s", a.yaw, 90.0f, 0.5f);
    check("pitch stays 0", a.pitch, 0.0f, 0.5f);
    check("roll stays 0", a.roll, 0.0f, 0.5f);
}

/* 测试2: 零偏标定后, 静止漂移应被扣除 -> yaw 不涨 */
static void test_bias_calibration(void)
{
    printf("test_bias_calibration:\n");
    attitude_t a;
    attitude_init(&a, 0.005f, 0.98f);
    float gbias_true[3] = {0.5f, -0.3f, 1.2f};   /* 假设静止时陀螺读数(=零偏) */

    attitude_bias_start(&a);
    for (int i = 0; i < 100; i++) attitude_bias_sample(&a, gbias_true);
    attitude_bias_apply(&a);
    check("bias_z estimated", a.gbias[2], 1.2f, 0.001f);

    /* 之后仍读到同样的静止零偏 -> 扣除后 yaw 应基本不动 */
    float acc[3] = {0, 0, 1.0f};
    for (int i = 0; i < 400; i++) attitude_update(&a, gbias_true, acc);  /* 2s */
    check("yaw drift removed", a.yaw, 0.0f, 0.05f);
}

/* 测试3: 静态倾斜 -> 加速度重力校正使 roll 收敛到几何角 */
static void test_accel_tilt(void)
{
    printf("test_accel_tilt:\n");
    attitude_t a;
    attitude_init(&a, 0.005f, 0.98f);
    /* 绕 X 倾斜 30°: ay=sin30, az=cos30 */
    float acc[3]  = {0.0f, 0.5f, 0.8660254f};
    float gyro[3] = {0, 0, 0};
    for (int i = 0; i < 4000; i++) attitude_update(&a, gyro, acc);  /* 收敛 */
    check("roll -> 30deg", a.roll, 30.0f, 0.5f);
    check("pitch -> 0", a.pitch, 0.0f, 0.5f);
}

/* 测试4: 强动态(加速度模长远离1g)时不做重力校正, 仅陀螺积分 */
static void test_dynamic_reject(void)
{
    printf("test_dynamic_reject:\n");
    attitude_t a;
    attitude_init(&a, 0.005f, 0.98f);
    float gyro[3] = {60.0f, 0, 0};          /* 绕 X 60°/s */
    float acc[3]  = {0.0f, 3.0f, 3.0f};     /* 模长≈4.24g, 强动态, 应被拒绝 */
    for (int i = 0; i < 200; i++) attitude_update(&a, gyro, acc);  /* 1s */
    check("roll pure-integ 60deg", a.roll, 60.0f, 0.5f);   /* 未被错误的重力角拉走 */
}

/* 测试5: 角度归一化 */
static void test_wrap(void)
{
    printf("test_wrap:\n");
    check("wrap 190", attitude_wrap180(190.0f), -170.0f, 0.001f);
    check("wrap -190", attitude_wrap180(-190.0f), 170.0f, 0.001f);
    check("wrap 370", attitude_wrap180(370.0f), 10.0f, 0.001f);
    check("wrap 720", attitude_wrap180(720.0f), 0.0f, 0.001f);
}

/* ==================== 以下为轴向置换 / 符号 / 死区(车级集成层) ====================
 * 背景: 真机实测重力不落在 Z(2026-07-27 主要在 +Y), 所以 car.c 要先把物理竖直轴置换到
 * slot2 再喂 attitude_update。**轴选错的现象特别阴**: 航向环完全不工作, 但看起来像
 * "PID 怎么都调不出来", 会诱人去调增益。所以这一层必须在 PC 上先断言死。 */

/* 测试6: 三种轴向下, 重力都应落到 slot2; 且置换保持右手系(det=+1) */
static void test_axis_map(void)
{
    printf("test_axis_map:\n");
    float out[3];

    /* 竖直轴 = X: 输入重力在 x */
    float gx_up[3] = { 1.0f, 0.0f, 0.0f };
    attitude_axis_map(0, gx_up, out);
    check("axis0: g -> slot2", out[2], 1.0f, 1e-6f);

    /* 竖直轴 = Y: 输入重力在 y (天猛星实测的疑似情况) */
    float gy_up[3] = { 0.0f, 1.0f, 0.0f };
    attitude_axis_map(1, gy_up, out);
    check("axis1: g -> slot2", out[2], 1.0f, 1e-6f);

    /* 竖直轴 = Z: 恒等 */
    float gz_up[3] = { 0.0f, 0.0f, 1.0f };
    attitude_axis_map(2, gz_up, out);
    check("axis2: g -> slot2", out[2], 1.0f, 1e-6f);

    /* 手性: 置换必须是真旋转(det=+1), 判据 = map(a) x map(b) == map(a x b)。
     * 若只"交换两轴"(det=-1)则姿态变镜像, 这里会失败。取 a=x_hat, b=y_hat, axb=z_hat。 */
    for (int ax = 0; ax <= 2; ax++) {
        float a[3] = {1,0,0}, b[3] = {0,1,0}, axb[3] = {0,0,1};
        float ma[3], mb[3], maxb[3], cross[3];
        attitude_axis_map(ax, a, ma);
        attitude_axis_map(ax, b, mb);
        attitude_axis_map(ax, axb, maxb);
        cross[0] = ma[1]*mb[2] - ma[2]*mb[1];
        cross[1] = ma[2]*mb[0] - ma[0]*mb[2];
        cross[2] = ma[0]*mb[1] - ma[1]*mb[0];
        float err = fabsf(cross[0]-maxb[0]) + fabsf(cross[1]-maxb[1]) + fabsf(cross[2]-maxb[2]);
        char nm[40]; snprintf(nm, sizeof nm, "axis%d right-handed(det=+1)", ax);
        check(nm, err, 0.0f, 1e-6f);
    }

    /* 原地置换(out==in)不得自我破坏: car.c 现在传的是不同数组, 但别人复用时很容易踩 */
    float inplace[3] = { 1.0f, 2.0f, 3.0f };
    attitude_axis_map(1, inplace, inplace);          /* 期望 (x,y,z)->(z,x,y) = (3,1,2) */
    check("in-place map [0]", inplace[0], 3.0f, 1e-6f);
    check("in-place map [1]", inplace[1], 1.0f, 1e-6f);
    check("in-place map [2]", inplace[2], 2.0f, 1e-6f);
}

/* car.c 的集成层等效实现(与固件里那段保持一致), 供下面两个测试复用。
 *   置换 -> 定符号 -> (去零偏->死区->加回零偏) -> attitude_update
 * 最后那步是为了让 attitude.c 一行不改: attitude_update 内部还会减一次 gbias。 */
static void car_imu_tick(attitude_t *a, int axis, int sign, float deadband,
                         const float gyro_raw[3], const float acc_raw[3], float *wz_out)
{
    float gm[3], am[3];
    attitude_axis_map(axis, gyro_raw, gm);
    attitude_axis_map(axis, acc_raw,  am);
    gm[2] *= (float)sign;
    float wz = gm[2] - a->gbias[2];
    if (wz < deadband && wz > -deadband) wz = 0.0f;
    gm[2] = a->gbias[2] + wz;
    if (wz_out) *wz_out = wz;
    attitude_update(a, gm, am);
}

/* 测试7: 车按 +Y 朝上安装, 绕物理竖直轴以 90dps 左转 1s。
 *   axis=1(对) -> yaw≈90 ; axis=2(错) -> yaw≈0  <== 就是"航向环完全不工作"的那个失败模式 */
static void test_axis_selection_matters(void)
{
    printf("test_axis_selection_matters:\n");
    /* 传感器坐标下: 重力在 +Y, 偏航角速度也出现在 gy */
    float gyro[3] = { 0.0f, 90.0f, 0.0f };
    float acc[3]  = { 0.0f, 1.0f,  0.0f };

    attitude_t ok; attitude_init(&ok, 0.005f, 0.98f);
    for (int i = 0; i < 200; i++) car_imu_tick(&ok, 1, +1, 0.0f, gyro, acc, NULL);
    check("axis=1 (correct) yaw", ok.yaw, 90.0f, 0.5f);

    attitude_t bad; attitude_init(&bad, 0.005f, 0.98f);
    for (int i = 0; i < 200; i++) car_imu_tick(&bad, 2, +1, 0.0f, gyro, acc, NULL);
    check("axis=2 (wrong) yaw~0", bad.yaw, 0.0f, 0.5f);   /* 车真的在转, yaw 却不动 */

    /* 符号反了 -> 转向读反(常见: 左转读成负) */
    attitude_t neg; attitude_init(&neg, 0.005f, 0.98f);
    for (int i = 0; i < 200; i++) car_imu_tick(&neg, 1, -1, 0.0f, gyro, acc, NULL);
    check("sign=-1 flips yaw", neg.yaw, -90.0f, 0.5f);
}

/* 测试8: 死区经"加回零偏"这条路要与"先去偏->死区->积分"完全等效 */
static void test_deadband_equivalence(void)
{
    printf("test_deadband_equivalence:\n");
    const float bias = 1.2f, db = 0.25f;
    float acc[3] = { 0.0f, 0.0f, 1.0f };

    /* (a) 残余角速度 0.1dps < 死区 -> 应被完全吃掉, yaw 不动 */
    attitude_t a1; attitude_init(&a1, 0.005f, 0.98f); a1.gbias[2] = bias;
    float g_small[3] = { 0.0f, 0.0f, bias + 0.1f };
    float wz = -1.0f;
    for (int i = 0; i < 400; i++) car_imu_tick(&a1, 2, +1, db, g_small, acc, &wz);  /* 2s */
    check("deadband kills 0.1dps", a1.yaw, 0.0f, 1e-4f);
    check("wz reported 0", wz, 0.0f, 1e-6f);

    /* (b) 真实转动 30dps 远大于死区 -> 必须原样积分, 死区不得偷走它 */
    attitude_t a2; attitude_init(&a2, 0.005f, 0.98f); a2.gbias[2] = bias;
    float g_turn[3] = { 0.0f, 0.0f, bias + 30.0f };
    for (int i = 0; i < 200; i++) car_imu_tick(&a2, 2, +1, db, g_turn, acc, &wz);   /* 1s */
    check("30dps*1s integrates", a2.yaw, 30.0f, 0.05f);
    check("wz reported 30", wz, 30.0f, 1e-3f);

    /* (c) 死区=0(当前 config.h 默认) 时行为 = 纯积分, 与不加死区一致 */
    attitude_t a3; attitude_init(&a3, 0.005f, 0.98f); a3.gbias[2] = bias;
    for (int i = 0; i < 200; i++) car_imu_tick(&a3, 2, +1, 0.0f, g_small, acc, &wz);
    check("deadband=0 -> pure integ", a3.yaw, 0.1f * 1.0f, 1e-3f);   /* 0.1dps*1s */
}

int main(void)
{
    printf("==== attitude.c PC unit test ====\n");
    test_yaw_integration();
    test_bias_calibration();
    test_accel_tilt();
    test_dynamic_reject();
    test_wrap();
    test_axis_map();
    test_axis_selection_matters();
    test_deadband_equivalence();
    printf("==== %s (%d failure%s) ====\n", g_fail == 0 ? "ALL PASS" : "FAILED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
