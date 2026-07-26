// -*- coding: utf-8 -*-
// QMI8658A 六轴 IMU 驱动（I2C, IDF 5.5 的 i2c_master 新 API）。
//
// 寄存器与量程配置取自 QMI8658A 数据手册的常规用法，**是否真的对靠三个真机判据**
// （与天猛星 ICM42688 那次的验证套路一致，见仓库 SSOT）：
//   ① WHO_AM_I == 0x05
//   ② 静止时加速度模长 |a| ≈ 1000 mg  <- 定标(LSB/g)正确的硬证据
//   ③ 静止时三轴角速度 ≈ 0            <- 陀螺定标与零偏正常
// 三条都过才算驱动可信；只看"读到了数"是不够的（错的定标也能读到数）。
#include "imu_qmi8658.h"

#include <math.h>

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "qmi8658";

// ---- 寄存器 ----
#define REG_WHO_AM_I   0x00
#define REG_REVISION   0x01
#define REG_CTRL1      0x02   // bit6 ADDR_AI(地址自增)
#define REG_CTRL2      0x03   // aFS[6:4] aODR[3:0]
#define REG_CTRL3      0x04   // gFS[6:4] gODR[3:0]
#define REG_CTRL5      0x06   // 低通滤波
#define REG_CTRL7      0x08   // bit0 aEN, bit1 gEN
#define REG_TEMP_L     0x33
#define REG_AX_L       0x35   // AX,AY,AZ,GX,GY,GZ 连续 12 字节

#define QMI8658_WHOAMI_EXPECT 0x05

// 量程选择: 加速度 ±8g / 陀螺 ±512dps, 两者 ODR 都取 125Hz(够用且噪声低)
#define CTRL2_VALUE   ((0x02 << 4) | 0x06)   // aFS=±8g,  aODR=125Hz
#define CTRL3_VALUE   ((0x05 << 4) | 0x06)   // gFS=±512dps, gODR=125Hz
#define ACC_LSB_PER_G   4096.0f              // 32768 / 8
#define GYR_LSB_PER_DPS 64.0f                // 32768 / 512

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool    s_ok;
static uint8_t s_whoami;
static uint8_t s_addr;

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 200);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    const uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 200);
}

esp_err_t imu_qmi8658_init(int sda_gpio, int scl_gpio)
{
    // ⚠️ 必须用 I2C_NUM_1：本工程的**触摸驱动已经占了 I2C_NUM_0**
    //    （见 main/axs15260_6_2in_lcd.c 的 TP_I2C_PORT，引脚 IO28/IO29）。
    //    第一版这里写 I2C_NUM_0，结果 IMU 先初始化抢到 0 号控制器，触摸随后
    //    i2c_new_master_bus 直接失败（"I2C bus id(0) has already been acquired"），
    //    触摸每帧轮询报错把串口刷满 "clear bus failed"。
    //    教训：原理图上的 "I2C_SDA0/SCL0" 是**电气总线编号**，与 ESP-IDF 的
    //    I2C_NUM_x **控制器编号无关**（任意 GPIO 都能挂任意控制器）—— 别照抄。
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus 失败: %s", esp_err_to_name(err));
        return err;
    }

    // SA0 引脚决定地址(0x6B/0x6A)，原理图未标明 -> 探测而不是猜
    const uint8_t cand[] = { 0x6B, 0x6A };
    s_addr = 0;
    for (size_t i = 0; i < sizeof(cand); i++) {
        if (i2c_master_probe(s_bus, cand[i], 200) == ESP_OK) {
            s_addr = cand[i];
            ESP_LOGI(TAG, "I2C 探测命中地址 0x%02X", s_addr);
            break;
        }
        ESP_LOGW(TAG, "地址 0x%02X 无应答", cand[i]);
    }
    if (s_addr == 0) {
        ESP_LOGE(TAG, "0x6A/0x6B 都无应答 —— 检查 I2C0(SDA=%d SCL=%d) 接线/供电", sda_gpio, scl_gpio);
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_addr,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    if (rd(REG_WHO_AM_I, &s_whoami, 1) != ESP_OK) {
        ESP_LOGE(TAG, "读 WHO_AM_I 失败");
        return ESP_FAIL;
    }
    uint8_t rev = 0;
    rd(REG_REVISION, &rev, 1);
    ESP_LOGI(TAG, "WHO_AM_I=0x%02X (期望 0x%02X), REVISION=0x%02X",
             s_whoami, QMI8658_WHOAMI_EXPECT, rev);
    if (s_whoami != QMI8658_WHOAMI_EXPECT) {
        ESP_LOGE(TAG, "WHO_AM_I 不符 —— 器件不是 QMI8658A 或总线有问题");
        return ESP_ERR_INVALID_RESPONSE;
    }

    // 地址自增(连读 12 字节必需) -> 量程/ODR -> 关低通 -> 使能 acc+gyro
    esp_err_t e = ESP_OK;
    e |= wr(REG_CTRL1, 0x40);
    e |= wr(REG_CTRL2, CTRL2_VALUE);
    e |= wr(REG_CTRL3, CTRL3_VALUE);
    e |= wr(REG_CTRL5, 0x00);
    e |= wr(REG_CTRL7, 0x03);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "写配置寄存器失败");
        return ESP_FAIL;
    }

    s_ok = true;
    ESP_LOGI(TAG, "初始化完成: acc ±8g / gyro ±512dps / ODR 125Hz (%.0f LSB/g, %.0f LSB/dps)",
             ACC_LSB_PER_G, GYR_LSB_PER_DPS);
    return ESP_OK;
}

esp_err_t imu_qmi8658_read(imu_reading_t *out)
{
    if (!s_ok || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t t[2] = {0};
    if (rd(REG_TEMP_L, t, 2) != ESP_OK) {
        return ESP_FAIL;
    }
    out->temp_c = (float)(int8_t)t[1] + (float)t[0] / 256.0f;

    uint8_t d[12] = {0};
    if (rd(REG_AX_L, d, sizeof(d)) != ESP_OK) {
        return ESP_FAIL;
    }
    const int16_t ax = (int16_t)((d[1] << 8) | d[0]);
    const int16_t ay = (int16_t)((d[3] << 8) | d[2]);
    const int16_t az = (int16_t)((d[5] << 8) | d[4]);
    const int16_t gx = (int16_t)((d[7] << 8) | d[6]);
    const int16_t gy = (int16_t)((d[9] << 8) | d[8]);
    const int16_t gz = (int16_t)((d[11] << 8) | d[10]);

    out->ax_mg = (float)ax / ACC_LSB_PER_G * 1000.0f;
    out->ay_mg = (float)ay / ACC_LSB_PER_G * 1000.0f;
    out->az_mg = (float)az / ACC_LSB_PER_G * 1000.0f;
    out->gx_dps = (float)gx / GYR_LSB_PER_DPS;
    out->gy_dps = (float)gy / GYR_LSB_PER_DPS;
    out->gz_dps = (float)gz / GYR_LSB_PER_DPS;
    out->a_norm_mg = sqrtf(out->ax_mg * out->ax_mg +
                           out->ay_mg * out->ay_mg +
                           out->az_mg * out->az_mg);
    return ESP_OK;
}

bool    imu_qmi8658_ok(void)     { return s_ok; }
uint8_t imu_qmi8658_whoami(void) { return s_whoami; }
uint8_t imu_qmi8658_addr(void)   { return s_addr; }
