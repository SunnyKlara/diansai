/*
 * imu.h - ICM-42688-P 六轴 IMU 驱动 (SPI1 共享总线, 天猛星四驱定版)
 *
 * ★★★ // 待真机验证 ★★★
 *   陀螺仪模块尚未到货, 本驱动全部依据 TDK/InvenSense ICM-42688-P 数据手册(DS-000347)
 *   编写, 未在真板上运行过。到货后 bring-up 顺序见文末。
 *
 * 接线(原理图 v1.3 §12, 与 gc9a01 LCD 共用 SPI1 总线, 独立片选):
 *   SCK  = PB9  (SPI1_SCLK, 与 LCD 共享)
 *   MOSI = PB8  (SPI1_PICO, 与 LCD 共享)  ICM 端为 SDI
 *   MISO = PB7  (SPI1_POCI, IMU 专用; LCD 不接 MISO)  ICM 端为 SDO/AD0
 *   CS   = PB6  (GPIO 软件片选, 低有效; LCD 用另一 GPIO PB14)
 *   INT  = PA29 (数据就绪中断, 本驱动用轮询, INT 暂不使用)
 *
 * SPI 模式: ICM-42688 支持 mode0(CPOL=0,CPHA=0) 与 mode3; 本工程 SPI1 用默认 mode0,
 *   与 LCD 同模式, 可无缝共享总线。读时序: 首字节 = 寄存器地址|0x80(bit7=1 表读)。
 *
 * ⚠ 共享总线注意: LCD(gc9a01.c) 只写不读, 会在 SPI RX FIFO 留下脏字节;
 *   本驱动每次事务前先冲刷 RX FIFO(见 imu.c imu_spi_txrx), 否则读到错位数据。
 *   LCD 与 IMU 均在主循环/命令里访问(非中断), 不会并发, 分时复用安全。
 */
#ifndef IMU_H
#define IMU_H

#include <stdint.h>

/* WHO_AM_I 期望值(ICM-42688-P 固定) */
#define ICM42688_WHOAMI_VAL   0x47

/* 满量程与灵敏度 —— 必须与 imu.c 的配置(GYRO_CONFIG0/ACCEL_CONFIG0)保持一致:
 *   陀螺 ±2000 dps  -> 16.4  LSB/(°/s)
 *   加速度 ±4 g     -> 8192  LSB/g
 * 改量程时这两个宏与 imu.c 里的寄存器值要一起改。 */
#define IMU_GYRO_LSB_PER_DPS  16.4f
#define IMU_ACCEL_LSB_PER_G   8192.0f

/* 原始寄存器读数(16bit 有符号, 大端在寄存器里已被 imu.c 组装为主机字节序) */
typedef struct {
    int16_t ax, ay, az;   /* 加速度原始 (LSB) */
    int16_t gx, gy, gz;   /* 角速度原始 (LSB) */
    int16_t temp;         /* 片上温度原始 (LSB) */
} imu_raw_t;

/* 初始化: 软复位 -> 校验 WHO_AM_I -> (校验通过才)配置量程/ODR/低噪声模式。
 * 返回读到的 WHO_AM_I 值: ==ICM42688_WHOAMI_VAL(0x47) 即成功; 其它值为失败
 * (无芯片/接线错时常读到 0x00 或 0xFF, 与 0x47 无歧义)。失败不阻塞, 主程序照常运行
 * (陀螺未到货时即如此)。判成功: if (imu_init() == ICM42688_WHOAMI_VAL) {...} */
int     imu_init(void);

/* 读 WHO_AM_I 寄存器(0x75), 正常应返回 0x47。用于到货后第一步验活。 */
uint8_t imu_whoami(void);

/* 突发读 加速度+陀螺+温度 原始值(一次 CS 事务, 地址自增)。 */
void    imu_read_raw(imu_raw_t *r);

/* 原始 LSB -> 物理量: gyro_dps[3] (°/s), accel_g[3] (g)。数组顺序 [x,y,z]。 */
void    imu_convert(const imu_raw_t *r, float gyro_dps[3], float accel_g[3]);

/* 片上温度 LSB -> 摄氏度 (数据手册: T = raw/132.48 + 25)。 */
float   imu_temp_c(int16_t traw);

#endif /* IMU_H */
