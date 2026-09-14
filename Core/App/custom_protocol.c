#include "custom_protocol.h"
#include "param_storage.h"
#include "crc16.h"
#include <string.h>

enum
{
    ST_HEAD = 0U,
    ST_DEV,
    ST_CMD,
    ST_LEN,
    ST_DATA,
    ST_CRC_L,
    ST_CRC_H
};

uint16_t CProto_BuildFrame( uint8_t dev, uint8_t cmd, const uint8_t *data,
                            uint8_t len, uint8_t *out, uint16_t outSize )
{
    uint16_t total = ( uint16_t )( len + 6U );

    if( out == NULL || total > outSize )
    {
        return 0U;
    }

    out[ 0 ] = CPROTO_HEAD;
    out[ 1 ] = dev;
    out[ 2 ] = cmd;
    out[ 3 ] = len;

    if( len > 0U && data != NULL )
    {
        memcpy( &out[ 4 ], data, len );
    }

    uint16_t crc = CRC16_Modbus( &out[ 1 ], ( uint16_t )( len + 3U ) );
    out[ 4 + len ] = ( uint8_t )( crc & 0xFFU );
    out[ 5 + len ] = ( uint8_t )( crc >> 8 );

    return total;
}

uint16_t CProto_BuildUpload( const SensorData_t *s, uint8_t *out, uint16_t outSize )
{
    uint8_t  payload[ 12 ];
    float    temp = ( float ) s->temp_x10 / 10.0f;
    float    humi = ( float ) s->humi_x10 / 10.0f;
    float    volt = ( float ) s->volt_mv / 1000.0f;

    memcpy( &payload[ 0 ], &temp, 4U );
    memcpy( &payload[ 4 ], &humi, 4U );
    memcpy( &payload[ 8 ], &volt, 4U );

    return CProto_BuildFrame( Param_GetDevId(), CPROTO_CMD_UP,
                              payload, sizeof( payload ), out, outSize );
}

void CProto_ParserInit( CProto_Parser_t *p )
{
    p->idx  = 0U;
    p->step = ST_HEAD;
}

int CProto_InFrame( const CProto_Parser_t *p )
{
    return ( p->step != ST_HEAD ) ? 1 : 0;
}

/* 下行帧处理: 0x02 改参数并落盘(无应答, 上位机看下次上报), 0x03 回设备信息 */
static uint16_t CProto_HandleDown( const uint8_t *frame, uint8_t dataLen,
                                   uint8_t *resp, uint16_t respSize )
{
    uint8_t dev = frame[ 1 ];
    uint8_t cmd = frame[ 2 ];

    if( cmd == CPROTO_CMD_SET )
    {
        if( dataLen == 3U )
        {
            uint16_t period = ( uint16_t )( frame[ 4 ] |
                                            ( ( uint16_t ) frame[ 5 ] << 8 ) );
            Param_SetPeriodMs( period );   /* 非法值内部忽略, 不落盘 */
            Param_SetDevId( frame[ 6 ] );
        }

        return 0U;
    }

    if( cmd == CPROTO_CMD_QRY )
    {
        uint8_t info[ 3 ];

        info[ 0 ] = ( uint8_t )( CPROTO_FW_VER & 0xFFU );
        info[ 1 ] = ( uint8_t )( CPROTO_FW_VER >> 8 );
        info[ 2 ] = Param_GetDevId();

        return CProto_BuildFrame( dev, CPROTO_CMD_QRY, info, 3U, resp, respSize );
    }

    return 0U;  /* 0x01 下行 / 未知 CMD 直接丢 */
}

int CProto_ParseByte( CProto_Parser_t *p, uint8_t b,
                       uint8_t *resp, uint16_t *respLen )
{
    *respLen = 0U;

    switch( p->step )
    {
    case ST_HEAD:
        if( b == CPROTO_HEAD )
        {
            p->buf[ 0 ] = b;
            p->idx      = 1U;
            p->step     = ST_DEV;
        }
        break;

    case ST_DEV:
    case ST_CMD:
        p->buf[ p->idx++ ] = b;
        p->step++;
        break;

    case ST_LEN:
        if( b > CPROTO_MAX_DATA )
        {
            CProto_ParserInit( p );   /* LEN 越界, 整帧丢掉重同步 */
            break;
        }
        p->buf[ p->idx++ ] = b;
        p->step = ( b == 0U ) ? ST_CRC_L : ST_DATA;
        break;

    case ST_DATA:
        p->buf[ p->idx++ ] = b;
        if( p->idx >= ( uint8_t )( p->buf[ 3 ] + 4U ) )
        {
            p->step = ST_CRC_L;
        }
        break;

    case ST_CRC_L:
        p->buf[ p->idx++ ] = b;
        p->step = ST_CRC_H;
        break;

    case ST_CRC_H:
        p->buf[ p->idx ] = b;

        do
        {
            uint8_t  dataLen = p->buf[ 3 ];
            uint16_t want    = ( uint16_t )( p->buf[ 4 + dataLen ] |
                                            ( ( uint16_t ) b << 8 ) );

            /* 设备 ID 不对: 不是发给我的, 静默丢掉 */
            if( p->buf[ 1 ] != Param_GetDevId() )
            {
                break;
            }

            if( CRC16_Modbus( &p->buf[ 1 ], ( uint16_t )( dataLen + 3U ) ) != want )
            {
                break;  /* CRC 错直接丢 */
            }

            *respLen = CProto_HandleDown( p->buf, dataLen, resp, 96U );
        } while( 0 );

        CProto_ParserInit( p );

        return ( *respLen > 0U ) ? 1 : 0;

    default:
        CProto_ParserInit( p );
        break;
    }

    return 0;
}
