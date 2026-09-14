#ifndef __BSP_FLASH_H
#define __BSP_FLASH_H

#include "main.h"

/* 参数掉电保存: 用 512KB Flash 最后一页(2KB, 起始 0x0807F800)。
 * F1 只能按半字写, 写之前整页擦。 */

#define PARAM_FLASH_ADDR        ( ( uint32_t ) 0x0807F800UL )
#define PARAM_FLASH_PAGE_SIZE   2048U

int  BSP_Flash_EraseParamPage( void );                                   /* 1=成功 */
void BSP_Flash_Read( uint32_t addr, uint8_t *dst, uint32_t len );
int  BSP_Flash_WriteHalfwords( uint32_t addr, const uint16_t *data,
                               uint32_t hwCount );                       /* 1=成功 */

#endif /* __BSP_FLASH_H */
