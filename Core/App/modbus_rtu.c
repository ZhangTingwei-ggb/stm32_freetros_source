#include "modbus_rtu.h"
#include "param_storage.h"
#include "crc16.h"
#include <stddef.h>

#define MB_IN_REG_N     3U
#define MB_HOLD_REG_N   3U
#define MB_COIL_N       2U

#define MB_EX_ILLEGAL_FUNC  0x01U
#define MB_EX_ILLEGAL_ADDR  0x02U
#define MB_EX_ILLEGAL_VALUE 0x03U

static void MB_PutU16BE( uint8_t *p, uint16_t v )
{
    p[ 0 ] = ( uint8_t )( v >> 8 );
    p[ 1 ] = ( uint8_t )( v & 0xFFU );
}

static uint16_t MB_GetU16BE( const uint8_t *p )
{
    return ( uint16_t )( ( ( uint16_t ) p[ 0 ] << 8 ) | p[ 1 ] );
}

/* 异常应答: 地址 + (功能码|0x80) + 异常码 + CRC */
static uint16_t MB_Exception( uint8_t addr, uint8_t func, uint8_t code,
                              uint8_t *resp )
{
    resp[ 0 ] = addr;
    resp[ 1 ] = ( uint8_t )( func | 0x80U );
    resp[ 2 ] = code;

    uint16_t crc = CRC16_Modbus( resp, 3U );
    resp[ 3 ] = ( uint8_t )( crc & 0xFFU );
    resp[ 4 ] = ( uint8_t )( crc >> 8 );

    return 5U;
}

static uint16_t MB_Finish( uint8_t *resp, uint16_t bodyLen )
{
    uint16_t crc = CRC16_Modbus( resp, bodyLen );
    resp[ bodyLen ]     = ( uint8_t )( crc & 0xFFU );
    resp[ bodyLen + 1 ] = ( uint8_t )( crc >> 8 );
    return ( uint16_t )( bodyLen + 2U );
}

/* ---- 数据源 ---- */

static int16_t MB_InReg( uint16_t addr )
{
    switch( addr )
    {
    case 0U: return g_latest.temp_x10;
    case 1U: return ( int16_t ) g_latest.humi_x10;
    case 2U: return ( int16_t ) g_latest.volt_mv;
    default: return 0;
    }
}

static uint16_t MB_HoldReg( uint16_t addr )
{
    switch( addr )
    {
    case 0U: return Param_GetPeriodMs();
    case 1U: return Param_GetDevId();
    case 2U: return Param_GetModbusAddr();
    default: return 0U;
    }
}

/* 返回 1 = 接受并落盘, 0 = 值非法 */
static int MB_WriteHoldReg( uint16_t addr, uint16_t val )
{
    switch( addr )
    {
    case 0U:
        if( val < PARAM_PERIOD_MIN_MS || val > PARAM_PERIOD_MAX_MS )
        {
            return 0;
        }
        Param_SetPeriodMs( val );
        return 1;

    case 1U:
        if( val == 0U || val > 254U )
        {
            return 0;
        }
        Param_SetDevId( ( uint8_t ) val );
        return 1;

    case 2U:
        if( val == 0U || val > 247U )
        {
            return 0;
        }
        Param_SetModbusAddr( ( uint8_t ) val );
        return 1;

    default:
        return 0;
    }
}

static int MB_CoilRead( uint16_t addr )
{
    if( addr == 0U )
    {
        return ( HAL_GPIO_ReadPin( LED0_GPIO_Port, LED0_Pin ) == GPIO_PIN_SET ) ? 1 : 0;
    }

    if( addr == 1U )
    {
        return ( HAL_GPIO_ReadPin( LED1_GPIO_Port, LED1_Pin ) == GPIO_PIN_SET ) ? 1 : 0;
    }

    return 0;
}

static void MB_CoilWrite( uint16_t addr, int on )
{
    GPIO_PinState s = on ? GPIO_PIN_SET : GPIO_PIN_RESET;

    if( addr == 0U )
    {
        HAL_GPIO_WritePin( LED0_GPIO_Port, LED0_Pin, s );
    }
    else if( addr == 1U )
    {
        HAL_GPIO_WritePin( LED1_GPIO_Port, LED1_Pin, s );
    }
}

