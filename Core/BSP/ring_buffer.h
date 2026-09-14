#ifndef __RING_BUFFER_H
#define __RING_BUFFER_H

#include <stdint.h>

/* 单生产者(串口DMA空闲中断)单消费者(协议任务)环形缓冲,
 * 头尾指针读写天然互斥, 不用加锁。 */
typedef struct
{
    uint8_t          *buf;
    uint16_t          size;
    volatile uint16_t head;     /* 写指针, 中断里动 */
    volatile uint16_t tail;     /* 读指针, 任务里动 */
} RingBuf_t;

void     RingBuf_Init( RingBuf_t *r, uint8_t *storage, uint16_t size );
int      RingBuf_Write( RingBuf_t *r, uint8_t b );   /* 0=成功, -1=满(丢弃) */
int      RingBuf_Read( RingBuf_t *r, uint8_t *b );   /* 0=成功, -1=空 */
uint16_t RingBuf_Available( const RingBuf_t *r );

#endif /* __RING_BUFFER_H */
