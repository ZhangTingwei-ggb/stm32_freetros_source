#ifndef __LCD_H
#define __LCD_H

#include "main.h"

/* 2.8" ILI9341 panel on the FSMC bus.
 *
 * This driver does three things and nothing else:
 *   LCD_Init()        bring the panel up and clear it
 *   LCD_Clear()       fill the screen with one colour
 *   LCD_ShowString()  draw text
 *
 * Everything a screen normally needs beyond that - panels, boxes, dividers,
 * a second font size - can be built from those. Keeping the driver this small
 * means there is only one thing to understand: how a colour reaches a pixel.
 */

/* ---------------------------------------------------------------------------
 * Panel geometry
 * ---------------------------------------------------------------------------
 * MADCTL (0x36) decides how the frame memory is mapped onto the glass.
 * Measured on THIS board (2026-09-12) - note that MV comes out the opposite
 * way round from a plain ILI9341 datasheet reading:
 *
 *     0xA8 -> 240 x 320, upright        <- this one
 *     0x68 -> 240 x 320, upside down
 *     0xC8 -> 320 x 240
 *     0x08 -> 320 x 240, upside down
 *
 * If you switch to the 0xC8 / 0x08 pair, swap LCD_W and LCD_H as well.
 * ------------------------------------------------------------------------- */
#define LCD_MADCTL      0xA8U
#define LCD_W           240
#define LCD_H           320

/* ---------------------------------------------------------------------------
 * Panel colour polarity
 * ---------------------------------------------------------------------------
 * This panel is normally-white, so without the right setting the controller
 * maps 0x0000 to WHITE and every colour comes out inverted - a dark theme
 * turns into black text on a white page.
 *
 * Test: fill the screen with 0x0000. White means the inversion is needed,
 * black means it is not. Measured here: white, so LCD_INVERSION_ON.
 * ------------------------------------------------------------------------- */
#define LCD_INVERSION_OFF   0x20U
#define LCD_INVERSION_ON    0x21U
#define LCD_INVERSION       LCD_INVERSION_ON

/* ---------------------------------------------------------------------------
 * FSMC address decoding - why the magic 0x7FE?
 * ---------------------------------------------------------------------------
 * The panel is wired to FSMC Bank1 / sector4, base 0x6C000000. Address line
 * A10 (PG0) is the RS (register select) line:
 *
 *     A10 = 0 -> the write goes to the command register
 *     A10 = 1 -> the write goes to the data register
 *
 * In 16-bit bus mode the FSMC shifts HADDR right by one, so FSMC_A10 is
 * HADDR bit 11, i.e. byte offset 0x800. LCD_RAM sits at struct offset 2, so
 * for it to have bit 11 set the struct base must be 0x800 - 2 = 0x7FE:
 *
 *     &LCD->LCD_REG = 0x6C000000 | 0x7FE + 0 -> bit 11 = 0 -> command
 *     &LCD->LCD_RAM = 0x6C000000 | 0x7FE + 2 -> bit 11 = 1 -> data
 * ------------------------------------------------------------------------- */
#define LCD_BASE    ( ( uint32_t ) ( 0x6C000000UL | 0x000007FEUL ) )

typedef struct
{
    __IO uint16_t LCD_REG;      /* write command */
    __IO uint16_t LCD_RAM;      /* write data    */
} LCD_TypeDef;

#define LCD         ( ( LCD_TypeDef * ) LCD_BASE )

/* Backlight: PB0, high = on */
#define LCD_BL_GPIO_Port    GPIOB
#define LCD_BL_Pin          GPIO_PIN_0

/* ---------------------------------------------------------------------------
 * Colours, RGB565
 * ---------------------------------------------------------------------------
 *   bit15..11 = red (5), bit10..5 = green (6), bit4..0 = blue (5)
 *   r5 = round( R * 31 / 255 ), g6 = round( G * 63 / 255 ), b5 = round( B * 31 / 255 )
 *
 * The source colour is written next to each value so the theme can be re-tuned
 * without guessing how the channels were squeezed into 565.
 * ------------------------------------------------------------------------- */
#define LCD_COLOR_BG        0x0000      /* #000000  page background          */
#define LCD_COLOR_TEXT      0xE77E      /* #E6EDF3  headings, normal text    */
#define LCD_COLOR_LABEL     0x8CB3      /* #8B949E  captions, secondary text */
#define LCD_COLOR_TEMP      0xFD8A      /* #FFB454  temperature reading      */
#define LCD_COLOR_HUMI      0x5F38      /* #5EE6C4  humidity reading         */
#define LCD_COLOR_OK        0x6E6F      /* #6FCF7F  status: healthy          */
#define LCD_COLOR_WARN      0xE588      /* #E3B341  status: waiting          */
#define LCD_COLOR_ERR       0xEB8E      /* #F07070  status: sensor missing   */

/* Bring the panel up and clear it to LCD_COLOR_BG.
 * MX_FSMC_Init() must already have run. */
void LCD_Init( void );

/* Fill the whole screen with one colour. */
void LCD_Clear( uint16_t color );

/* Draw a string at (x, y) with the top-left corner of the first character.
 * The 16x24 font covers ASCII 0x20..0x7E; anything else is drawn as '?'.
 * Characters are written onto LCD_COLOR_BG, so text always erases whatever
 * was underneath it - just make sure a redrawn string is not shorter than the
 * one it replaces, or pad it with trailing spaces. */
void LCD_ShowString( uint16_t x, uint16_t y, const char *str, uint16_t color );

#endif /* __LCD_H */
