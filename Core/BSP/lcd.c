#include "lcd.h"
#include "lcdfont.h"

/* ------------------------------------------------------------------ */
/* 底层: FSMC 读写                                                     */
/* ------------------------------------------------------------------ */

static inline void LCD_WriteCmd( uint16_t cmd )
{
    LCD->LCD_REG = cmd;
}

static inline void LCD_WriteData( uint16_t data )
{
    LCD->LCD_RAM = data;
}

static inline uint16_t LCD_ReadData( void )
{
    return LCD->LCD_RAM;
}

static void LCD_SetWindow( uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye )
{
    LCD_WriteCmd( 0x2A );                       /* Column address set */
    LCD_WriteData( xs >> 8 ); LCD_WriteData( xs & 0xFF );
    LCD_WriteData( xe >> 8 ); LCD_WriteData( xe & 0xFF );

    LCD_WriteCmd( 0x2B );                       /* Page address set */
    LCD_WriteData( ys >> 8 ); LCD_WriteData( ys & 0xFF );
    LCD_WriteData( ye >> 8 ); LCD_WriteData( ye & 0xFF );

    LCD_WriteCmd( 0x2C );                       /* Memory write */
}

/* ------------------------------------------------------------------ */
/* ILI9341 初始化序列                                                  */
/* ------------------------------------------------------------------ */

static void ILI9341_Init( void )
{
    LCD_WriteCmd( 0x01 );                       /* Software reset */
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

    LCD_WriteCmd( 0xC0 );                       /* Power control 1 */
    LCD_WriteData( 0x1B );

    LCD_WriteCmd( 0xC1 );                       /* Power control 2 */
    LCD_WriteData( 0x01 );

    LCD_WriteCmd( 0xC5 );                       /* VCOM control 1 */
    LCD_WriteData( 0x30 ); LCD_WriteData( 0x30 );

    LCD_WriteCmd( 0xC7 );                       /* VCOM control 2 */
    LCD_WriteData( 0xB7 );

    LCD_WriteCmd( 0x36 );                       /* Memory access control */
    LCD_WriteData( 0x08 );                      /* 竖屏, 自上而下, BGR 顺序 */

    LCD_WriteCmd( 0x3A );                       /* Pixel format */
    LCD_WriteData( 0x55 );                      /* 16 bit / pixel (RGB565) */

    LCD_WriteCmd( 0xB1 );                       /* Frame rate control */
    LCD_WriteData( 0x00 ); LCD_WriteData( 0x1B );

    LCD_WriteCmd( 0xB6 );                       /* Display function control */
    LCD_WriteData( 0x0A ); LCD_WriteData( 0xA2 );

    LCD_WriteCmd( 0xF2 );                       /* Enable 3 gamma */
    LCD_WriteData( 0x00 );

    LCD_WriteCmd( 0x26 );                       /* Gamma set */
    LCD_WriteData( 0x01 );

    LCD_WriteCmd( 0xE0 );                       /* Positive gamma correction */
    LCD_WriteData( 0x0F ); LCD_WriteData( 0x31 ); LCD_WriteData( 0x2B ); LCD_WriteData( 0x0C );
    LCD_WriteData( 0x0E ); LCD_WriteData( 0x08 ); LCD_WriteData( 0x4E ); LCD_WriteData( 0xF1 );
    LCD_WriteData( 0x37 ); LCD_WriteData( 0x07 ); LCD_WriteData( 0x10 ); LCD_WriteData( 0x03 );
    LCD_WriteData( 0x0E ); LCD_WriteData( 0x09 ); LCD_WriteData( 0x00 );

    LCD_WriteCmd( 0xE1 );                       /* Negative gamma correction */
    LCD_WriteData( 0x00 ); LCD_WriteData( 0x0E ); LCD_WriteData( 0x14 ); LCD_WriteData( 0x03 );
    LCD_WriteData( 0x11 ); LCD_WriteData( 0x07 ); LCD_WriteData( 0x31 ); LCD_WriteData( 0xC1 );
    LCD_WriteData( 0x48 ); LCD_WriteData( 0x08 ); LCD_WriteData( 0x0F ); LCD_WriteData( 0x0C );
    LCD_WriteData( 0x31 ); LCD_WriteData( 0x36 ); LCD_WriteData( 0x0F );

    LCD_WriteCmd( 0x11 );                       /* Sleep out */
    HAL_Delay( 120 );

    LCD_WriteCmd( 0x29 );                       /* Display on */
}

/* ------------------------------------------------------------------ */
/* 对外接口                                                            */
/* ------------------------------------------------------------------ */

void LCD_Init( void )
{
    ILI9341_Init();

    /* 打开背光 */
    HAL_GPIO_WritePin( LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET );

    LCD_Clear( LCD_COLOR_BLACK );
}

void LCD_Clear( uint16_t color )
{
    LCD_SetWindow( 0, 0, LCD_W - 1, LCD_H - 1 );

    for( uint32_t i = 0; i < ( ( uint32_t ) LCD_W * LCD_H ); i++ )
    {
        LCD_WriteData( color );
    }
}

void LCD_Fill( uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color )
{
    if( x1 >= LCD_W ) x1 = LCD_W - 1;
    if( y1 >= LCD_H ) y1 = LCD_H - 1;

    LCD_SetWindow( x0, y0, x1, y1 );

    uint32_t n = ( uint32_t ) ( x1 - x0 + 1 ) * ( y1 - y0 + 1 );

    for( uint32_t i = 0; i < n; i++ )
    {
        LCD_WriteData( color );
    }
}

void LCD_DrawPoint( uint16_t x, uint16_t y, uint16_t color )
{
    LCD_SetWindow( x, y, x, y );
    LCD_WriteData( color );
}

void LCD_ShowChar( uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg )
{
    if( ch < 0x20 || ch > 0x7E )
    {
        ch = '?';
    }

    const uint8_t *p = ascii_16x24[ ( uint8_t ) ch - 0x20 ];

    LCD_SetWindow( x, y, x + FONT_W - 1, y + FONT_H - 1 );

    for( uint8_t row = 0; row < FONT_H; row++ )
    {
        uint16_t bits = ( ( uint16_t ) p[ row * 2 ] << 8 ) | p[ row * 2 + 1 ];

        for( uint8_t col = 0; col < FONT_W; col++ )
        {
            LCD_WriteData( ( bits & ( 0x8000 >> col ) ) ? fg : bg );
        }
    }
}

void LCD_ShowString( uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg )
{
    uint16_t cur = x;

    while( *str )
    {
        if( *str == '\n' )
        {
            y += FONT_H;
            cur = x;
        }
        else
        {
            LCD_ShowChar( cur, y, *str, fg, bg );
            cur += FONT_W;
            if( cur + FONT_W > LCD_W )
            {
                cur = x;
                y += FONT_H;
            }
        }
        str++;
    }
}

/* 保留, 读 ID 时可用 */
uint16_t LCD_ReadID( void )
{
    LCD_WriteCmd( 0xD3 );
    LCD_ReadData();                 /* 空读一次 */
    LCD_ReadData();
    uint16_t id = LCD_ReadData() << 8;
    id |= LCD_ReadData();
    return id;                      /* ILI9341 应返回 0x9341 */
}
