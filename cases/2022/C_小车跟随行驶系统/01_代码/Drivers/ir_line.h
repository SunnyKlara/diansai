#ifndef __IR_LINE_H__
#define __IR_LINE_H__
#include <stdint.h>
void ir_line_init(void);
void ir_line_update_isr(const uint16_t *raw);
float ir_line_get_error(void);
uint8_t ir_line_at_stop_marker(void);
#endif
