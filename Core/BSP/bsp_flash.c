#include "bsp_flash.h"

int BSP_Flash_EraseParamPage( void )
{
    FLASH_EraseInitTypeDef erase = { 0 };
    uint32_t               pageErr = 0U;

    HAL_FLASH_Unlock();

    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = PARAM_FLASH_ADDR;
    erase.NbPages     = 1U;

    int ok = ( HAL_FLASHEx_Erase( &erase, &pageErr ) == HAL_OK );

    HAL_FLASH_Lock();
    return ok;
}

void BSP_Flash_Read( uint32_t addr, uint8_t *dst, uint32_t len )
{
    const uint8_t *p = ( const uint8_t * ) addr;

    for( uint32_t i = 0U; i < len; i++ )
    {
        dst[ i ] = p[ i ];
    }
}

int BSP_Flash_WriteHalfwords( uint32_t addr, const uint16_t *data, uint32_t hwCount )
{
    HAL_FLASH_Unlock();

    int ok = 1;

    for( uint32_t i = 0U; i < hwCount; i++ )
    {
        if( HAL_FLASH_Program( FLASH_TYPEPROGRAM_HALFWORD,
                               addr + i * 2U, data[ i ] ) != HAL_OK )
        {
            ok = 0;
            break;
        }
    }

    HAL_FLASH_Lock();
    return ok;
}
