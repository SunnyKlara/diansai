#include "BSP/bsp_encoder.h"

#include "ti_msp_dl_config.h"

static const int8_t g_quadratureTable[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
};

/* Normalize mirrored wheel encoders so vehicle-forward is positive. */
static const int8_t g_leftPolarity = -1;
static const int8_t g_rightPolarity = 1;

static volatile int32_t g_leftCount;
static volatile int32_t g_rightCount;
static volatile uint32_t g_leftInvalid;
static volatile uint32_t g_rightInvalid;
static uint8_t g_leftPrevious;
static uint8_t g_rightPrevious;
static int32_t g_leftLastSample;
static int32_t g_rightLastSample;

static uint8_t read_left_state(void)
{
    uint32_t pins = DL_GPIO_readPins(GPIOA,
        GPIO_ENCODER_LEFT_A_PIN | GPIO_ENCODER_LEFT_B_PIN);
    uint8_t a = ((pins & GPIO_ENCODER_LEFT_A_PIN) != 0U) ? 1U : 0U;
    uint8_t b = ((pins & GPIO_ENCODER_LEFT_B_PIN) != 0U) ? 1U : 0U;
    return (uint8_t) ((a << 1U) | b);
}

static uint8_t read_right_state(void)
{
    uint32_t pins = DL_GPIO_readPins(GPIOB,
        GPIO_ENCODER_RIGHT_A_PIN | GPIO_ENCODER_RIGHT_B_PIN);
    uint8_t a = ((pins & GPIO_ENCODER_RIGHT_A_PIN) != 0U) ? 1U : 0U;
    uint8_t b = ((pins & GPIO_ENCODER_RIGHT_B_PIN) != 0U) ? 1U : 0U;
    return (uint8_t) ((a << 1U) | b);
}

static void update_left(void)
{
    uint8_t current = read_left_state();
    uint8_t transition = (uint8_t) ((g_leftPrevious << 2U) | current);
    int8_t delta = g_quadratureTable[transition];

    if ((delta == 0) && (current != g_leftPrevious)) {
        g_leftInvalid++;
    }
    g_leftCount += delta;
    g_leftPrevious = current;
}

static void update_right(void)
{
    uint8_t current = read_right_state();
    uint8_t transition = (uint8_t) ((g_rightPrevious << 2U) | current);
    int8_t delta = g_quadratureTable[transition];

    if ((delta == 0) && (current != g_rightPrevious)) {
        g_rightInvalid++;
    }
    g_rightCount += delta;
    g_rightPrevious = current;
}

void bsp_encoder_init(void)
{
    g_leftCount = 0;
    g_rightCount = 0;
    g_leftInvalid = 0;
    g_rightInvalid = 0;
    g_leftLastSample = 0;
    g_rightLastSample = 0;
    g_leftPrevious = read_left_state();
    g_rightPrevious = read_right_state();

    DL_GPIO_clearInterruptStatus(GPIOA,
        GPIO_ENCODER_LEFT_A_PIN | GPIO_ENCODER_LEFT_B_PIN);
    DL_GPIO_clearInterruptStatus(GPIOB,
        GPIO_ENCODER_RIGHT_A_PIN | GPIO_ENCODER_RIGHT_B_PIN);
    NVIC_ClearPendingIRQ(GPIO_ENCODER_GPIOA_INT_IRQN);
    NVIC_ClearPendingIRQ(GPIO_ENCODER_GPIOB_INT_IRQN);
    NVIC_EnableIRQ(GPIO_ENCODER_GPIOA_INT_IRQN);
    NVIC_EnableIRQ(GPIO_ENCODER_GPIOB_INT_IRQN);
}

BspEncoderSample bsp_encoder_sample_window(void)
{
    BspEncoderSample sample;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    sample.leftTotal = g_leftCount * g_leftPolarity;
    sample.rightTotal = g_rightCount * g_rightPolarity;
    sample.leftInvalid = g_leftInvalid;
    sample.rightInvalid = g_rightInvalid;
    if (primask == 0U) {
        __enable_irq();
    }

    sample.leftDelta = sample.leftTotal - g_leftLastSample;
    sample.rightDelta = sample.rightTotal - g_rightLastSample;
    g_leftLastSample = sample.leftTotal;
    g_rightLastSample = sample.rightTotal;
    return sample;
}

void bsp_encoder_reset(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_leftCount = 0;
    g_rightCount = 0;
    g_leftInvalid = 0;
    g_rightInvalid = 0;
    g_leftLastSample = 0;
    g_rightLastSample = 0;
    g_leftPrevious = read_left_state();
    g_rightPrevious = read_right_state();
    if (primask == 0U) {
        __enable_irq();
    }
}

void GROUP1_IRQHandler(void)
{
    uint32_t leftStatus = DL_GPIO_getEnabledInterruptStatus(GPIOA,
        GPIO_ENCODER_LEFT_A_PIN | GPIO_ENCODER_LEFT_B_PIN);
    uint32_t rightStatus = DL_GPIO_getEnabledInterruptStatus(GPIOB,
        GPIO_ENCODER_RIGHT_A_PIN | GPIO_ENCODER_RIGHT_B_PIN);

    if (leftStatus != 0U) {
        update_left();
        DL_GPIO_clearInterruptStatus(GPIOA, leftStatus);
    }
    if (rightStatus != 0U) {
        update_right();
        DL_GPIO_clearInterruptStatus(GPIOB, rightStatus);
    }
}
