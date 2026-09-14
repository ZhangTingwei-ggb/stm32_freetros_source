#include "param_storage.h"
#include "bsp_flash.h"
#include "crc16.h"
#include <string.h>

/* 正好 8 字节 = 4 个半字, 直接整块写入 Flash。CRC 只算前 6 字节。 */
typedef struct
{
    uint32_t magic;
    uint8_t  dev_id;
    uint8_t  modbus_addr;
    uint16_t period_ms;
    uint16_t crc;
} ParamFlash_t;

static ParamFlash_t s_param;

static uint16_t Param_CalcCrc( const ParamFlash_t *p )
{
    return CRC16_Modbus( ( const uint8_t * ) p,
                         ( uint16_t )( sizeof( ParamFlash_t ) - 2U ) );
}

static void Param_UseDefault( void )
{
    s_param.magic       = PARAM_MAGIC;
    s_param.dev_id      = PARAM_DEFAULT_DEV_ID;
    s_param.modbus_addr = PARAM_DEFAULT_MODBUS_ADDR;
    s_param.period_ms   = PARAM_DEFAULT_PERIOD_MS;
    s_param.crc         = Param_CalcCrc( &s_param );
}

void Param_Load( void )
{
    ParamFlash_t tmp;

    BSP_Flash_Read( PARAM_FLASH_ADDR, ( uint8_t * ) &tmp, sizeof( tmp ) );

    if( tmp.magic != PARAM_MAGIC || tmp.crc != Param_CalcCrc( &tmp ) )
    {
        Param_UseDefault();   /* 空片或坏块: 用默认值, 不急着写回 */
        return;
    }

    s_param = tmp;

    /* 即使 Flash 里有值, 越界的也 clamp, 防止上位机曾经写坏 */
    if( s_param.period_ms < PARAM_PERIOD_MIN_MS ||
        s_param.period_ms > PARAM_PERIOD_MAX_MS )
    {
        s_param.period_ms = PARAM_DEFAULT_PERIOD_MS;
    }

    if( s_param.dev_id == 0U )
    {
        s_param.dev_id = PARAM_DEFAULT_DEV_ID;
    }

    if( s_param.modbus_addr == 0U || s_param.modbus_addr > 247U )
    {
        s_param.modbus_addr = PARAM_DEFAULT_MODBUS_ADDR;
    }
}

void Param_Save( void )
{
    uint16_t buf[ 4 ];

    s_param.magic = PARAM_MAGIC;
    s_param.crc   = Param_CalcCrc( &s_param );
    memcpy( buf, &s_param, sizeof( buf ) );

    if( !BSP_Flash_EraseParamPage() )
    {
        return;
    }

    if( !BSP_Flash_WriteHalfwords( PARAM_FLASH_ADDR, buf, 4U ) )
    {
        return;
    }

    /* 回读校验: 写坏了就当没写, 内存里的值照样用 */
    ParamFlash_t back;

    BSP_Flash_Read( PARAM_FLASH_ADDR, ( uint8_t * ) &back, sizeof( back ) );

    if( back.crc != s_param.crc )
    {
        /* 校验失败不处理, 下次上电 Param_Load 会回退默认值 */
    }
}

uint8_t Param_GetDevId( void )
{
    return s_param.dev_id;
}

uint8_t Param_GetModbusAddr( void )
{
    return s_param.modbus_addr;
}

uint16_t Param_GetPeriodMs( void )
{
    return s_param.period_ms;
}

void Param_SetDevId( uint8_t id )
{
    if( id == 0U )
    {
        return;
    }

    s_param.dev_id = id;
    Param_Save();
}

void Param_SetModbusAddr( uint8_t addr )
{
    if( addr == 0U || addr > 247U )
    {
        return;
    }

    s_param.modbus_addr = addr;
    Param_Save();
}

void Param_SetPeriodMs( uint16_t ms )
{
    if( ms < PARAM_PERIOD_MIN_MS || ms > PARAM_PERIOD_MAX_MS )
    {
        return;
    }

    s_param.period_ms = ms;
    Param_Save();
}
