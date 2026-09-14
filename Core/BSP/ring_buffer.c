#include "ring_buffer.h"

void RingBuf_Init( RingBuf_t *r, uint8_t *storage, uint16_t size )
{
    r->buf  = storage;
    r->size = size;
    r->head = 0U;
    r->tail = 0U;
}

int RingBuf_Write( RingBuf_t *r, uint8_t b )
{
    uint16_t next = ( uint16_t )( ( r->head + 1U ) % r->size );

    if( next == r->tail )
    {
        return -1;  /* 满了, 直接丢, 不覆盖旧数据 */
    }

    r->buf[ r->head ] = b;
    r->head = next;
    return 0;
}

int RingBuf_Read( RingBuf_t *r, uint8_t *b )
{
    if( r->head == r->tail )
    {
        return -1;  /* 空 */
    }

    *b      = r->buf[ r->tail ];
    r->tail = ( uint16_t )( ( r->tail + 1U ) % r->size );
    return 0;
}

uint16_t RingBuf_Available( const RingBuf_t *r )
{
    if( r->head >= r->tail )
    {
        return ( uint16_t )( r->head - r->tail );
    }

    return ( uint16_t )( r->size - r->tail + r->head );
}
