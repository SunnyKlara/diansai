#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>
#include <string.h>
#include "main.h"
#include "tim.h"
#include "gpio.h"
#include "oled.h"

// 引脚定义
#define OLED_CLK_PORT   GPIOB
#define OLED_MOSI_PORT  GPIOB
#define OLED_RES_PORT   GPIOE
#define OLED_DC_PORT    GPIOG
#define OLED_CS_PORT    GPIOG

#define OLED_CLK_PIN    GPIO_PIN_3
#define OLED_MOSI_PIN   GPIO_PIN_5
#define OLED_RES_PIN    GPIO_PIN_6
#define OLED_DC_PIN     GPIO_PIN_14
#define OLED_CS_PIN     GPIO_PIN_13

// 宏定义-高低电平控制
#define OLED_CLK_LOW    HAL_GPIO_WritePin(OLED_CLK_PORT, OLED_CLK_PIN, GPIO_PIN_RESET)
#define OLED_CLK_HIGH   HAL_GPIO_WritePin(OLED_CLK_PORT, OLED_CLK_PIN, GPIO_PIN_SET)
#define OLED_MOSI_LOW   HAL_GPIO_WritePin(OLED_MOSI_PORT, OLED_MOSI_PIN, GPIO_PIN_RESET)
#define OLED_MOSI_HIGH  HAL_GPIO_WritePin(OLED_MOSI_PORT, OLED_MOSI_PIN, GPIO_PIN_SET)
#define OLED_RES_LOW    HAL_GPIO_WritePin(OLED_RES_PORT, OLED_RES_PIN, GPIO_PIN_RESET)
#define OLED_RES_HIGH   HAL_GPIO_WritePin(OLED_RES_PORT, OLED_RES_PIN, GPIO_PIN_SET)
#define OLED_DC_LOW     HAL_GPIO_WritePin(OLED_DC_PORT, OLED_DC_PIN, GPIO_PIN_RESET)    // 命令
#define OLED_DC_HIGH    HAL_GPIO_WritePin(OLED_DC_PORT, OLED_DC_PIN, GPIO_PIN_SET)      // 数据
#define OLED_CS_LOW     HAL_GPIO_WritePin(OLED_CS_PORT, OLED_CS_PIN, GPIO_PIN_RESET)
#define OLED_CS_HIGH    HAL_GPIO_WritePin(OLED_CS_PORT, OLED_CS_PIN, GPIO_PIN_SET)
#define OLED_CMD  0	//Ð´ÃüÁî
#define OLED_DATA 1	//Ð´Êý¾Ý

// 函数声明
void OLED_Init(void);
void OLED_Clear(void);
void OLED_UpdateScreen(void);
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 size1);
void OLED_ShowString(u8 x,u8 y,u8 *chr,u8 size1);
void OLED_DrawPoint(uint8_t x, uint8_t y, uint8_t mode);
void OLED_DrawLine(u8 x1,u8 y1,u8 x2,u8 y2);
void OLED_DrawCircle(u8 x,u8 y,u8 r);
void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 size1);
void OLED_ShowChinese(u8 x,u8 y,u8 num,u8 size1);
void OLED_ScrollDisplay(u8 num,u8 space);
void OLED_ShowPicture(u8 x,u8 y,u8 sizex,u8 sizey,u8 BMP[]);
void OLED_InvertRect(u8 x0,u8 y0,u8 x1,u8 y1);
void OLED_ClearBuffer(void);
void OLED_ShowCN16(u8 x,u8 y,u8 idx);
u32 OLED_Pow(u8 m,u8 n);
#endif
