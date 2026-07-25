/*
 * test_attitude.c - attitude.c 六轴姿态解算的 PC 单元测试(纯算法层可离线验证)
 *
 * 编译运行(在本目录):
 *   gcc -O2 -Wall -I.. -o test_attitude test_attitude.c ../attitude.c -lm
 *   ./test_attitude        (Windows: .\test_attitude.exe)
 *
 * 覆盖: 陀螺积分得 yaw / 零偏标定与扣除 / 加速度重力校正 pitch&roll / 角度归一化。
 * 这些是"六轴位置算法"里与硬件无关的部分, 可在 PC 断言验证; 上板闭环转角另行真机验证。
 */
#include "attitude.h"
#include <stdio.h>
#include <math.h>

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

int main(void)
{
    printf("==== attitude.c PC unit test ====\n");
    test_yaw_integration();
    test_bias_calibration();
    test_accel_tilt();
    test_dynamic_reject();
    test_wrap();
    printf("==== %s (%d failure%s) ====\n", g_fail == 0 ? "ALL PASS" : "FAILED",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
