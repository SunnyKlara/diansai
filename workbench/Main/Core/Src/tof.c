/*
 * tof.c  -  VL53L0X ToF laser height sensor glue
 *
 * Uses the ported ST VL53L0X API (vl53l0x_api*.c) over the bit-bang I2C
 * (vl53l0x_i2c.c, SCL=PD11 / SDA=PD12). Runs the sensor in CONTINUOUS ranging
 * and reads it NON-BLOCKING via the data-ready poll, so the 50Hz control loop
 * is never stalled (unlike the reference project's blocking single-shot).
 *
 * Geometry reuses the same config.h calibration as the ultrasonic path:
 *   height = TUBE_HEIGHT_CM - raw_cm - SENSOR_OFFSET_CM - BALL_DIAMETER_CM
 * (TUBE_HEIGHT_CM / SENSOR_OFFSET_CM must be re-measured for the laser: its
 *  reference point is the front cover glass, not the ultrasonic transducer.)
 *
 * Wall/cross-talk defence in a 45mm tube: RangeStatus gating + the mode's
 * signal-rate/sigma limit checks reject low-confidence (wall scatter) returns;
 * a 3-point median + sliding average (HeightFilter) smooth the rest.
 * ASCII comments only (armclang/GBK pitfall, see 04 debug log 2026-06-10).
 */
#include "tof.h"
#include "config.h"
#include "global.h"
#include "vl53l0x_api.h"
#include "vl53l0x_platform.h"

volatile float   g_tof_raw_mm = -1.0f;
volatile uint8_t g_tof_status = 0xFF;
volatile uint8_t g_tof_id     = 0x00;   /* 0xEE expected once I2C talks */
volatile uint8_t g_tof_init   = 0xFF;   /* 0 = init ok; else failing step */

static VL53L0X_Dev_t  tof_dev;
static VL53L0X_Dev_t *dev = &tof_dev;

/* per-mode tuning (mirrors the vendor reference Mode_data table).
 * cols: signalLimit, sigmaLimit, timingBudget(us), preVcsel, finalVcsel
 * index: 0 default(33ms) / 1 high-accuracy(200ms) / 2 long-range / 3 high-speed(20ms) */
typedef struct {
    FixPoint1616_t signalLimit;
    FixPoint1616_t sigmaLimit;
    uint32_t       timingBudget;
    uint8_t        preVcsel;
    uint8_t        finalVcsel;
} tof_mode_t;

static const tof_mode_t TOF_MODES[4] = {
    { (FixPoint1616_t)(0.25 * 65536), (FixPoint1616_t)(18 * 65536),  33000, 14, 10 }, /* default */
    { (FixPoint1616_t)(0.25 * 65536), (FixPoint1616_t)(18 * 65536), 200000, 14, 10 }, /* high accuracy */
    { (FixPoint1616_t)(0.10 * 65536), (FixPoint1616_t)(60 * 65536),  33000, 18, 14 }, /* long range */
    { (FixPoint1616_t)(0.25 * 65536), (FixPoint1616_t)(32 * 65536),  20000, 14, 10 }, /* high speed */
};

/* 3-point median to kill isolated spikes (same idea as the ultrasonic path) */
static float med_buf[3];
static uint8_t med_n = 0, med_idx = 0;
static float Median3(float x)
{
    med_buf[med_idx] = x;
    med_idx = (uint8_t)((med_idx + 1) % 3);
    if (med_n < 3) { med_n++; return x; }
    float a = med_buf[0], b = med_buf[1], c = med_buf[2];
    float mx = (a > b) ? a : b; mx = (mx > c) ? mx : c;
    float mn = (a < b) ? a : b; mn = (mn < c) ? mn : c;
    return a + b + c - mx - mn;
}

