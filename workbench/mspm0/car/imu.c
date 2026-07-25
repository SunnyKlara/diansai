/*
 * imu.c - ICM-42688-P 六轴 IMU 驱动实现 (SPI1 共享总线)
 *
 * ★★★ // 待真机验证 ★★★  陀螺未到货, 依数据手册编写, 未上板运行。
 *
 * 到货后 bring-up 顺序(见 car.c 的 'g' 命令):
 *   1) imu_whoami() 应读到 0x47  —— 不对先查 CS/MISO 接线、供电、SPI 模式。
 *   2) 静止放平, 读 gyro 三轴应≈0(有小零偏), accel Z≈±1g、X/Y≈0。
 *   3) 静止采 N 拍求陀螺零偏(attitude_bias_*), 之后积分 yaw 才不飘。
 *   4) 绕垂直轴慢转 90°, 看 attitude yaw 增量≈90°(符号依装配, 见 attitude.c)。
 */
#include "ti_msp_dl_config.h"
#include "imu.h"

/* 共享 LCD 的 SPI1 实例(SysConfig 生成宏 LCD_SPI_INST=SPI1)。
 * 单独取别名, 表达"这是 IMU 复用同一条总线"。 */
#define IMU_SPI_INST   LCD_SPI_INST

/* 软件片选(GPIO_IMU 实例, SysConfig 生成; PB6 低有效)。
 * 单端口实例的端口宏是 GPIO_IMU_PORT(非每脚 _PORT)。 */
#define IMU_CS_PORT    GPIO_IMU_PORT
#define IMU_CS_PIN     GPIO_IMU_IMU_CS_PIN
#define CS_L()  DL_GPIO_clearPins(IMU_CS_PORT, IMU_CS_PIN)   /* 选中 */
#define CS_H()  DL_GPIO_setPins  (IMU_CS_PORT, IMU_CS_PIN)   /* 释放 */

#define CPUCLK_HZ  32000000UL
static void delay_us(uint32_t us) { delay_cycles((CPUCLK_HZ / 1000000UL) * us); }

/* ================= ICM-42688-P 寄存器 (User Bank 0) ================= */
#define REG_DEVICE_CONFIG   0x11   /* bit0 SOFT_RESET_CONFIG(1=复位) */
#define REG_INT_CONFIG      0x14
#define REG_TEMP_DATA1      0x1D   /* 温度高字节; 后接 accel(6)+gyro(6), 连续 14 字节 */
#define REG_ACCEL_DATA_X1   0x1F
#define REG_GYRO_DATA_X1    0x25
#define REG_PWR_MGMT0       0x4E   /* [3:2]GYRO_MODE [1:0]ACCEL_MODE (11=Low Noise) */
#define REG_GYRO_CONFIG0    0x4F   /* [7:5]GYRO_FS_SEL [3:0]GYRO_ODR */
#define REG_ACCEL_CONFIG0   0x50   /* [7:5]ACCEL_FS_SEL [3:0]ACCEL_ODR */
#define REG_WHO_AM_I        0x75   /* =0x47 */
#define REG_BANK_SEL        0x76

/* 配置值(与 imu.h 的灵敏度宏对应):
 *   GYRO_CONFIG0  = 0x06: FS_SEL=000(±2000dps) | ODR=0110(1kHz)
 *   ACCEL_CONFIG0 = 0x46: FS_SEL=010(±4g)      | ODR=0110(1kHz)
 *   PWR_MGMT0     = 0x0F: GYRO_MODE=11 + ACCEL_MODE=11 (均低噪声) */
#define CFG_GYRO_CONFIG0    0x06
#define CFG_ACCEL_CONFIG0   0x46
#define CFG_PWR_MGMT0       0x0F

#define SPI_READ_BIT        0x80

/* ---- 底层: 一字节全双工收发。共享总线: 先冲刷 LCD 遗留的脏 RX, 再收发。 ---- */
static uint8_t imu_spi_txrx(uint8_t tx)
{
    while (!DL_SPI_isRXFIFOEmpty(IMU_SPI_INST)) { (void)DL_SPI_receiveData8(IMU_SPI_INST); }
    DL_SPI_transmitDataBlocking8(IMU_SPI_INST, tx);
    while (DL_SPI_isBusy(IMU_SPI_INST)) { }
    return DL_SPI_receiveDataBlocking8(IMU_SPI_INST);
}

