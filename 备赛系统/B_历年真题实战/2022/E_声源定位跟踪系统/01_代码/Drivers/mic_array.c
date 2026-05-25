#include "mic_array.h"
#include <string.h>
static int16_t s_m[MIC_NUM][TDOA_FRAME_LEN];
static volatile uint8_t s_ready = 0;
void mic_init(void){ s_ready = 0; }
void mic_start(void){}
uint8_t mic_get_frame(int16_t *m1, int16_t *m2, int16_t *m3, int16_t *m4, uint32_t len)
{
    if (!s_ready || len < TDOA_FRAME_LEN || !m1 || !m2 || !m3 || !m4) return 0;
    memcpy(m1, s_m[0], TDOA_FRAME_LEN * sizeof(int16_t));
    memcpy(m2, s_m[1], TDOA_FRAME_LEN * sizeof(int16_t));
    memcpy(m3, s_m[2], TDOA_FRAME_LEN * sizeof(int16_t));
    memcpy(m4, s_m[3], TDOA_FRAME_LEN * sizeof(int16_t));
    s_ready = 0;
    return 1;
}
