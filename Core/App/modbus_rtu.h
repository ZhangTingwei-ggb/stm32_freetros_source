#ifndef __MODBUS_RTU_H
#define __MODBUS_RTU_H

#include "app_shared.h"

/* 轻量 Modbus RTU 从站, 文档 6.2 的寄存器映射, 不搬 FreeModbus(64KB RAM 紧张)。
 * 输入寄存器(只读): 0x0000 温度x10(int16) / 0x0001 湿度x10 / 0x0002 电压mV
 * 保持寄存器(读写): 0x0000 采样周期ms / 0x0001 设备ID / 0x0002 从站地址(掉电保存)
 * 线圈: 0x0000 LED0(PB5) / 0x0001 LED1(PE5)
 * 功能码: 0x01 读线圈 / 0x03 读保持 / 0x04 读输入 /
 *          0x05 写单线圈 / 0x06 写单寄存器 / 0x0F 写多线圈 / 0x10 写多寄存器
 * 返回 0 = 无需应答(广播或地址不符), 否则返回应答帧长度(含 CRC)。 */

uint16_t Modbus_Process( const uint8_t *req, uint16_t reqLen,
                         uint8_t *resp, uint16_t respSize );

#endif /* __MODBUS_RTU_H */
