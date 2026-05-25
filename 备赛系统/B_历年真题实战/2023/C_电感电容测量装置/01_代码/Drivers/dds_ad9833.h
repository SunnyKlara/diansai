#ifndef __DDS_AD9833_H__
#define __DDS_AD9833_H__
#include <stdint.h>
void dds_init(void);
void dds_set_freq(uint32_t hz);
void dds_set_wave(uint8_t mode);
#endif
