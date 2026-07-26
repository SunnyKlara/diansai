#include "BSP/bsp_imu.h"

#include <stddef.h>

#include "ti_msp_dl_config.h"

#define ICM45686_I2C_ADDRESS       (0x68U)
#define ICM45686_REG_ACCEL_DATA_X  (0x00U)
#define ICM45686_REG_PWR_MGMT0     (0x10U)
#define ICM45686_REG_ACCEL_CONFIG0 (0x1BU)
#define ICM45686_REG_GYRO_CONFIG0  (0x1CU)
#define ICM45686_REG_WHO_AM_I      (0x72U)
#define ICM45686_REG_MISC2         (0x7FU)
#define ICM45686_WHO_AM_I_VALUE    (0xE9U)

#define ICM45686_SOFT_RESET       (0x02U)
#define ICM45686_ACCEL_4G_200HZ   (0x38U)
#define ICM45686_GYRO_500DPS_200HZ (0x38U)
#define ICM45686_ACCEL_GYRO_LN    (0x0FU)

#define IMU_TRANSFER_TIMEOUT (200000U)
#define IMU_DATA_SIZE        (12U)

static bool g_online;
static uint8_t g_whoAmI;
static uint32_t g_errorCount;

static bool wait_for_idle(void)
{
    uint32_t timeout = IMU_TRANSFER_TIMEOUT;

    while ((DL_I2C_getControllerStatus(IMU_I2C_INST) &
               DL_I2C_CONTROLLER_STATUS_IDLE) == 0U) {
        if (--timeout == 0U) {
            return false;
        }
    }
    return true;
}

static bool wait_for_complete(void)
{
    uint32_t timeout = IMU_TRANSFER_TIMEOUT;

    while ((DL_I2C_getControllerStatus(IMU_I2C_INST) &
               DL_I2C_CONTROLLER_STATUS_BUSY) != 0U) {
        if (--timeout == 0U) {
            return false;
        }
    }
    return (DL_I2C_getControllerStatus(IMU_I2C_INST) &
               DL_I2C_CONTROLLER_STATUS_ERROR) == 0U;
}

static void recover_bus(void)
{
    DL_I2C_flushControllerTXFIFO(IMU_I2C_INST);
    DL_I2C_flushControllerRXFIFO(IMU_I2C_INST);
    DL_I2C_resetControllerTransfer(IMU_I2C_INST);
}

static bool write_register(uint8_t address, uint8_t value)
{
    uint8_t packet[2] = {address, value};

    if (!wait_for_idle()) {
        recover_bus();
        g_errorCount++;
        return false;
    }
    DL_I2C_flushControllerTXFIFO(IMU_I2C_INST);
    if (DL_I2C_fillControllerTXFIFO(IMU_I2C_INST, packet, 2U) != 2U) {
        recover_bus();
        g_errorCount++;
        return false;
    }
    DL_I2C_startControllerTransfer(IMU_I2C_INST, ICM45686_I2C_ADDRESS,
        DL_I2C_CONTROLLER_DIRECTION_TX, 2U);
    delay_cycles(12U);
    if (!wait_for_complete()) {
        recover_bus();
        g_errorCount++;
        return false;
    }
    return true;
}

static bool read_registers(uint8_t address, uint8_t *data, uint8_t length)
{
    uint32_t timeout;
    uint8_t received = 0U;

    if ((data == NULL) || (length == 0U) || !wait_for_idle()) {
        recover_bus();
        g_errorCount++;
        return false;
    }

    DL_I2C_flushControllerTXFIFO(IMU_I2C_INST);
    if (DL_I2C_fillControllerTXFIFO(IMU_I2C_INST, &address, 1U) != 1U) {
        recover_bus();
        g_errorCount++;
        return false;
    }
    DL_I2C_startControllerTransfer(IMU_I2C_INST, ICM45686_I2C_ADDRESS,
        DL_I2C_CONTROLLER_DIRECTION_TX, 1U);
    delay_cycles(12U);
    if (!wait_for_complete() || !wait_for_idle()) {
        recover_bus();
        g_errorCount++;
        return false;
    }

    DL_I2C_flushControllerRXFIFO(IMU_I2C_INST);
    DL_I2C_startControllerTransfer(IMU_I2C_INST, ICM45686_I2C_ADDRESS,
        DL_I2C_CONTROLLER_DIRECTION_RX, length);
    delay_cycles(12U);

    timeout = IMU_TRANSFER_TIMEOUT;
    while (received < length) {
        while (!DL_I2C_isControllerRXFIFOEmpty(IMU_I2C_INST)) {
            data[received++] = DL_I2C_receiveControllerData(IMU_I2C_INST);
            if (received >= length) {
                break;
            }
        }
        if ((DL_I2C_getControllerStatus(IMU_I2C_INST) &
                DL_I2C_CONTROLLER_STATUS_ERROR) != 0U || --timeout == 0U) {
            recover_bus();
            g_errorCount++;
            return false;
        }
    }

    if (!wait_for_complete()) {
        recover_bus();
        g_errorCount++;
        return false;
    }
    return true;
}

static int16_t little_endian_i16(const uint8_t *bytes)
{
    return (int16_t) ((uint16_t) bytes[0] | ((uint16_t) bytes[1] << 8U));
}

bool bsp_imu_init(void)
{
    g_online = false;
    g_whoAmI = 0U;
    g_errorCount = 0U;

    delay_cycles(320000U);
    if (!read_registers(ICM45686_REG_WHO_AM_I, &g_whoAmI, 1U) ||
        (g_whoAmI != ICM45686_WHO_AM_I_VALUE)) {
        return false;
    }

    if (!write_register(ICM45686_REG_MISC2, ICM45686_SOFT_RESET)) {
        return false;
    }
    delay_cycles(320000U);

    if (!read_registers(ICM45686_REG_WHO_AM_I, &g_whoAmI, 1U) ||
        (g_whoAmI != ICM45686_WHO_AM_I_VALUE) ||
        !write_register(ICM45686_REG_ACCEL_CONFIG0,
            ICM45686_ACCEL_4G_200HZ) ||
        !write_register(ICM45686_REG_GYRO_CONFIG0,
            ICM45686_GYRO_500DPS_200HZ) ||
        !write_register(ICM45686_REG_PWR_MGMT0, ICM45686_ACCEL_GYRO_LN)) {
        return false;
    }

    delay_cycles(640000U);
    g_online = true;
    return true;
}

bool bsp_imu_read(BspImuSample *sample)
{
    uint8_t data[IMU_DATA_SIZE];

    if (!g_online || (sample == NULL) ||
        !read_registers(ICM45686_REG_ACCEL_DATA_X, data, sizeof(data))) {
        return false;
    }

    for (uint8_t axis = 0U; axis < 3U; axis++) {
        sample->accelRaw[axis] = little_endian_i16(&data[axis * 2U]);
        sample->gyroRaw[axis] = little_endian_i16(&data[6U + axis * 2U]);
        sample->accelMg[axis] =
            ((int32_t) sample->accelRaw[axis] * 1000) / 8192;
        sample->gyroMdps[axis] =
            ((int32_t) sample->gyroRaw[axis] * 15625) / 1024;
    }
    return true;
}

bool bsp_imu_online(void)
{
    return g_online;
}

uint8_t bsp_imu_who_am_i(void)
{
    return g_whoAmI;
}

uint32_t bsp_imu_error_count(void)
{
    return g_errorCount;
}