uint8_t Tof_Init(void)
{
    VL53L0X_Error st = VL53L0X_ERROR_NONE;
    uint8_t  vhv, phase;
    uint32_t spad_count;
    uint8_t  spad_aperture;
    uint8_t  mode = (TOF_MODE < 4) ? TOF_MODE : 0;

    dev->I2cDevAddr     = TOF_I2C_ADDR8;   /* power-on default 0x52 (XSHUT tied high) */
    dev->comms_type     = 1;               /* I2C */
    dev->comms_speed_khz = 400;

    VL53L0X_i2c_init();
    HAL_Delay(5);                          /* let the sensor boot after power/XSHUT high */

    /* raw I2C alive test: model id register 0xC0 must read 0xEE.
     * g_tof_id == 0xEE  -> wiring/address/pull-ups OK.
     * g_tof_id == 0x00  -> no ACK: check SDA/SCL pins/swap, XSH=3V3, pull-ups, power. */
    g_tof_id = 0x00;
    VL53L0X_RdByte(dev, 0xC0, (uint8_t *)&g_tof_id);

    g_tof_init = 1; st = VL53L0X_DataInit(dev);                              if (st) goto done;
    g_tof_init = 2; st = VL53L0X_StaticInit(dev);                           if (st) goto done;
    g_tof_init = 3; st = VL53L0X_PerformRefCalibration(dev, &vhv, &phase);   if (st) goto done;
    g_tof_init = 4; st = VL53L0X_PerformRefSpadManagement(dev, &spad_count, &spad_aperture); if (st) goto done;
    g_tof_init = 5; st = VL53L0X_SetLimitCheckEnable(dev, VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, 1);        if (st) goto done;
    g_tof_init = 5; st = VL53L0X_SetLimitCheckEnable(dev, VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, 1);  if (st) goto done;
    g_tof_init = 6; st = VL53L0X_SetLimitCheckValue(dev, VL53L0X_CHECKENABLE_SIGMA_FINAL_RANGE, TOF_MODES[mode].sigmaLimit);       if (st) goto done;
    g_tof_init = 6; st = VL53L0X_SetLimitCheckValue(dev, VL53L0X_CHECKENABLE_SIGNAL_RATE_FINAL_RANGE, TOF_MODES[mode].signalLimit); if (st) goto done;
    g_tof_init = 7; st = VL53L0X_SetMeasurementTimingBudgetMicroSeconds(dev, TOF_MODES[mode].timingBudget); if (st) goto done;
    g_tof_init = 7; st = VL53L0X_SetVcselPulsePeriod(dev, VL53L0X_VCSEL_PERIOD_PRE_RANGE,   TOF_MODES[mode].preVcsel);   if (st) goto done;
    g_tof_init = 7; st = VL53L0X_SetVcselPulsePeriod(dev, VL53L0X_VCSEL_PERIOD_FINAL_RANGE, TOF_MODES[mode].finalVcsel);  if (st) goto done;
    g_tof_init = 8; st = VL53L0X_SetDeviceMode(dev, VL53L0X_DEVICEMODE_CONTINUOUS_RANGING);  if (st) goto done;
    g_tof_init = 9; st = VL53L0X_StartMeasurement(dev);                                      if (st) goto done;
    g_tof_init = 0;                        /* all steps passed */

done:
    g_tof_status = (uint8_t)st;
    return g_tof_init;
}

void Tof_Measure(void)
{
    uint8_t ready = 0;
    VL53L0X_RangingMeasurementData_t meas;

    if (VL53L0X_GetMeasurementDataReady(dev, &ready) != VL53L0X_ERROR_NONE || !ready)
        return;

    if (VL53L0X_GetRangingMeasurementData(dev, &meas) != VL53L0X_ERROR_NONE) {
        VL53L0X_ClearInterruptMask(dev, 0);
        return;
    }
    VL53L0X_ClearInterruptMask(dev, 0);

    g_tof_status = meas.RangeStatus;
    float raw_mm = (float)meas.RangeMilliMeter;
    g_tof_raw_mm = raw_mm;

    /* gate: RangeStatus 0 = valid; reject out-of-window (wall/crosstalk) */
    if (meas.RangeStatus != 0) return;
    if (raw_mm < TOF_RAW_MIN_MM || raw_mm > TOF_RAW_MAX_MM) return;

    float raw_cm = Median3(raw_mm) * 0.1f;

    float h = TOF_ZERO_CM - raw_cm;     /* laser geometry (see config.h TOF_ZERO_CM) */
    if (h < 0.0f) h = 0.0f;
    current_height = HeightFilter(h);
    height_updated = 1;
    height_update_tick = uwTick;
    g_height_sample_count++;            /* frame-rate telemetry (F: in heartbeat) */
}
