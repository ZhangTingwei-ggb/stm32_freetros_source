#ifndef __CUSTOM_PROTOCOL_H
#define __CUSTOM_PROTOCOL_H

#include "app_shared.h"

/* 自定义串口协议, 文档 6.1:
 *   帧头0xAA | 设备ID | CMD | LEN | DATA[LEN] | CRC16低 | CRC16高
 * CRC 范围: 设备ID + CMD + LEN + DATA, 算法 CRC16-MODBUS。
 *
 * CMD: 0x01 上行上报(温度float+湿度float+电压float, 小端)
 *      0x02 下行设置(周期u16小端ms + 设备ID u8, 掉电保存)
 *      0x03 下行查询(无数据) / 上行应答(固件版本u16 + 设备ID u8) */

#define CPROTO_HEAD     0xAAU
#define CPROTO_CMD_UP   0x01U
#define CPROTO_CMD_SET  0x02U
#define CPROTO_CMD_QRY  0x03U
#define CPROTO_FW_VER   0x0100U
#define CPROTO_MAX_DATA 32U

uint16_t CProto_BuildFrame( uint8_t dev, uint8_t cmd, const uint8_t *data,
                            uint8_t len, uint8_t *out, uint16_t outSize );
uint16_t CProto_BuildUpload( const SensorData_t *s, uint8_t *out, uint16_t outSize );

/* 逐字节状态机, 凑齐一帧且 CRC 通过后置应答, 返回 1 表示 resp 里有应答要发 */
typedef struct
{
    uint8_t buf[ CPROTO_MAX_DATA + 6U ];
    uint8_t idx;
    uint8_t step;
} CProto_Parser_t;

void CProto_ParserInit( CProto_Parser_t *p );
int  CProto_InFrame( const CProto_Parser_t *p );
int  CProto_ParseByte( CProto_Parser_t *p, uint8_t b,
                       uint8_t *resp, uint16_t *respLen );

#endif /* __CUSTOM_PROTOCOL_H */
