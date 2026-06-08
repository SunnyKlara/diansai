#ifndef __PATH_GEN_H__
#define __PATH_GEN_H__
#include <stdint.h>

typedef enum { PATH_BORDER = 0, PATH_A4 = 1 } path_type_t;

void path_get_point(path_type_t type, float t01, float rot_deg, float *x_m, float *y_m);
void path_xy_to_angles(float x_m, float y_m, float dist_m, float *yaw_deg, float *pitch_deg);

#endif
