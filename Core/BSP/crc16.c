#include "crc16.h"

uint16_t CRC16_Modbus( const uint8_t *data, uint16_t len )
{
    uint16_t crc = 0xFFFFU;

    for( uint16_t i = 0U; i < len; i++ )
    {
        crc ^= data[ i ];

        for( uint8_t b = 0U; b < 8U; b++ )
        {
            if( crc & 0x0001U )
            {
                crc = ( uint16_t )( ( crc >> 1 ) ^ 0xA001U );
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}
