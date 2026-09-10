#ifndef __LCD_H
#define __LCD_H

#include "main.h"

/* 2.8 寸 ILI9341, 竖屏 240x320 */
#define LCD_W      240
#define LCD_H      320

/* FSMC Bank1.sector4 基址 0x6C000000, A10 作为 RS(命令/数据选择)线。
 * 16 位模式下 HADDR[25:1] -> FSMC_A[24:0], 所以 A10 对应地址 bit11。
 * 结构体里 LCD_RAM 偏移 2 字节, 要让它的地址 bit11 = 1, 基址须为 0x800-2 = 0x7FE。 */
#define LCD_BASE        ( ( uint32_t ) ( 0x6C000000UL | 0x000007FEUL ) )

typedef struct
{
    __IO uint16_t LCD_REG;      /* 写命令 / 读状态 */
    __IO uint16_t LCD_RAM;      /* 写数据 / 读数据 */
} LCD_TypeDef;

#define LCD             ( ( LCD_TypeDef * ) LCD_BASE )

/* 背光 PB0 */
#define LCD_BL_GPIO_Port    GPIOB
#define LCD_BL_Pin          GPIO_PIN_0

/* 常用颜色 RGB565 */
#define LCD_COLOR_WHITE     0xFFFF
#define LCD_COLOR_BLACK     0x0000
#define LCD_COLOR_RED       0xF800
#define LCD_COLOR_GREEN     0x07E0
#define LCD_COLOR_BLUE      0x001F
#define LCD_COLOR_YELLOW    0xFFE0
#define LCD_COLOR_CYAN      0x07FF
#define LCD_COLOR_MAGENTA   0xF81F
#define LCD_COLOR_DARK      0x2104

void LCD_Init( void );
void LCD_Clear( uint16_t color );
void LCD_Fill( uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color );
void LCD_DrawPoint( uint16_t x, uint16_t y, uint16_t color );
void LCD_ShowChar( uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg );
void LCD_ShowString( uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg );

#endif /* __LCD_H */