static uint8_t imu_read_reg(uint8_t reg)
{
    uint8_t v;
    CS_L();
    imu_spi_txrx(reg | SPI_READ_BIT);
    v = imu_spi_txrx(0x00);
    CS_H();
    return v;
}

static void imu_write_reg(uint8_t reg, uint8_t val)
{
    CS_L();
    imu_spi_txrx(reg & 0x7F);
    imu_spi_txrx(val);
    CS_H();
}

/* 突发读: 从 start 起连读 len 字节到 buf(地址自增, 单次 CS 事务) */
static void imu_read_burst(uint8_t start, uint8_t *buf, uint8_t len)
{
    CS_L();
    imu_spi_txrx(start | SPI_READ_BIT);
    for (uint8_t i = 0; i < len; i++) buf[i] = imu_spi_txrx(0x00);
    CS_H();
}

uint8_t imu_whoami(void) { return imu_read_reg(REG_WHO_AM_I); }

int imu_init(void)
{
    uint8_t id;

    DL_SPI_enable(IMU_SPI_INST);   /* LCD 已 enable 过则幂等; IMU 单独用也能起 */
    CS_H();
    delay_us(1000);

    /* 软复位: DEVICE_CONFIG.SOFT_RESET_CONFIG=1, 手册要求复位后等 ~1ms */
    imu_write_reg(REG_DEVICE_CONFIG, 0x01);
    delay_us(2000);

    id = imu_read_reg(REG_WHO_AM_I);
    if (id != ICM42688_WHOAMI_VAL) {
        return (int)id;   /* 失败: 返回读到的值(无芯片常见 0x00/0xFF)。成功值 0x47 与之无歧义, 不阻塞主程序 */
    }

    /* WHO_AM_I 正确 -> 配置 */
    imu_write_reg(REG_BANK_SEL, 0x00);            /* 确保在 Bank 0 */
    imu_write_reg(REG_GYRO_CONFIG0,  CFG_GYRO_CONFIG0);   /* 量程 + ODR */
    imu_write_reg(REG_ACCEL_CONFIG0, CFG_ACCEL_CONFIG0);

    /* 开陀螺+加速度低噪声模式。手册: 使能后 200us 内不要再写其它寄存器;
     * 陀螺完全稳定约需 ~45ms, 故零偏标定应在上电稳定后(通常自然满足)进行。 */
    imu_write_reg(REG_PWR_MGMT0, CFG_PWR_MGMT0);
    delay_us(300);

    return (int)id;   /* =0x47 表示成功 */
}

void imu_read_raw(imu_raw_t *r)
{
    uint8_t b[14];   /* temp(2) + accel xyz(6) + gyro xyz(6) */
    imu_read_burst(REG_TEMP_DATA1, b, 14);

    r->temp = (int16_t)((b[0]  << 8) | b[1]);
    r->ax   = (int16_t)((b[2]  << 8) | b[3]);
    r->ay   = (int16_t)((b[4]  << 8) | b[5]);
    r->az   = (int16_t)((b[6]  << 8) | b[7]);
    r->gx   = (int16_t)((b[8]  << 8) | b[9]);
    r->gy   = (int16_t)((b[10] << 8) | b[11]);
    r->gz   = (int16_t)((b[12] << 8) | b[13]);
}

void imu_convert(const imu_raw_t *r, float gyro_dps[3], float accel_g[3])
{
    gyro_dps[0] = (float)r->gx / IMU_GYRO_LSB_PER_DPS;
    gyro_dps[1] = (float)r->gy / IMU_GYRO_LSB_PER_DPS;
    gyro_dps[2] = (float)r->gz / IMU_GYRO_LSB_PER_DPS;
    accel_g[0]  = (float)r->ax / IMU_ACCEL_LSB_PER_G;
    accel_g[1]  = (float)r->ay / IMU_ACCEL_LSB_PER_G;
    accel_g[2]  = (float)r->az / IMU_ACCEL_LSB_PER_G;
}

float imu_temp_c(int16_t traw)
{
    return (float)traw / 132.48f + 25.0f;
}
