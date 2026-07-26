#ifndef BSP_ENCODER_H
#define BSP_ENCODER_H

#include <stdint.h>

typedef struct {
    int32_t leftTotal;
    int32_t rightTotal;
    int32_t leftDelta;
    int32_t rightDelta;
    uint32_t leftInvalid;
    uint32_t rightInvalid;
} BspEncoderSample;

void bsp_encoder_init(void);
BspEncoderSample bsp_encoder_sample_window(void);
void bsp_encoder_reset(void);

#endif
