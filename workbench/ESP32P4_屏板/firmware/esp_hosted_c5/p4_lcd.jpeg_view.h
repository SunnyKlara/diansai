// P4 hardware JPEG decode -> LVGL image, for the plan-D "receive and display" half.
//
// This deliberately decodes a JPEG that is EMBEDDED IN THE FIRMWARE (a real frame
// produced by the K230's hardware encoder, see workbench/ESP32P4_屏板/README.md
// §10.8). Reason: the network hop K230->P4 is blocked on a separate problem, so the
// display half is proven with the network taken out of the picture -- one variable.
// Swapping the source from "embedded array" to "socket buffer" later is a two-line
// change; everything else (decoder engine, buffers, LVGL descriptor) stays.
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Decodes the embedded frame once. Safe to call before or after lvgl_port init.
// Returns false if the decoder or the buffers could not be set up; jpeg_view_status()
// then explains why (and is short enough to print on the panel).
bool jpeg_view_init(void);

// NULL until jpeg_view_init() succeeds.
const lv_image_dsc_t *jpeg_view_dsc(void);

// One-line human readable result, e.g. "640x480 6625B->614400B 7.4ms".
const char *jpeg_view_status(void);

#ifdef __cplusplus
}
#endif
