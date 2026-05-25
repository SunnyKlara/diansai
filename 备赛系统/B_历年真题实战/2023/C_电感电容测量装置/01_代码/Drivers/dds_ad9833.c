/**
 * @file    dds_ad9833.c
 * @brief   AD9833 SPI DDS 信号源占位
 */
#include "dds_ad9833.h"
void dds_init(void) {}
void dds_set_freq(uint32_t hz){ (void)hz; }
void dds_set_wave(uint8_t mode){ (void)mode; }   /* 0=Sine, 1=Tri, 2=Square */
