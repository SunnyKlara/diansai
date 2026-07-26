#include "BSP/bsp_gray.h"

#include "ti_msp_dl_config.h"

#define GRAY_ADC_AVERAGE_SAMPLES (4U)
#define GRAY_MIN_CALIBRATION_SPAN (100U)
#define GRAY_LINE_DETECT_MIN (650U)

static uint16_t g_values[BSP_GRAY_CHANNEL_COUNT];
static uint16_t g_calibrationMin[BSP_GRAY_CHANNEL_COUNT];
static uint16_t g_calibrationMax[BSP_GRAY_CHANNEL_COUNT];
static bool g_filterReady;
static bool g_calibrationActive;
static bool g_calibrationReady;

/* 2026-07-10 map sweep at the installed sensor height; low ADC is black. */
static const uint16_t g_defaultCalibrationMin[BSP_GRAY_CHANNEL_COUNT] = {
    18U, 32U, 57U, 26U, 32U, 41U, 38U, 58U,
};
static const uint16_t g_defaultCalibrationMax[BSP_GRAY_CHANNEL_COUNT] = {
    398U, 1076U, 624U, 874U, 825U, 644U, 801U, 527U,
};

static void select_channel(uint8_t channel)
{
    uint32_t addressPins = GPIO_GRAY_AD0_PIN | GPIO_GRAY_AD1_PIN |
                           GPIO_GRAY_AD2_PIN;
    uint32_t setPins = 0U;

    if ((channel & 0x01U) != 0U) {
        setPins |= GPIO_GRAY_AD0_PIN;
    }
    if ((channel & 0x02U) != 0U) {
        setPins |= GPIO_GRAY_AD1_PIN;
    }
    if ((channel & 0x04U) != 0U) {
        setPins |= GPIO_GRAY_AD2_PIN;
    }

    DL_GPIO_clearPins(GPIO_GRAY_PORT, addressPins);
    DL_GPIO_setPins(GPIO_GRAY_PORT, setPins);
}

static uint16_t read_adc(void)
{
    const uint32_t readyMask = DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED;

    DL_ADC12_clearInterruptStatus(GRAY_ADC_INST, readyMask);
    DL_ADC12_startConversion(GRAY_ADC_INST);
    while ((DL_ADC12_getRawInterruptStatus(GRAY_ADC_INST, readyMask) &
            readyMask) == 0U) {
    }

    uint16_t value = DL_ADC12_getMemResult(GRAY_ADC_INST, GRAY_ADC_ADCMEM_0);
    DL_ADC12_clearInterruptStatus(GRAY_ADC_INST, readyMask);
    DL_ADC12_enableConversions(GRAY_ADC_INST);
    return value;
}

void bsp_gray_init(void)
{
    DL_GPIO_clearPins(GPIO_GRAY_PORT, GPIO_GRAY_EN_PIN |
        GPIO_GRAY_AD0_PIN | GPIO_GRAY_AD1_PIN | GPIO_GRAY_AD2_PIN);
    for (uint8_t i = 0; i < BSP_GRAY_CHANNEL_COUNT; i++) {
        g_values[i] = 0;
        g_calibrationMin[i] = g_defaultCalibrationMin[i];
        g_calibrationMax[i] = g_defaultCalibrationMax[i];
    }
    g_filterReady = false;
    g_calibrationActive = false;
    g_calibrationReady = true;
}

void bsp_gray_scan(void)
{
    for (uint8_t i = 0; i < BSP_GRAY_CHANNEL_COUNT; i++) {
        uint32_t sum = 0;

        select_channel(i);
        delay_cycles(800U);
        (void) read_adc();
        for (uint8_t sample = 0; sample < GRAY_ADC_AVERAGE_SAMPLES; sample++) {
            sum += read_adc();
        }

        uint16_t averaged = (uint16_t) (sum / GRAY_ADC_AVERAGE_SAMPLES);
        if (g_filterReady) {
            g_values[i] = (uint16_t) (((uint32_t) g_values[i] * 3U +
                                       averaged + 2U) / 4U);
        } else {
            g_values[i] = averaged;
        }

        if (g_calibrationActive) {
            if (g_values[i] < g_calibrationMin[i]) {
                g_calibrationMin[i] = g_values[i];
            }
            if (g_values[i] > g_calibrationMax[i]) {
                g_calibrationMax[i] = g_values[i];
            }
        }
    }
    g_filterReady = true;
}

const uint16_t *bsp_gray_values(void)
{
    return g_values;
}

void bsp_gray_calibration_start(void)
{
    for (uint8_t i = 0; i < BSP_GRAY_CHANNEL_COUNT; i++) {
        g_calibrationMin[i] = g_values[i];
        g_calibrationMax[i] = g_values[i];
    }
    g_calibrationReady = false;
    g_calibrationActive = true;
}

uint8_t bsp_gray_calibration_valid_channels(void)
{
    uint8_t valid = 0;

    for (uint8_t i = 0; i < BSP_GRAY_CHANNEL_COUNT; i++) {
        if ((g_calibrationMax[i] - g_calibrationMin[i]) >=
            GRAY_MIN_CALIBRATION_SPAN) {
            valid++;
        }
    }
    return valid;
}

bool bsp_gray_calibration_stop(void)
{
    g_calibrationActive = false;
    g_calibrationReady =
        (bsp_gray_calibration_valid_channels() == BSP_GRAY_CHANNEL_COUNT);
    return g_calibrationReady;
}

bool bsp_gray_calibration_active(void)
{
    return g_calibrationActive;
}

bool bsp_gray_is_calibrated(void)
{
    return g_calibrationReady;
}

const uint16_t *bsp_gray_calibration_min(void)
{
    return g_calibrationMin;
}

const uint16_t *bsp_gray_calibration_max(void)
{
    return g_calibrationMax;
}

BspGrayLine bsp_gray_line(void)
{
    static const int16_t positions[BSP_GRAY_CHANNEL_COUNT] = {
        -3500, -2500, -1500, -500, 500, 1500, 2500, 3500,
    };
    BspGrayLine line = {0};
    int32_t weighted = 0;
    uint16_t strongest = 0;

    if (!g_calibrationReady) {
        return line;
    }

    for (uint8_t i = 0; i < BSP_GRAY_CHANNEL_COUNT; i++) {
        uint16_t minimum = g_calibrationMin[i];
        uint16_t maximum = g_calibrationMax[i];
        uint16_t value = g_values[i];
        uint16_t rawBlackness;
        uint16_t weight;

        if (value <= minimum) {
            rawBlackness = 1000U;
        } else if (value >= maximum) {
            rawBlackness = 0U;
        } else {
            rawBlackness = (uint16_t) (
                ((uint32_t) (maximum - value) * 1000U) /
                (maximum - minimum));
        }

        weight = (uint16_t) (
            (((uint32_t) rawBlackness * rawBlackness) + 500U) / 1000U);
        line.normalized[i] = weight;
        line.strength = (uint16_t) (line.strength + weight);
        weighted += (int32_t) weight * positions[i];
        if (rawBlackness > strongest) {
            strongest = rawBlackness;
        }
    }

    if (line.strength > 0U) {
        line.error = (int16_t) (weighted / line.strength);
    }
    line.valid = (strongest >= GRAY_LINE_DETECT_MIN) &&
                 (line.strength >= 300U);
    return line;
}