uint16_t Modbus_Process( const uint8_t *req, uint16_t reqLen,
                         uint8_t *resp, uint16_t respSize )
{
    if( req == NULL || resp == NULL || reqLen < 4U || respSize < 8U )
    {
        return 0U;
    }

    /* CRC 先验, 错帧直接丢, 连异常都不回 */
    uint16_t want = ( uint16_t )( req[ reqLen - 2U ] |
                                 ( ( uint16_t ) req[ reqLen - 1U ] << 8 ) );

    if( CRC16_Modbus( req, ( uint16_t )( reqLen - 2U ) ) != want )
    {
        return 0U;
    }

    uint8_t  addr     = req[ 0 ];
    uint8_t  func     = req[ 1 ];
    uint8_t  own      = Param_GetModbusAddr();
    int      isBc     = ( addr == 0U );   /* 广播: 只执行写, 不回任何帧 */

    if( addr != own && !isBc )
    {
        return 0U;  /* 不是发给我的 */
    }

    uint16_t body = ( uint16_t )( reqLen - 2U );  /* 不含 CRC 的长度 */

    switch( func )
    {
    /* ---- 0x03 读保持 / 0x04 读输入 ---- */
    case 0x03U:
    case 0x04U:
    {
        if( body != 6U )
        {
            break;
        }

        uint16_t start = MB_GetU16BE( &req[ 2 ] );
        uint16_t count = MB_GetU16BE( &req[ 4 ] );
        uint16_t maxN  = ( func == 0x03U ) ? MB_HOLD_REG_N : MB_IN_REG_N;

        if( count == 0U || count > 8U || start + count > maxN )
        {
            if( !isBc )
            {
                return MB_Exception( addr, func, MB_EX_ILLEGAL_ADDR, resp );
            }
            return 0U;
        }

        if( ( uint16_t )( 3U + count * 2U ) > respSize - 2U )
        {
            return 0U;
        }

        resp[ 0 ] = addr;
        resp[ 1 ] = func;
        resp[ 2 ] = ( uint8_t )( count * 2U );

        for( uint16_t i = 0U; i < count; i++ )
        {
            uint16_t v = ( func == 0x03U ) ? MB_HoldReg( ( uint16_t )( start + i ) )
                                           : ( uint16_t ) MB_InReg( ( uint16_t )( start + i ) );
            MB_PutU16BE( &resp[ 3 + i * 2U ], v );
        }

        return MB_Finish( resp, ( uint16_t )( 3U + count * 2U ) );
    }

    /* ---- 0x06 写单个保持寄存器 ---- */
    case 0x06U:
    {
        if( body != 6U )
        {
            break;
        }

        uint16_t reg = MB_GetU16BE( &req[ 2 ] );
        uint16_t val = MB_GetU16BE( &req[ 4 ] );

        if( reg >= MB_HOLD_REG_N )
        {
            if( !isBc )
            {
                return MB_Exception( addr, func, MB_EX_ILLEGAL_ADDR, resp );
            }
            return 0U;
        }

        if( !MB_WriteHoldReg( reg, val ) )
        {
            if( !isBc )
            {
                return MB_Exception( addr, func, MB_EX_ILLEGAL_VALUE, resp );
            }
            return 0U;
        }

        if( isBc )
        {
            return 0U;
        }

        /* 写成功原帧回显 */
        for( uint16_t i = 0U; i < 6U && ( uint16_t )( i + 2U ) < respSize; i++ )
        {
            resp[ i ] = req[ i ];
        }

        return MB_Finish( resp, 6U );
    }

    /* ---- 0x10 写多个保持寄存器 ---- */
    case 0x10U:
    {
        if( body < 7U )
        {
            break;
        }

        uint16_t start = MB_GetU16BE( &req[ 2 ] );
        uint16_t count = MB_GetU16BE( &req[ 4 ] );
        uint8_t  bytes = req[ 6 ];

        if( count == 0U || count > MB_HOLD_REG_N ||
            start + count > MB_HOLD_REG_N || bytes != count * 2U ||
            body != ( uint16_t )( 7U + bytes ) )
        {
            if( !isBc )
            {
                return MB_Exception( addr, func, MB_EX_ILLEGAL_VALUE, resp );
            }
            return 0U;
        }

        for( uint16_t i = 0U; i < count; i++ )
        {
            if( !MB_WriteHoldReg( ( uint16_t )( start + i ),
                                  MB_GetU16BE( &req[ 7 + i * 2U ] ) ) )
            {
                if( !isBc )
                {
                    return MB_Exception( addr, func, MB_EX_ILLEGAL_VALUE, resp );
                }
                return 0U;
            }
        }

        if( isBc )
        {
            return 0U;
        }

        resp[ 0 ] = addr;
        resp[ 1 ] = func;
        MB_PutU16BE( &resp[ 2 ], start );
        MB_PutU16BE( &resp[ 4 ], count );

        return MB_Finish( resp, 6U );
    }

    /* ---- 0x01 读线圈 ---- */
    case 0x01U:
    {
        if( body != 6U )
        {
            break;
        }

        uint16_t start = MB_GetU16BE( &req[ 2 ] );
        uint16_t count = MB_GetU16BE( &req[ 4 ] );

        if( count == 0U || count > MB_COIL_N || start + count > MB_COIL_N )
        {
            if( !isBc )
            {
                return MB_Exception( addr, func, MB_EX_ILLEGAL_ADDR, resp );
            }
            return 0U;
        }

        uint8_t nBytes = ( uint8_t )( ( count + 7U ) / 8U );

        if( ( uint16_t )( 3U + nBytes ) > respSize - 2U )
        {
            return 0U;
        }

        resp[ 0 ] = addr;
        resp[ 1 ] = func;
        resp[ 2 ] = nBytes;

        for( uint8_t i = 0U; i < nBytes; i++ )
        {
            resp[ 3 + i ] = 0U;
        }

        for( uint16_t i = 0U; i < count; i++ )
        {
            if( MB_CoilRead( ( uint16_t )( start + i ) ) )
            {
                resp[ 3 + i / 8U ] |= ( uint8_t )( 1U << ( i % 8U ) );
            }
        }

        return MB_Finish( resp, ( uint16_t )( 3U + nBytes ) );
    }

    /* ---- 0x05 写单个线圈 ---- */
    case 0x05U:
    {
        if( body != 6U )
        {
            break;
        }

        uint16_t coil = MB_GetU16BE( &req[ 2 ] );
        uint16_t val  = MB_GetU16BE( &req[ 4 ] );

        if( coil >= MB_COIL_N )
        {
            if( !isBc )
            {
                return MB_Exception( addr, func, MB_EX_ILLEGAL_ADDR, resp );
            }
            return 0U;
        }

        if( val != 0xFF00U && val != 0x0000U )
        {
            if( !isBc )
            {
                return MB_Exception( addr, func, MB_EX_ILLEGAL_VALUE, resp );
            }
            return 0U;
        }

        MB_CoilWrite( coil, val == 0xFF00U );

        if( isBc )
        {
            return 0U;
        }

        for( uint16_t i = 0U; i < 6U && ( uint16_t )( i + 2U ) < respSize; i++ )
        {
            resp[ i ] = req[ i ];
        }

        return MB_Finish( resp, 6U );
    }

    /* ---- 0x0F 写多个线圈 ---- */
    case 0x0FU:
    {
        if( body < 7U )
        {
            break;
        }

        uint16_t start = MB_GetU16BE( &req[ 2 ] );
        uint16_t count = MB_GetU16BE( &req[ 4 ] );
        uint8_t  bytes = req[ 6 ];
        uint8_t  need  = ( uint8_t )( ( count + 7U ) / 8U );

        if( count == 0U || count > MB_COIL_N ||
            start + count > MB_COIL_N || bytes != need ||
            body != ( uint16_t )( 7U + bytes ) )
        {
            if( !isBc )
            {
                return MB_Exception( addr, func, MB_EX_ILLEGAL_VALUE, resp );
            }
            return 0U;
        }

        for( uint16_t i = 0U; i < count; i++ )
        {
            MB_CoilWrite( ( uint16_t )( start + i ),
                          ( req[ 7 + i / 8U ] >> ( i % 8U ) ) & 0x01U );
        }

        if( isBc )
        {
            return 0U;
        }

        resp[ 0 ] = addr;
        resp[ 1 ] = func;
        MB_PutU16BE( &resp[ 2 ], start );
        MB_PutU16BE( &resp[ 4 ], count );

        return MB_Finish( resp, 6U );
    }

    default:
        break;
    }

    /* 长度对不上 / 不支持的功能码 */
    if( !isBc )
    {
        uint8_t code = ( func == 0x01U || func == 0x03U || func == 0x04U ||
                         func == 0x05U || func == 0x06U ||
                         func == 0x0FU || func == 0x10U )
                       ? MB_EX_ILLEGAL_VALUE : MB_EX_ILLEGAL_FUNC;
        return MB_Exception( addr, func, code, resp );
    }

    return 0U;
}
