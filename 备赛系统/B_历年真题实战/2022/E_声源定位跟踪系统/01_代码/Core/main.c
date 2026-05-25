/**
 * @file    main.c
 */
#include "../config.h"
#include "../Drivers/mic_array.h"
#include "../Drivers/servo_pan_tilt.h"
#include "../Algorithm/tdoa.h"

static int16_t m1[TDOA_FRAME_LEN], m2[TDOA_FRAME_LEN];
static int16_t m3[TDOA_FRAME_LEN], m4[TDOA_FRAME_LEN];
static tdoa_result_t s_r;

int main(void)
{
    mic_init(); servo_init(); tdoa_init();
    mic_start();
    while (1) {
        if (mic_get_frame(m1, m2, m3, m4, TDOA_FRAME_LEN)) {
            tdoa_locate(m1, m2, m3, m4, TDOA_FRAME_LEN, (float)MIC_FS_HZ, &s_r);
            servo_set_target(s_r.angle_deg, 0.0f);
        }
    }
}
