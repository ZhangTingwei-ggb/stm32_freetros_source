#include "lcd.h"
#include "lcdfont.h"

/* ---------------------------------------------------------------------------
 * The two addresses the panel exposes on the bus
 * ---------------------------------------------------------------------------
 * Writing LCD_REG sends a command, writing LCD_RAM pushes one parameter or one
 * pixel. Which one you hit is decided purely by address bit 11 (see lcd.h), so
 * these two functions are the whole hardware interface.
 * ------------------------------------------------------------------------- */
static void LCD_WriteCmd( uint16_t cmd )
{
    LCD->LCD_REG = cmd;
}

static void LCD_WriteData( uint16_t data )
{
    LCD->LCD_RAM = data;
}

/* ---------------------------------------------------------------------------
 * Open a rectangle
 * ---------------------------------------------------------------------------
 * 0x2A sets the column range, 0x2B the page range, and 0x2C says "every write
 * from now on is a pixel". The controller then walks the rectangle by itself,
 * so a whole area is filled without ever sending a coordinate again.
 * ------------------------------------------------------------------------- */
static void LCD_SetWindow( uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1 )
{
    LCD_WriteCmd( 0x2A );                       /* column address set */
    LCD_WriteData( x0 >> 8 ); LCD_WriteData( x0 & 0xFF );
    LCD_WriteData( x1 >> 8 ); LCD_WriteData( x1 & 0xFF );

    LCD_WriteCmd( 0x2B );                       /* page address set */
    LCD_WriteData( y0 >> 8 ); LCD_WriteData( y0 & 0xFF );
    LCD_WriteData( y1 >> 8 ); LCD_WriteData( y1 & 0xFF );

    LCD_WriteCmd( 0x2C );                       /* memory write */
}

/* ---------------------------------------------------------------------------
 * Panel configuration
 * ---------------------------------------------------------------------------
 * Almost all of this is the vendor's magic numbers; there is nothing to
 * understand and no reason to touch it. Only three settings matter, and all
 * three come from lcd.h:
 *
 *   0x36 MADCTL      orientation and RGB order
 *   0x3A COLMOD      0x55 = 16 bits per pixel, matching the FSMC bus
 *   0x20 / 0x21      colour polarity of the panel
 * ------------------------------------------------------------------------- */
