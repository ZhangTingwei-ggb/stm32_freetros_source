#ifndef __CRC16_H
#define __CRC16_H

#include <stdint.h>

/* CRC16-MODBUS, 多项式 0xA001(反转的0x8005), 初值 0xFFFF。
 * 自定义串口协议帧校验 + Flash 参数完整性校验共用。 */
uint16_t CRC16_Modbus( const uint8_t *data, uint16_t len );

#endif /* __CRC16_H */