static void ILI9341_Init( void )
{
    LCD_WriteCmd( 0x01 );                       /* software reset */
    HAL_Delay( 120 );

    LCD_WriteCmd( 0xCF );
    LCD_WriteData( 0x00 ); LCD_WriteData( 0xC1 ); LCD_WriteData( 0x30 );

    LCD_WriteCmd( 0xED );
    LCD_WriteData( 0x64 ); LCD_WriteData( 0x03 ); LCD_WriteData( 0x12 ); LCD_WriteData( 0x81 );

    LCD_WriteCmd( 0xE8 );
    LCD_WriteData( 0x85 ); LCD_WriteData( 0x00 ); LCD_WriteData( 0x78 );

    LCD_WriteCmd( 0xCB );
    LCD_WriteData( 0x39 ); LCD_WriteData( 0x2C ); LCD_WriteData( 0x00 );
    LCD_WriteData( 0x34 ); LCD_WriteData( 0x02 );

    LCD_WriteCmd( 0xF7 );
    LCD_WriteData( 0x20 );

    LCD_WriteCmd( 0xEA );
    LCD_WriteData( 0x00 ); LCD_WriteData( 0x00 );

    LCD_WriteCmd( 0xC0 );                       /* power control 1 */
    LCD_WriteData( 0x1B );

    LCD_WriteCmd( 0xC1 );                       /* power control 2 */
    LCD_WriteData( 0x01 );

    LCD_WriteCmd( 0xC5 );                       /* VCOM control 1 */
    LCD_WriteData( 0x30 ); LCD_WriteData( 0x30 );

    LCD_WriteCmd( 0xC7 );                       /* VCOM control 2 */
    LCD_WriteData( 0xB7 );

    LCD_WriteCmd( 0x36 );                       /* orientation, see lcd.h */
    LCD_WriteData( LCD_MADCTL );

    LCD_WriteCmd( 0x3A );                       /* pixel format */
    LCD_WriteData( 0x55 );                      /* 16 bit / pixel */

    LCD_WriteCmd( LCD_INVERSION );              /* colour polarity, see lcd.h */

    LCD_WriteCmd( 0xB1 );                       /* frame rate */
    LCD_WriteData( 0x00 ); LCD_WriteData( 0x1B );

    LCD_WriteCmd( 0xB6 );                       /* display function */
    LCD_WriteData( 0x0A ); LCD_WriteData( 0xA2 );

    LCD_WriteCmd( 0xF2 );                       /* 3 gamma function */
    LCD_WriteData( 0x00 );

    LCD_WriteCmd( 0x26 );                       /* gamma curve */
    LCD_WriteData( 0x01 );

    LCD_WriteCmd( 0xE0 );                       /* positive gamma */
    LCD_WriteData( 0x0F ); LCD_WriteData( 0x31 ); LCD_WriteData( 0x2B ); LCD_WriteData( 0x0C );
    LCD_WriteData( 0x0E ); LCD_WriteData( 0x08 ); LCD_WriteData( 0x4E ); LCD_WriteData( 0xF1 );
    LCD_WriteData( 0x37 ); LCD_WriteData( 0x07 ); LCD_WriteData( 0x10 ); LCD_WriteData( 0x03 );
    LCD_WriteData( 0x0E ); LCD_WriteData( 0x09 ); LCD_WriteData( 0x00 );

    LCD_WriteCmd( 0xE1 );                       /* negative gamma */
    LCD_WriteData( 0x00 ); LCD_WriteData( 0x0E ); LCD_WriteData( 0x14 ); LCD_WriteData( 0x03 );
    LCD_WriteData( 0x11 ); LCD_WriteData( 0x07 ); LCD_WriteData( 0x31 ); LCD_WriteData( 0xC1 );
    LCD_WriteData( 0x48 ); LCD_WriteData( 0x08 ); LCD_WriteData( 0x0F ); LCD_WriteData( 0x0C );
    LCD_WriteData( 0x31 ); LCD_WriteData( 0x36 ); LCD_WriteData( 0x0F );

    LCD_WriteCmd( 0x11 );                       /* sleep out */
    HAL_Delay( 120 );                           /* wait for the charge pump */

    LCD_WriteCmd( 0x29 );                       /* display on */
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void LCD_Init( void )
{
    ILI9341_Init();

    HAL_GPIO_WritePin( LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET );

    LCD_Clear( LCD_COLOR_BG );
}

void LCD_Clear( uint16_t color )
{
    LCD_SetWindow( 0, 0, LCD_W - 1, LCD_H - 1 );

    for( uint32_t i = 0; i < ( ( uint32_t ) LCD_W * LCD_H ); i++ )
    {
        LCD_WriteData( color );
    }
}

/* ---------------------------------------------------------------------------
 * One character
 * ---------------------------------------------------------------------------
 * The font stores each glyph as 24 rows of 2 bytes = 16 bits, with the MSB
 * being the leftmost pixel. So a row is decoded by testing one bit at a time:
 * bit set -> ink, bit clear -> background.
 * ------------------------------------------------------------------------- */
static void LCD_ShowChar( uint16_t x, uint16_t y, char ch, uint16_t color )
{
    if( ch < 0x20 || ch > 0x7E )
    {
        ch = '?';
    }

    const uint8_t *p = ascii_16x24[ ( uint8_t ) ch - 0x20 ];

    /* One window per character, then stream its 16 x 24 pixels into it */
    LCD_SetWindow( x, y, x + FONT_W - 1U, y + FONT_H - 1U );

    for( uint8_t row = 0; row < FONT_H; row++ )
    {
        uint16_t bits = ( ( uint16_t ) p[ row * 2 ] << 8 ) | p[ row * 2 + 1 ];

        for( uint8_t col = 0; col < FONT_W; col++ )
        {
            LCD_WriteData( ( bits & ( 0x8000U >> col ) ) ? color : LCD_COLOR_BG );
        }
    }
}

void LCD_ShowString( uint16_t x, uint16_t y, const char *str, uint16_t color )
{
    while( *str )
    {
        LCD_ShowChar( x, y, *str, color );
        x += FONT_W;
        str++;
    }
}
